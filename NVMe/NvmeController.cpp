#include "NvmeController.h"
#include "nvme_intdefs.h"
#include <multiprocessor.h>

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

NVME::NVME(void* abar, size_t barsize, pci_address busaddr)
	:m_mbar(abar), m_mbarsize(barsize), m_busaddr(busaddr)
{
	m_countIoCompletion = 0;
	m_countIoSubmission = 0;
	m_completionReady = false;
}

void NVME::createAdminCommand(PNVME_COMMAND pCommand, NVME_ADMIN_COMMAND opcode, uint32_t namespaceId, paddr_t phyAddr, uint32_t cdword10, uint32_t cdword11)
{
	pCommand->opcode = opcode;
	pCommand->NSID = CPU_TO_LE32(namespaceId);
	pCommand->prp_fuse = NVME_PRP | NVME_NOTFUSED;
	pCommand->reserved = CPU_TO_LE64(0);
	pCommand->metadata_ptr = CPU_TO_LE64(0);
	pCommand->data_ptr.prp1 = CPU_TO_LE64(phyAddr);
	pCommand->data_ptr.prp2 = CPU_TO_LE64(0);
	pCommand->cdword_1x[0] = CPU_TO_LE32(cdword10);
	pCommand->cdword_1x[1] = CPU_TO_LE32(cdword11);
	memset(&pCommand->cdword_1x[2], 0, 4 * sizeof(uint32_le));
	
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
	m_adminCommandQueue = new CommandQueue(this, pasq, admin_queue_size, NVME_ADMIN_QUEUE_ID, NVME_ADMIN_QUEUE_ID);
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
	PciAllocateMsi(m_busaddr.segment, m_busaddr.bus, m_busaddr.device, m_busaddr.function, 1, &nvme_interrupt, this);

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
	//CNS, controller
	createAdminCommand(cmd, IDENTIFY, 0, get_physical_address(idbuf), 0x1, 0);
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
	kprintf_a("  Maximum Transfer Size: %d\n", m_maxTransferSize);

	//IDENTIFY namespaces
	//CNS, namespaces
	cmd = m_adminCommandQueue->get_entry();
	createAdminCommand(cmd, IDENTIFY, 0, get_physical_address(idbuf), 0x2, 0);
	token = m_adminCommandQueue->submit_entry(cmd);

	m_adminCompletionQueue->wait_event(1000, NVME_ADMIN_QUEUE_ID, token);
	uint32_le* identifiers = (uint32_t*)idbuf;

	//Create I/O queues
	size_t desired = CpuCount();

	desired = (desired == 0) ? 1 : desired;
	desired = (desired > UINT16_MAX) ? UINT16_MAX : desired;
	--desired;		//Zero's based

	cmd = m_adminCommandQueue->get_entry();
	//Number of Queues
	createAdminCommand(cmd, SET_FEATURES, 0, 0, 0x7, (desired << 16) | desired);
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
		if (!queue_valid(ioCompletion))
		{
			kprintf(u"Error allocating I/O completion queue\n");
			return;
		}
		m_IoCompletionQueues[i - 1] = ioCompletion;
		//Register queue
		cmd = m_adminCommandQueue->get_entry();
		//CDWORD 10 - queue size and ID
		//CDWORD 11 - interrupts & contig
		createAdminCommand(cmd, CREATE_IO_COMPLETION, 0, phyaddr, (maxents << 16) | i, (0 << 16) | (1 << 1) | (1 << 0));
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
		size_t pairedCompletion = i;
		CommandQueue* ioCommand = new CommandQueue(this, phyaddr, maxents * sizeof(NVME_COMMAND), i, pairedCompletion);
		if (!queue_valid(ioCommand))
		{
			kprintf(u"Error allocating I/O submission queue\n");
			return;
		}
		m_IoSubmissionQueues[i - 1] = ioCommand;
		//Register queue
		cmd = m_adminCommandQueue->get_entry();
		//CDWORD 10 - queue size and ID
		//CDWORD 11 - paired completion queue, priority, contig
		createAdminCommand(cmd, CREATE_IO_SUBMISSION, 0, phyaddr, (maxents << 16) | i, (pairedCompletion << 16) | (0 << 1) | (1 << 0));
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

uint64_le NVME::read_rawnvme_reg64(NVME_CONTROLLER_REGISTERS reg)
{
	return *raw_offset<volatile uint64_le*>(m_mbar, reg);
}

uint32_t NVME::read_nvme_reg32(NVME_CONTROLLER_REGISTERS reg)
{
	return LE_TO_CPU32(*raw_offset<volatile uint32_le*>(m_mbar, reg));
}
uint64_t NVME::read_nvme_reg64(NVME_CONTROLLER_REGISTERS reg)
{
	return LE_TO_CPU64(*raw_offset<volatile uint64_le*>(m_mbar, reg));
}
void NVME::write_nvme_reg32(NVME_CONTROLLER_REGISTERS reg, uint32_t value)
{
	*raw_offset<volatile uint32_le*>(m_mbar, reg) = CPU_TO_LE32(value);
}
void NVME::write_nvme_reg64(NVME_CONTROLLER_REGISTERS reg, uint64_t value)
{
	*raw_offset<volatile uint64_le*>(m_mbar, reg) = CPU_TO_LE64(value);
}

void* NVME::register_address(NVME_CONTROLLER_REGISTERS reg)
{
	return raw_offset<void*>(m_mbar, reg);
}

void NVME::write_doorbell(bool completionqueue, uint16_t queue, uint16_t value)
{
	size_t offset = 0x1000 + (2 * queue + (completionqueue ? 1 : 0)) * (4 << m_dstrd);
	*raw_offset<volatile uint32_le*>(m_mbar, offset) = CPU_TO_LE32(value);
}

void NVME::reset_controller()
{
	uint32_t nvme_cc = read_nvme_reg32(NVME_REG_CC);
	nvme_cc = (nvme_cc & ~(NVME_CC_EN_MASK)) | NVME_CC_DISABLE;
	write_nvme_reg32(NVME_REG_CC, nvme_cc);
}

NVME::CommandQueue* NVME::getIoSubmissionQueue()
{
	uint16_t subQueue = CpuCurrentLogicalId() + 1;
	if (subQueue > m_countIoSubmission)
		subQueue = m_countIoSubmission;
	return m_IoSubmissionQueues[subQueue - 1];
}
NVME::CompletionQueue* NVME::getIoCompletionQueue(CommandQueue* subQueue)
{
	return m_IoCompletionQueues[subQueue->getCompletionQueueId() - 1];
}
