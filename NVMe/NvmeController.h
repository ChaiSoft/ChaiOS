#ifndef CHAIOS_NVME_NVMECONTROLLER_H
#define CHAIOS_NVME_NVMECONTROLLER_H

#include <chaikrnl.h>
#include <pciexpress.h>
#include <kstdio.h>
#include <string.h>
#include <endian.h>
#include <scheduler.h>
#include <multiprocessor.h>
#include <vds.h>
#include <guid.h>
#include <redblack.h>
#include "nvme_intdefs.h"

class NVME {
public:
	NVME(void* abar, size_t barsize, pci_address busaddr);
	void init();

protected:

	uint64_le read_rawnvme_reg64(NVME_CONTROLLER_REGISTERS reg);
	uint32_t read_nvme_reg32(NVME_CONTROLLER_REGISTERS reg);
	uint64_t read_nvme_reg64(NVME_CONTROLLER_REGISTERS reg);
	void write_nvme_reg32(NVME_CONTROLLER_REGISTERS reg, uint32_t value);
	void write_nvme_reg64(NVME_CONTROLLER_REGISTERS reg, uint64_t value);
	void* register_address(NVME_CONTROLLER_REGISTERS reg);
	void write_doorbell(bool completionqueue, uint16_t queue, uint16_t value);
	void reset_controller();

	void* allocate_queue(size_t length, paddr_t& paddr);
	void createAdminCommand(PNVME_COMMAND pCommand, NVME_ADMIN_COMMAND opcode, uint32_t namespaceId, paddr_t phyAddr, uint32_t cdword10, uint32_t cdword11);

	bool timeout_check_reg_flags32(NVME_CONTROLLER_REGISTERS index, uint32_t mask, uint32_t value, uint32_t timeout);

	friend uint8_t nvme_interrupt(size_t vector, void* param);
	friend void  nvme_event_thread(void* param);

	uint8_t interrupt(size_t vector);

	void eventThread();

	class NvmeQueue {
	public:
		virtual bool is_valid() { return false; }
	};

	static inline uint32_t completionTag(uint16_t subqId, uint16_t cmdid) { return ((uint32_t)subqId << 16) | cmdid; }
	static inline void breakCompletionTag(uint32_t tag, uint16_t& subqId, uint16_t& cmdid)
	{
		subqId = (tag >> 16) & UINT16_MAX;
		cmdid = tag & UINT16_MAX;
	}

	class CommandQueue : public NvmeQueue
	{
	public:
		CommandQueue(NVME* parent, paddr_t& pqueue, size_t length, size_t queueid, size_t completionId);

		PNVME_COMMAND get_entry();

		uint16_t submit_entry(PNVME_COMMAND command);

		size_t getCompletionQueueId();
		size_t getQueueId();

		virtual bool is_valid();

	private:
		size_t next_after(size_t val)
		{
			return (val + 1) % m_entries;
		}

		NVME* const m_parent;
		void* const m_queuebase;
		const size_t m_entries;
		const size_t m_queueid;
		const size_t m_completionId;

		static const size_t ENTRY_SIZE = 64;

		paddr_t m_addr;
		size_t m_waiting_count;

		volatile uint16_t m_cmd_id;
		volatile size_t list_tail;
	};

	class CompletionQueue : public NvmeQueue
	{
	public:
		CompletionQueue(NVME* parent, paddr_t& pqueue, size_t length, size_t queueid);

		void dispatch_events();
		semaphore_t async_event(uint16_t subqId, uint16_t cmdid, pcommand_memory allocatedMem, PNVME_DMA_DESCRIPTOR dmaDesc);
		PCOMPLETION_INFO get_completion(uint16_t subqId, uint16_t cmdid);
		PCOMPLETION_INFO wait_event(uint64_t timeout, uint16_t subqId, uint16_t cmdid, pcommand_memory allocatedMem = nullptr, PNVME_DMA_DESCRIPTOR dmaDesc = nullptr);
		virtual bool is_valid();

	private:

		typedef struct _waiting_info {
			semaphore_t wait_sem;
			PCOMPLETION_INFO comp_info;
			pcommand_memory commandAllocated;
			NVME_DMA_DESCRIPTOR dmaDescriptor;
		}WAITING_INFO, *PWAITING_INFO;

		size_t next_after(size_t val, size_t& flag);

		NVME* const m_parent;
		void* const m_queuebase;
		const size_t m_entries;
		const size_t m_queueid;

		static const size_t ENTRY_SIZE = 16;

		paddr_t m_addr;
		size_t m_flag;

		volatile size_t m_list_head;
		volatile size_t m_waiting;

		RedBlackTree<uint32_t, PWAITING_INFO> m_waiters;
		spinlock_t m_treelock;
	};

	inline bool queue_valid(NvmeQueue* queue)
	{
		if (!queue)
			return false;
		else
			return queue->is_valid();
	};

	CommandQueue* getIoSubmissionQueue();
	CompletionQueue* getIoCompletionQueue(CommandQueue* subQueue);

public:
	class NvmeNamespace {
	public:
		NvmeNamespace(NVME* parent, uint32_t nsid);
		void initialize();
	protected:
		vds_err_t read(lba_t blockaddr, vds_length_t count, void* __user buffer, semaphore_t* completionEvent);
		vds_err_t write(lba_t blockaddr, vds_length_t count, void* __user buffer, semaphore_t* completionEvent);
		vds_err_t flush(semaphore_t* completionEvent);
		vds_err_t completeAsync(vds_err_t token, semaphore_t completionEvent);

		vds_err_t issue_command(NVME_COMMAND_SET operation, lba_t blockaddr, vds_length_t count, void* __user buffer, semaphore_t* completionEvent);

	private:
		NVME* const m_parent;
		const uint32_t m_nsid;
		CHAIOS_VDS_DISK m_diskdriver;
		CHAIOS_VDS_PARAMS m_diskparams;

		friend vds_err_t nvme_disk_read(void* param, lba_t blockaddr, vds_length_t count, void* buffer, semaphore_t* completionEvent);
		friend vds_err_t nvme_disk_write(void* param, lba_t blockaddr, vds_length_t count, void* buffer, semaphore_t* completionEvent);
		friend vds_err_t nvme_disk_flush(void* param, semaphore_t* completionEvent);
		friend vds_err_t nvme_complete_async(void* param, vds_err_t token, semaphore_t completionEvent);
		friend PCHAIOS_VDS_PARAMS nvme_disk_params(void* param);
	};

	static void free_command_memory(pcommand_memory& memoryPtr);

	static void* createNvmeCommandBuffer(command_memory& AllocatedSegments, paddr_t& phyaddr);

	static bool nvmePrepareDma(void* __user buffer, size_t length, NVME_DMA_DESCRIPTOR& dmaDescriptor);

	static void nvmeFinishDma(NVME_DMA_DESCRIPTOR& dmaDescriptor);

	static PNVME_SGL createSegment(command_memory& AllocatedSegments, paddr_t& phyaddr)
	{
		return (PNVME_SGL)createNvmeCommandBuffer(AllocatedSegments, phyaddr);
	};

	static uint64_le* createPrpBuffer(command_memory& AllocatedBuffers, paddr_t& phyaddr)
	{
		return (uint64_le*)createNvmeCommandBuffer(AllocatedBuffers, phyaddr);
	};

	static pcommand_memory create_prp_list(void* __user buffer, size_t length, const PNVME_COMMAND command, NVME_DMA_DESCRIPTOR& dmaDescriptor);

	static pcommand_memory create_sgl_list(void* __user buffer, size_t length, const PNVME_COMMAND command, NVME_DMA_DESCRIPTOR& dmaDescriptor);

private:
	CommandQueue* m_adminCommandQueue;
	CompletionQueue* m_adminCompletionQueue;

	CommandQueue** m_IoSubmissionQueues;
	CompletionQueue** m_IoCompletionQueues;
	size_t m_countIoSubmission;
	size_t m_countIoCompletion;
	bool m_completionReady;

	size_t m_SglSupport;

	const void* m_mbar;
	const size_t m_mbarsize;
	const pci_address m_busaddr;

	uint_fast8_t m_dstrd;
	uint8_t m_maxTransferSize;

	semaphore_t m_InterruptSemaphore;
};

#endif
