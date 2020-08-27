#include "NvmeController.h"
#include "nvme_intdefs.h"

void NVME::CompletionQueue::dispatch_events()
{
	PNVME_COMPLETION cur = raw_offset<PNVME_COMPLETION>(m_queuebase, m_list_head*ENTRY_SIZE);
	while ((LE_TO_CPU16(cur->Status) & 1) == m_flag)
	{
		//Package completion event
		PCOMPLETION_INFO compinfo = new COMPLETION_INFO;
		compinfo->commandSpecific = LE_TO_CPU32(cur->CommandSpecific);
		compinfo->status = LE_TO_CPU16(cur->Status) >> 1;
		compinfo->SubmissionQueue = LE_TO_CPU16(cur->SQIdent);
		compinfo->CommandId = LE_TO_CPU16(cur->CommandIdent);

		uint32_t completionTag = NVME::completionTag(compinfo->SubmissionQueue, compinfo->CommandId);
		//Write this to the waiters
		auto st = acquire_spinlock(m_treelock);
		auto iter = m_waiters.find(completionTag);
		PWAITING_INFO inf;
		if (iter == m_waiters.end())
		{
			inf = new WAITING_INFO;
			m_waiters[completionTag] = inf;
			inf->wait_sem = create_semaphore(1, u"NVMe Completion Sem");
		}
		else
		{
			inf = iter->second;
			signal_semaphore(inf->wait_sem, 1);
		}
		inf->comp_info = compinfo;
		release_spinlock(m_treelock, st);

		m_list_head = next_after(m_list_head, m_flag);
		cur = raw_offset<PNVME_COMPLETION>(m_queuebase, m_list_head*ENTRY_SIZE);
	}
	m_parent->write_doorbell(true, m_queueid, m_list_head);
}


	

semaphore_t NVME::CompletionQueue::async_event(uint16_t subqId, uint16_t cmdid, pcommand_memory allocatedMem, PNVME_DMA_DESCRIPTOR dmaDesc)
{
	uint32_t completionTag = NVME::completionTag(subqId, cmdid);
	//Add wait event to the tree, or get the existing one
	auto st = acquire_spinlock(m_treelock);
	auto iter = m_waiters.find(completionTag);
	PWAITING_INFO inf;
	if (iter == m_waiters.end())
	{
		inf = new WAITING_INFO;
		m_waiters[completionTag] = inf;
		inf->wait_sem = create_semaphore(0, u"NVMe Completion Sem");
		inf->commandAllocated = allocatedMem;
		if (dmaDesc)
			inf->dmaDescriptor = *dmaDesc;
		else
			inf->dmaDescriptor.buffer = nullptr;
	}
	else
	{
		inf = iter->second;
		inf->commandAllocated = allocatedMem;
		if (dmaDesc)
			inf->dmaDescriptor = *dmaDesc;
		else
			inf->dmaDescriptor.buffer = nullptr;
	}
	release_spinlock(m_treelock, st);
	return inf->wait_sem;
}

PCOMPLETION_INFO NVME::CompletionQueue::get_completion(uint16_t subqId, uint16_t cmdid)
{
	uint32_t completionTag = NVME::completionTag(subqId, cmdid);

	auto st = acquire_spinlock(m_treelock);
	PWAITING_INFO inf = m_waiters[completionTag];
	PCOMPLETION_INFO result = inf->comp_info;
	m_waiters.remove(completionTag);
	release_spinlock(m_treelock, st);

	delete_semaphore(inf->wait_sem);
	free_command_memory(inf->commandAllocated);
	if (inf->dmaDescriptor.buffer != nullptr)
		nvmeFinishDma(inf->dmaDescriptor);
	delete inf;
	return result;
}

PCOMPLETION_INFO NVME::CompletionQueue::wait_event(uint64_t timeout, uint16_t subqId, uint16_t cmdid, pcommand_memory allocatedMem, PNVME_DMA_DESCRIPTOR dmaDesc)
{
	semaphore_t sem = async_event(subqId, cmdid, allocatedMem, dmaDesc);

	wait_semaphore(sem, 1, timeout);
	//Cleanup
	return get_completion(subqId, cmdid);
#if 0
	++m_waiting;
	uint64_t time = arch_get_system_timer();
	PNVME_COMPLETION cur = raw_offset<PNVME_COMPLETION>(m_queuebase, m_list_head*ENTRY_SIZE);
	PNVME_COMPLETION next = cur;
	do {
		cur = next;
		while (arch_get_system_timer() < time + timeout)
		{
			if ((cur->Status & 1) == m_flag)
				break;
		}
		if (arch_get_system_timer() >= time + timeout)
		{
			--m_waiting;
			return nullptr;		//Timeout
		}
		next = cur + 1;
	} while (cur->CommandIdent != cmdid);

	m_list_head = next_after(m_list_head, m_flag);
	--m_waiting;

	m_parent->write_doorbell(true, m_queueid, m_list_head);

	return cur;
#endif
}

bool NVME::CompletionQueue::is_valid()
{
	return m_queuebase != nullptr;
}
