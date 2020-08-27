#include "NvmeController.h"

#define kprintf(...)

vds_err_t nvme_disk_read(void* param, lba_t blockaddr, vds_length_t count, void* buffer, semaphore_t* completionEvent);
vds_err_t nvme_disk_write(void* param, lba_t blockaddr, vds_length_t count, void* buffer, semaphore_t* completionEvent);
vds_err_t nvme_disk_flush(void* param, semaphore_t* completionEvent);
vds_err_t nvme_complete_async(void* param, vds_err_t token, semaphore_t completionEvent);
PCHAIOS_VDS_PARAMS nvme_disk_params(void* param);

NVME::NvmeNamespace::NvmeNamespace(NVME* parent, uint32_t nsid)
	: m_parent(parent), m_nsid(nsid)
{

}
	
void NVME::NvmeNamespace::initialize()
{
	void* nsbuf = new uint8_t[4096];
	kprintf(u"  Active namespace %d\n", m_nsid);
	auto cmd = m_parent->m_adminCommandQueue->get_entry();
	cmd->opcode = IDENTIFY;		//IDENTIFY
	cmd->NSID = m_nsid;
	cmd->prp_fuse = NVME_PRP | NVME_NOTFUSED;
	cmd->reserved = 0;
	cmd->metadata_ptr = 0;
	cmd->data_ptr.prp1 = get_physical_address(nsbuf);
	cmd->data_ptr.prp2 = 0;
	cmd->cdword_1x[0] = 0x0;		//CNS, namespace
	memset(&cmd->cdword_1x[1], 0, 5 * sizeof(uint32_t));
	uint16_t token = m_parent->m_adminCommandQueue->submit_entry(cmd);

	m_parent->m_adminCompletionQueue->wait_event(1000, NVME_ADMIN_QUEUE_ID, token, nullptr, nullptr);
	PNVME_NAMESPACE namesp = (PNVME_NAMESPACE)nsbuf;
	uint8_t lbaFormat = namesp->formattedLbaSize & 0xF;
	uint8_t lbads = namesp->lbaformats[lbaFormat].lbadataSize;

	uint64_t blocksize = (1 << lbads);

	kprintf(u"    Size: %d blocks\n", namesp->namespaceSize);
	kprintf(u"    Capacity: %d blocks\n", namesp->namespaceCapacity);
	kprintf(u"    Block size: %d\n", blocksize);
	kprintf(u"    Size: %x:%x B\n", namesp->namespaceBytesHigh, namesp->namespaceBytesLow);

	//REGISTER WITH VDS

	m_diskparams.diskSize = namesp->namespaceSize;
	m_diskparams.sectorSize = blocksize;
	GUID x = CHAIOS_VDS_DISK_NVME;
	m_diskparams.diskType = x;
	m_diskparams.diskId = *(GUID*)&namesp->NGUID;
	m_diskparams.parent = NULL;

	m_diskdriver.write_fn = &nvme_disk_write;
	m_diskdriver.read_fn = &nvme_disk_read;
	m_diskdriver.get_params = &nvme_disk_params;
	m_diskdriver.async_status = &nvme_complete_async;
	m_diskdriver.flush_buffers = &nvme_disk_flush;
	m_diskdriver.fn_param = this;
	RegisterVdsDisk(&m_diskdriver);

#if 0
	void* block0bufffer = new uint8_t[blocksize];
	read(0, 1, block0bufffer, nullptr);
	delete[] block0bufffer;
#endif
	delete[] nsbuf;
}


vds_err_t NVME::NvmeNamespace::issue_command(NVME_COMMAND_SET operation, lba_t blockaddr, vds_length_t count, void* __user buffer, semaphore_t* completionEvent)
{
	//kprintf(u"NVMe read: block %d, length %d\n", blockaddr, count);
	uint16_t subQueue = 1;
	auto cmd = m_parent->m_IoSubmissionQueues[subQueue - 1]->get_entry();
	cmd->opcode = operation;
	cmd->NSID = m_nsid;
	pcommand_memory pCmdMem = nullptr;

	NVME_DMA_DESCRIPTOR nvmeDmaDescriptor;

	if (buffer)
	{
		if (!nvmePrepareDma(buffer, count * m_diskparams.sectorSize, nvmeDmaDescriptor))
			return -1;

		if ((m_parent->m_SglSupport & NVME_COMMAND_SGLMASK) == NVME_COMMAND_NOSGLS)
		{
			cmd->prp_fuse = NVME_PRP | NVME_NOTFUSED;
			pCmdMem = create_prp_list(buffer, count * m_diskparams.sectorSize, cmd, nvmeDmaDescriptor);
			if (!pCmdMem)
				return -1;
		}
		else
		{
			//SGLs are more efficient to represent
			cmd->prp_fuse = NVME_SGL_OM | NVME_NOTFUSED;
			pCmdMem = create_sgl_list(buffer, count * m_diskparams.sectorSize, cmd, nvmeDmaDescriptor);
			if (!pCmdMem)
				return -1;
		}
	}
	else
	{
		//Command doesn't use buffers
		cmd->prp_fuse = 0;
		cmd->data_ptr.prp1 = 0;
		cmd->data_ptr.prp2 = 0;
	}
	cmd->reserved = 0;
	cmd->metadata_ptr = 0;
	//cmd->data_ptr.prp1 = get_physical_address(buffer);
	//cmd->data_ptr.prp2 = 0;

	//Starting LBA
	cmd->cdword_1x[0] = blockaddr & UINT32_MAX;
	cmd->cdword_1x[1] = (blockaddr >> 32);

	//Count, no protection info
	cmd->cdword_1x[2] = (count - 1);

	memset(&cmd->cdword_1x[3], 0, 3 * sizeof(uint32_t));

	auto token = m_parent->m_IoSubmissionQueues[subQueue - 1]->submit_entry(cmd);

	auto& completionQueue = m_parent->m_IoCompletionQueues[subQueue - 1];
	if (!completionEvent)
	{
		auto Completion = completionQueue->wait_event(1000, subQueue, token, pCmdMem, &nvmeDmaDescriptor);
		return Completion->status;
	}
	else
	{
		*completionEvent = completionQueue->async_event(subQueue, token, pCmdMem, &nvmeDmaDescriptor);
		return completionTag(subQueue, token);
	}
}

vds_err_t NVME::NvmeNamespace::read(lba_t blockaddr, vds_length_t count, void* __user buffer, semaphore_t* completionEvent)
{
	return issue_command(NVME_READ, blockaddr, count, buffer, completionEvent);
}

vds_err_t NVME::NvmeNamespace::write(lba_t blockaddr, vds_length_t count, void* __user buffer, semaphore_t* completionEvent)
{
	return issue_command(NVME_WRITE, blockaddr, count, buffer, completionEvent);
}

vds_err_t NVME::NvmeNamespace::flush(semaphore_t* completionEvent)
{
	return issue_command(NVME_FLUSH, 0, 0, 0, completionEvent);
}

vds_err_t NVME::NvmeNamespace::completeAsync(vds_err_t token, semaphore_t completionEvent)
{
	uint16_t subQueue, cmdId;
	breakCompletionTag(token, subQueue, cmdId);
	auto& ioQueue = m_parent->m_IoCompletionQueues[subQueue - 1];
	auto completion = ioQueue->get_completion(subQueue, cmdId);
	return completion->status;
}

vds_err_t nvme_disk_read(void* param, lba_t blockaddr, vds_length_t count, void* buffer, semaphore_t* completionEvent)
{
	NVME::NvmeNamespace* namesp = (NVME::NvmeNamespace*)param;
	return namesp->read(blockaddr, count, buffer, completionEvent);
}

vds_err_t nvme_disk_write(void* param, lba_t blockaddr, vds_length_t count, void* buffer, semaphore_t* completionEvent)
{
	NVME::NvmeNamespace* namesp = (NVME::NvmeNamespace*)param;
	return namesp->write(blockaddr, count, buffer, completionEvent);
}

vds_err_t nvme_disk_flush(void* param, semaphore_t* completionEvent)
{
	NVME::NvmeNamespace* namesp = (NVME::NvmeNamespace*)param;
	return namesp->flush(completionEvent);
}

vds_err_t nvme_complete_async(void* param, vds_err_t token, semaphore_t completionEvent)
{
	NVME::NvmeNamespace* namesp = (NVME::NvmeNamespace*)param;
	return namesp->completeAsync(token, completionEvent);
}


PCHAIOS_VDS_PARAMS nvme_disk_params(void* param)
{
	NVME::NvmeNamespace* namesp = (NVME::NvmeNamespace*)param;
	return &namesp->m_diskparams;
}