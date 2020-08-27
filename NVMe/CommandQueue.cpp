#include "NvmeController.h"
#include "nvme_intdefs.h"

PNVME_COMMAND NVME::CommandQueue::get_entry()
{
	size_t token = list_tail;
	++m_waiting_count;
	while (!arch_cas(&list_tail, token, next_after(token)))
		token = list_tail;
	return raw_offset<PNVME_COMMAND>(m_queuebase, token * sizeof(NVME_COMMAND));
}

uint16_t NVME::CommandQueue::submit_entry(PNVME_COMMAND command)
{
	size_t slot = raw_diff(command, m_queuebase) / ENTRY_SIZE;
	uint16_t cmdid = m_cmd_id++;
	--m_waiting_count;
	command->command_id = cmdid;
	//Make sure that we don't preempt previous queue members
	if (m_waiting_count == 0)
		m_parent->write_doorbell(false, m_queueid, next_after(slot));
	return cmdid;
}

bool NVME::CommandQueue::is_valid()
{
	return m_queuebase != nullptr;
}
