#include "NvmeController.h"
#include "nvme_intdefs.h"

#define kprintf(...)

uint8_t nvme_interrupt(size_t vector, void* param);
void nvme_event_thread(void* param);

uint8_t nvme_interrupt(size_t vector, void* param)
{
	NVME* nvme = (NVME*)param;
	return nvme->interrupt(vector);
}

void nvme_event_thread(void* param)
{
	NVME* nvme = (NVME*)param;
	return nvme->eventThread();
}

void NVME::init()
{
	m_InterruptSemaphore = create_semaphore(0, u"NVMe Interrupt Sem");
	//NVMe initialisation - check that we actually support NVMe
	uint64_le nvmecap = read_rawnvme_reg64(NVME_REG_CAP);
	kprintf(u"Raw capabilities: %x\n", nvmecap);
	PNVME_CAP capreg = (PNVME_CAP)&nvmecap;
	if ((nvme_cap_css(capreg) & 1) == 0)
	{
		return kprintf(u"Error: NVMe command set not supported\n");
	}

	//Configure page size
	uint32_t mps = 0;
	uint32_t mps_size = 4096;		//(1<<12)
	while (mps_size < PAGESIZE)
	{
		++mps;
		mps_size <<= 1;
	}

	if (nvme_cap_mpsmin(capreg) > mps)	//Doesn't support system pages
		return kprintf(u"Error: NVMe does not support system page size\n");
	else if (nvme_cap_mpsmax(capreg) < mps)
		return kprintf(u"Error: NVMe does not support system page size\n");

	m_dstrd = nvme_cap_dstrd(capreg);

	uint32_t ver = read_nvme_reg32(NVME_REG_VS);
	uint_fast16_t major = (ver >> 16);
	uint_fast8_t minor = (ver >> 8) & 0xFF;
	uint_fast8_t rev = (ver & 0xFF);		//RsvdZ where not supported, so OK
	kprintf(u"NVMe version %d.%d.%d\n", major, minor, rev);

	reset_controller();

	size_t admin_queue_size = PAGESIZE;
	paddr_t pasq = 0, pacq = 0;
	m_adminCommandQueue = new CommandQueue(this, pasq, admin_queue_size, NVME_ADMIN_QUEUE_ID);
	if (!queue_valid(m_adminCommandQueue))
		return kprintf(u"Error allocating NVMe ASQ\n");

	m_adminCompletionQueue = new CompletionQueue(this, pacq, admin_queue_size, NVME_ADMIN_QUEUE_ID);
	if (!queue_valid(m_adminCommandQueue))
		return kprintf(u"Error allocating NVMe ACQ\n");

	write_nvme_reg64(NVME_REG_ASQ, pasq);
	write_nvme_reg64(NVME_REG_ACQ, pacq);

	//Admin queue attributes
	size_t admin_entries_command = admin_queue_size / 64;
	size_t admin_entries_complete = admin_queue_size / 16;
	write_nvme_reg32(NVME_REG_AQA, admin_entries_command | (admin_entries_command << 16));		//TODO: tweak

	//Set MSI interrupt
	pci_allocate_msi(m_busaddr.segment, m_busaddr.bus, m_busaddr.device, m_busaddr.function, 1, &nvme_interrupt, this);

	//Configure Controller
	auto regcc = read_nvme_reg32(NVME_REG_CC);
	regcc &= ~(NVME_CC_MPS_MASK | NVME_CC_AMS_MASK | NVME_CC_CSS_MASK | NVME_CC_IOCQES_MASK | NVME_CC_IOSQES_MASK);
	regcc |= ((mps << NVME_CC_MPS_SHIFT) | (4 << NVME_CC_IOCQES_SHIFT) | (6 << NVME_CC_IOSQES_SHIFT) | NVME_CC_AMS_ROUNDROBIN | NVME_CC_CSNVME);
	write_nvme_reg32(NVME_REG_CC, regcc);

	//Create event thread
	create_thread(&nvme_event_thread, this, THREAD_PRIORITY_NORMAL, DRIVER_EVENT);

	//Enable Controller
	regcc = read_nvme_reg32(NVME_REG_CC);
	regcc |= NVME_CC_EN;
	write_nvme_reg32(NVME_REG_CC, regcc);

	size_t milliseconds = capreg->timeout * 500;
	if (!timeout_check_reg_flags32(NVME_REG_CSTS, NVME_CSTS_RDY, NVME_CSTS_RDY, milliseconds))
		return kprintf(u"NVMe Controller timed out tranisitioning to Ready (%d ms)\n", milliseconds);

	kprintf(u"NVMe Controller Started up\n");

	//IDENTIFY
	void* idbuf = find_free_paging(4096);
	if (!paging_map(idbuf, PADDR_ALLOCATE, 4096, PAGE_ATTRIBUTE_NO_CACHING | PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_EXECUTE))
		return kprintf(u"Could not allocate IDENTIFY buffer\n");

	PNVME_COMMAND cmd = m_adminCommandQueue->get_entry();
	cmd->opcode = IDENTIFY;		//IDENTIFY
	cmd->NSID = 0;
	cmd->prp_fuse = NVME_PRP | NVME_NOTFUSED;
	cmd->reserved = 0;
	cmd->metadata_ptr = 0;
	cmd->data_ptr.prp1 = get_physical_address(idbuf);
	cmd->data_ptr.prp2 = 0;
	cmd->cdword_1x[0] = 0x1;		//CNS, controller
	memset(&cmd->cdword_1x[1], 0, 5 * sizeof(uint32_t));

	auto token = m_adminCommandQueue->submit_entry(cmd);

	PCOMPLETION_INFO completion = m_adminCompletionQueue->wait_event(1000, NVME_ADMIN_QUEUE_ID, token);
	kprintf(u"Identify Command Completed: status %x\n", completion->status);
	PNVME_CONTROLLER ctrlid = (PNVME_CONTROLLER)idbuf;
	ctrlid->ModelNumber[39] = 0;
	kprintf_a("  Model Number: %s\n", ctrlid->ModelNumber);
	ctrlid->SerialNumber[19] = 0;
	kprintf_a("  Serial Number: %s\n", ctrlid->SerialNumber);

	m_SglSupport = ctrlid->SglSupport;
	kprintf_a("  Scatter Gather support: %d\n", ctrlid->SglSupport);

	m_maxTransferSize = ctrlid->MaximumTransferSize;

	//IDENTIFY namespaces
	cmd = m_adminCommandQueue->get_entry();
	cmd->opcode = IDENTIFY;		//IDENTIFY
	cmd->NSID = 0;
	cmd->prp_fuse = NVME_PRP | NVME_NOTFUSED;
	cmd->reserved = 0;
	cmd->metadata_ptr = 0;
	cmd->data_ptr.prp1 = get_physical_address(idbuf);
	cmd->data_ptr.prp2 = 0;
	cmd->cdword_1x[0] = 0x2;		//CNS, namespaces
	memset(&cmd->cdword_1x[1], 0, 5 * sizeof(uint32_t));

	token = m_adminCommandQueue->submit_entry(cmd);

	m_adminCompletionQueue->wait_event(1000, NVME_ADMIN_QUEUE_ID, token);
	uint32_le* identifiers = (uint32_t*)idbuf;

	//Create I/O queues
	size_t desired = num_cpus();

	desired = (desired == 0) ? 1 : desired;
	desired = (desired > UINT16_MAX) ? UINT16_MAX : desired;
	--desired;		//Zero's based

	cmd = m_adminCommandQueue->get_entry();
	cmd->opcode = SET_FEATURES;
	cmd->NSID = 0;
	cmd->prp_fuse = NVME_PRP | NVME_NOTFUSED;
	cmd->reserved = 0;
	cmd->metadata_ptr = 0;
	cmd->data_ptr.prp1 = 0;
	cmd->data_ptr.prp2 = 0;
	memset(cmd->cdword_1x, 0, 6 * sizeof(uint32_t));

	cmd->cdword_1x[0] = 0x7;	//Number of Queues
	cmd->cdword_1x[1] = (desired << 16) | desired;
	token = m_adminCommandQueue->submit_entry(cmd);

	PCOMPLETION_INFO setfeature = m_adminCompletionQueue->wait_event(1000, NVME_ADMIN_QUEUE_ID, token);
	uint32_t allocated = setfeature->commandSpecific;
	m_countIoCompletion = (allocated >> 16) + 1;
	m_countIoSubmission = (allocated & UINT16_MAX) + 1;

	kprintf(u"  %d queues requested, %d allocated\n", desired + 1, (allocated & UINT16_MAX) + 1);

	if (m_countIoCompletion > desired + 1)
		m_countIoCompletion = desired + 1;

	if (m_countIoSubmission > desired + 1)
		m_countIoSubmission = desired + 1;

	m_IoSubmissionQueues = new CommandQueue*[m_countIoSubmission];
	m_IoCompletionQueues = new CompletionQueue*[m_countIoCompletion];

	uint32_t maxents = LE_TO_CPU16(capreg->MQES) + 1;
	maxents = (maxents > (PAGESIZE / sizeof(NVME_COMMAND))) ? (PAGESIZE / sizeof(NVME_COMMAND)) : maxents;



	for (unsigned i = 1; i <= m_countIoCompletion; ++i)
	{
		paddr_t phyaddr;
		CompletionQueue* ioCompletion = new CompletionQueue(this, phyaddr, maxents * sizeof(NVME_COMPLETION), i);
		m_IoCompletionQueues[i - 1] = ioCompletion;
		//Register queue
		cmd = m_adminCommandQueue->get_entry();
		cmd->opcode = CREATE_IO_COMPLETION;
		cmd->NSID = 0;
		cmd->prp_fuse = NVME_PRP | NVME_NOTFUSED;
		cmd->reserved = 0;
		cmd->metadata_ptr = 0;
		cmd->data_ptr.prp1 = phyaddr;
		cmd->data_ptr.prp2 = 0;
		memset(cmd->cdword_1x, 0, 6 * sizeof(uint32_t));
		//CDWORD 10 - queue size and ID
		cmd->cdword_1x[0] = (maxents << 16) | i;
		//CDWORD 11 - interrupts & contig
		cmd->cdword_1x[1] = (0 << 16) | (1 << 1) | (1 << 0);

		token = m_adminCommandQueue->submit_entry(cmd);
		PCOMPLETION_INFO createio = m_adminCompletionQueue->wait_event(1000, NVME_ADMIN_QUEUE_ID, token);
		if (i == 1)
		{
			kprintf(u"    Completion queue 1 status: %x\n", createio->status);
		}

	}
	kprintf(u"  Created completion queues\n");
	m_completionReady = true;

	for (unsigned i = 1; i <= m_countIoSubmission; ++i)
	{
		paddr_t phyaddr;
		CommandQueue* ioCommand = new CommandQueue(this, phyaddr, maxents * sizeof(NVME_COMMAND), i);
		m_IoSubmissionQueues[i - 1] = ioCommand;
		//Register queue
		cmd = m_adminCommandQueue->get_entry();
		cmd->opcode = CREATE_IO_SUBMISSION;
		cmd->NSID = 0;
		cmd->prp_fuse = NVME_PRP | NVME_NOTFUSED;
		cmd->reserved = 0;
		cmd->metadata_ptr = 0;
		cmd->data_ptr.prp1 = phyaddr;
		cmd->data_ptr.prp2 = 0;
		memset(cmd->cdword_1x, 0, 6 * sizeof(uint32_t));
		//CDWORD 10 - queue size and ID
		cmd->cdword_1x[0] = (maxents << 16) | i;
		//CDWORD 11 - paired completion queue, priority, contig
		cmd->cdword_1x[1] = (i << 16) | (0 << 1) | (1 << 0);

		token = m_adminCommandQueue->submit_entry(cmd);
		PCOMPLETION_INFO createio = m_adminCompletionQueue->wait_event(1000, NVME_ADMIN_QUEUE_ID, token);
		if (i == 1)
		{
			kprintf(u"    Submission queue 1 status: %x\n", createio->status);
		}

	}
	kprintf(u"  Created submission queues\n");

	for (int i = 0; i < 1024; ++i)
	{
		if (identifiers[i] == 0)
			break;

		//Export namespace as block device
		NvmeNamespace* nspace = new NvmeNamespace(this, identifiers[i]);
		nspace->initialize();
	}

	delete[] idbuf;
}

void* NVME::allocate_queue(size_t length, paddr_t & paddr)
{
	uint8_t memregion = ARCH_PHY_REGION_NORMAL;
	paddr = pmmngr_allocate(DIV_ROUND_UP(length, PAGESIZE), memregion);
	if (paddr == NULL)
		return nullptr;
	void* alloc = find_free_paging(length);
	if (!paging_map(alloc, paddr, length, PAGE_ATTRIBUTE_NO_CACHING | PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_EXECUTE))
	{
		pmmngr_free(paddr, DIV_ROUND_UP(length, PAGESIZE));
		return nullptr;
	}
	return alloc;
}

bool NVME::timeout_check_reg_flags32(NVME_CONTROLLER_REGISTERS index, uint32_t mask, uint32_t value, uint32_t timeout)
{
	uint64_t count = arch_get_system_timer();
	while (arch_get_system_timer() < count + timeout)
	{
		if ((read_nvme_reg32(index) & mask) == value)
			return true;
	}
	return false;
}

uint8_t NVME::interrupt(size_t vector)
{
	signal_semaphore(m_InterruptSemaphore, 1);
	//kprintf_a("NVMe Interrupt\n");
	return 1;
}

void NVME::eventThread()
{
	while (true)
	{
		wait_semaphore(m_InterruptSemaphore, 1, TIMEOUT_INFINITY);
		m_adminCompletionQueue->dispatch_events();

		if (m_completionReady)
		{
			for (unsigned i = 0; i < m_countIoCompletion; ++i)
				m_IoCompletionQueues[i]->dispatch_events();
		}
	}
}

void NVME::free_command_memory(pcommand_memory& memoryPtr)
{
	if (!memoryPtr)
		return;
	for (auto it = memoryPtr->begin(); it != memoryPtr->end(); ++it)
	{
		paging_free(it->second, PAGESIZE);
	}
	delete memoryPtr;
	memoryPtr = nullptr;
}

void* NVME::createNvmeCommandBuffer(command_memory& AllocatedSegments, paddr_t& phyaddr)
{
	phyaddr = pmmngr_allocate(1);
	if (!phyaddr)
		return nullptr;
	void* buffer = find_free_paging(PAGESIZE);
	if (!paging_map((void*)buffer, phyaddr, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING))
	{
		pmmngr_free(phyaddr, 1);
		phyaddr = 0;
		return nullptr;
	}
	AllocatedSegments[phyaddr] = buffer;
	return buffer;
}

bool NVME::nvmePrepareDma(void *__user buffer, size_t length, NVME_DMA_DESCRIPTOR & dmaDescriptor)
{
	size_t desccount = PagingPrepareDma(buffer, length, NULL, 0, false);
	if (desccount == 0)
		return (length == 0);
	dmaDescriptor.physicalAddresses = new PAGING_PHYADDR_DESC[desccount];
	if (!dmaDescriptor.physicalAddresses)
		return false;

	dmaDescriptor.entryCount = PagingPrepareDma(buffer, length, dmaDescriptor.physicalAddresses, desccount, false);
	if (dmaDescriptor.entryCount == 0)
	{
		nvmeFinishDma(dmaDescriptor);
		return false;
	}
	dmaDescriptor.buffer = buffer;
	dmaDescriptor.bufLength = length;
	return true;
}

void NVME::nvmeFinishDma(NVME_DMA_DESCRIPTOR & dmaDescriptor)
{
	PagingFinishDma(dmaDescriptor.buffer, dmaDescriptor.bufLength);
	if (dmaDescriptor.physicalAddresses)
	{
		delete[] dmaDescriptor.physicalAddresses;
		dmaDescriptor.physicalAddresses = nullptr;
	}
}

pcommand_memory NVME::create_prp_list(void *__user buffer, size_t length, const PNVME_COMMAND command, NVME_DMA_DESCRIPTOR & dmaDescriptor)
{
	/*size_t desccount = PagingPrepareDma(buffer, length, NULL, 0, false);
	PPAGING_PHYADDR_DESC descriptors = new PAGING_PHYADDR_DESC[desccount];
	size_t count = PagingPrepareDma(buffer, length, descriptors, desccount, false);*/

	auto err_return = [&dmaDescriptor](pcommand_memory memory) -> pcommand_memory
	{
		nvmeFinishDma(dmaDescriptor);
		if (memory)
			free_command_memory(memory);
		return nullptr;
	};

	pcommand_memory pMemoryTracker = new command_memory;
	if (!pMemoryTracker)
		return err_return(nullptr);
	command_memory& MemoryTracker = *pMemoryTracker;

	PPAGING_PHYADDR_DESC curdesc = &dmaDescriptor.physicalAddresses[0];
	PPAGING_PHYADDR_DESC end = &dmaDescriptor.physicalAddresses[dmaDescriptor.entryCount];

	size_t remaining_length = length;

	//First PRP is dealt with directly, and may have an offset
	command->data_ptr.prp1 = CPU_TO_LE64(curdesc->phyaddr);
	size_t offset = curdesc->phyaddr & (PAGESIZE - 1);
	size_t curlen = PAGESIZE - offset;
	curdesc->length -= (remaining_length > curlen ? curlen : remaining_length);
	if (curdesc->length == 0)
		++curdesc;
	remaining_length -= curlen;

	//Check if PRP2 is direct
	if (remaining_length < PAGESIZE)
	{
		if (remaining_length != 0)
			command->data_ptr.prp2 = CPU_TO_LE64(curdesc->phyaddr);
		else
			command->data_ptr.prp2 = CPU_TO_LE64(0);
		return pMemoryTracker;
	}

	//PRP2 is indirect. Build the structures
	size_t entries = remaining_length / PAGESIZE;

	curlen = PAGESIZE;		//We deal in pages for everything else

	paddr_t pdpage = 0;
	uint64_le* window = createPrpBuffer(MemoryTracker, pdpage);
	if (!window)
		return err_return(pMemoryTracker);
	uint64_le* curent = window;
	size_t page_length_remaining = PAGESIZE / sizeof(uint64_le);

	while (curdesc != end)
	{
		while (curdesc->length > 0)
		{
			*curent++ = curdesc->phyaddr;
			--page_length_remaining;
			if (page_length_remaining == 1)
			{
				//Linked list time
				paddr_t newpage = 0;
				window = createPrpBuffer(MemoryTracker, newpage);
				if (!window)
					return err_return(pMemoryTracker);
				*curent = newpage;

				curent = window;
			}
			curdesc->phyaddr += PAGESIZE;
			curdesc->length = (curdesc->length < PAGESIZE ? 0 : curdesc->length - PAGESIZE);
		}
		++curdesc;
	}

	command->data_ptr.prp2 = CPU_TO_LE64(pdpage);
	paging_free(window, PAGESIZE, false);

	return pMemoryTracker;
	//kprintf_a("%d descriptors required to represent buffer %x (len %d)\n", desccount, buffer, length);
}

pcommand_memory NVME::create_sgl_list(void *__user buffer, size_t length, const PNVME_COMMAND command, NVME_DMA_DESCRIPTOR & dmaDescriptor)
{
	kprintf(u"WARNING: SGL Lists are currently untested\n");
	/*size_t desccount = PagingPrepareDma(buffer, length, NULL, 0, false);
	PPAGING_PHYADDR_DESC descriptors = new PAGING_PHYADDR_DESC[desccount];
	size_t count = PagingPrepareDma(buffer, length, descriptors, desccount, false);*/

	auto err_return = [&dmaDescriptor](pcommand_memory memory) -> pcommand_memory
	{
		nvmeFinishDma(dmaDescriptor);
		if (memory)
			free_command_memory(memory);
		return nullptr;
	};

	pcommand_memory MemoryTracker = new command_memory;
	if (!MemoryTracker)
		return err_return(nullptr);
	command_memory& AllocatedSegments = *MemoryTracker;

	const size_t maxSegment = PAGESIZE / sizeof(NVME_SGL);

	auto writeSglFromPhydesc = [](NVME_SGL& sgl, PAGING_PHYADDR_DESC& phydesc)
	{
		sgl.address = phydesc.phyaddr;
		sgl.length = phydesc.length;
		sgl.high = sgl.highb = 0;
		sgl.sglid = sglid(SGL_DATA_BLOCK, SGL_ADDRESS);
	};

	auto calculateLength = [maxSegment](size_t len) -> size_t
	{
		return (len > maxSegment ? maxSegment : len) * sizeof(NVME_SGL);
	};

	PNVME_SGL sgl = &command->data_ptr.scatter_gather;
	if (dmaDescriptor.entryCount == 1)
	{
		//Direct SGL
		writeSglFromPhydesc(*sgl, dmaDescriptor.physicalAddresses[0]);
	}
	else
	{
		paddr_t Segment = 0;
		PNVME_SGL mappedSegment;
		size_t currentEntry = 0;

		mappedSegment = createSegment(AllocatedSegments, Segment);
		if (!mappedSegment)
			return err_return(nullptr);

		if ((dmaDescriptor.entryCount - currentEntry) > maxSegment)
			sgl->sglid = sglid(SGL_SEGMENT, SGL_ADDRESS);
		else
			sgl->sglid = sglid(SGL_LAST_SEGMENT, SGL_ADDRESS);
		sgl->length = calculateLength(dmaDescriptor.entryCount - currentEntry);
		sgl->address = Segment;
		sgl->high = sgl->highb = 0;

		kprintf(u"Created SGL: %x, length %d, type %x, mapped %x\n", sgl->address, sgl->length, sgl->sglid, mappedSegment);

		while (currentEntry < dmaDescriptor.entryCount)
		{
			sgl = mappedSegment;

			for (unsigned i = 0; (currentEntry < dmaDescriptor.entryCount) && (i < maxSegment - 1); ++i)
			{
				writeSglFromPhydesc(sgl[i], dmaDescriptor.physicalAddresses[currentEntry]);
				++currentEntry;
				kprintf(u"  SGL entry: %x, length %d, type %x\n", sgl[i].address, sgl[i].length, sgl[i].sglid);
			}

			if (currentEntry == dmaDescriptor.entryCount - 1)
			{
				//Special case: we are the last segment completely full
				writeSglFromPhydesc(sgl[maxSegment - 1], dmaDescriptor.physicalAddresses[currentEntry]);
				kprintf(u"  SGL entry: %x, length %d\n", sgl[maxSegment - 1].address, sgl[maxSegment - 1].length);
				++currentEntry;
			}
			else if (currentEntry < dmaDescriptor.entryCount)
			{
				mappedSegment = createSegment(AllocatedSegments, Segment);
				if (!mappedSegment)
					return err_return(MemoryTracker);

				if ((dmaDescriptor.entryCount - currentEntry) > maxSegment)
					sgl[maxSegment - 1].sglid = sglid(SGL_SEGMENT, SGL_ADDRESS);
				else
					sgl[maxSegment - 1].sglid = sglid(SGL_LAST_SEGMENT, SGL_ADDRESS);
				sgl[maxSegment - 1].address = Segment;
				sgl[maxSegment - 1].length = calculateLength(dmaDescriptor.entryCount - currentEntry);
				sgl[maxSegment - 1].high = sgl[maxSegment - 1].highb = 0;
				kprintf(u"  SGL entry: %x, length %d\n", sgl[maxSegment - 1].address, sgl[maxSegment - 1].length);
			}
		}

	}
	return MemoryTracker;
}
