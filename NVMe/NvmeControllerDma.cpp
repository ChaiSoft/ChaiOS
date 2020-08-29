#include "NvmeController.h"

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

//PRPs:
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
	curlen = (curdesc->length > curlen ? curlen : curdesc->length);
	curdesc->length -= curlen;
	if (curdesc->length == 0)
		++curdesc;
	else
		curdesc->phyaddr += curlen;
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

	return pMemoryTracker;
	//kprintf_a("%d descriptors required to represent buffer %x (len %d)\n", desccount, buffer, length);
}

//SGLs:
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