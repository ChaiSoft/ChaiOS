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

#define NVME_PCI_MBAR 0

#define	SATA_SIG_ATA	0x00000101	// SATA drive
#define	SATA_SIG_ATAPI	0xEB140101	// SATAPI drive
#define	SATA_SIG_SEMB	0xC33C0101	// Enclosure management bridge
#define	SATA_SIG_PM	0x96690101	// Port multiplier

#define AHCI_DEV_NULL 0
#define AHCI_DEV_SATA 1
#define AHCI_DEV_SEMB 2
#define AHCI_DEV_PM 3
#define AHCI_DEV_SATAPI 4

#define HBA_PORT_IPM_ACTIVE 1
#define HBA_PORT_DET_PRESENT 3

#define ATA_CMD_IDENTIFY 0xEC

#define kprintf(...)

uint8_t nvme_interrupt(size_t vector, void* param);
void nvme_event_thread(void* param);
vds_err_t nvme_disk_read(void* param, lba_t blockaddr, vds_length_t count, void* buffer, semaphore_t* completionEvent);
vds_err_t nvme_complete_async(void* param, vds_err_t token, semaphore_t completionEvent);
PCHAIOS_VDS_PARAMS nvme_disk_params(void* param);

class NVME {
public:
	NVME(void* abar, size_t barsize, pci_address busaddr)
		:m_mbar(abar), m_mbarsize(barsize), m_busaddr(busaddr)
	{
		m_countIoCompletion = 0;
		m_countIoSubmission = 0;
		m_completionReady = false;
	}

	void init()
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

		if(nvme_cap_mpsmin(capreg) > mps)	//Doesn't support system pages
			return kprintf(u"Error: NVMe does not support system page size\n");
		else if(nvme_cap_mpsmax(capreg) < mps)
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
		if(!timeout_check_reg_flags32(NVME_REG_CSTS, NVME_CSTS_RDY, NVME_CSTS_RDY, milliseconds))
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

protected:
	typedef struct _completion_info {
		uint32_t commandSpecific;
		uint16_t status;
		uint16_t SubmissionQueue;
		uint16_t CommandId;
	}COMPLETION_INFO, *PCOMPLETION_INFO;

	enum NVME_CONTROLLER_REGISTERS {
		NVME_REG_CAP = 0,
		NVME_REG_VS = 8,
		NVME_REG_INTMS = 0x0C,
		NVME_REG_INTMC = 0x10,
		NVME_REG_CC = 0x14,
		NVME_REG_CSTS = 0x1C,
		NVME_REG_NSSR = 0x20,
		NVME_REG_AQA = 0x24,
		NVME_REG_ASQ = 0x28,
		NVME_REG_ACQ = 0x30,
		NVME_REG_CMBLOC = 0x38,
		NVME_REG_CMBSZ = 0x3C,
		NVME_REG_BPINFO = 0x40,
		NVME_REG_BPRSEL = 0x44,
		NVME_REG_BPBML = 0x48
	};

	uint64_le read_rawnvme_reg64(NVME_CONTROLLER_REGISTERS reg)
	{
		return *raw_offset<volatile uint64_le*>(m_mbar, reg);
	}

	uint32_t read_nvme_reg32(NVME_CONTROLLER_REGISTERS reg)
	{
		return LE_TO_CPU32(*raw_offset<volatile uint32_le*>(m_mbar, reg));
	}
	uint64_t read_nvme_reg64(NVME_CONTROLLER_REGISTERS reg)
	{
		return LE_TO_CPU64(*raw_offset<volatile uint64_le*>(m_mbar, reg));
	}
	void write_nvme_reg32(NVME_CONTROLLER_REGISTERS reg, uint32_t value)
	{
		*raw_offset<volatile uint32_le*>(m_mbar, reg) = CPU_TO_LE32(value);
	}
	void write_nvme_reg64(NVME_CONTROLLER_REGISTERS reg, uint64_t value)
	{
		*raw_offset<volatile uint64_le*>(m_mbar, reg) = CPU_TO_LE64(value);
	}

	void* register_address(NVME_CONTROLLER_REGISTERS reg)
	{
		return raw_offset<void*>(m_mbar, reg);
	}

	void write_doorbell(bool completionqueue, uint16_t queue, uint16_t value)
	{
		size_t offset = 0x1000 + (2 * queue + (completionqueue ? 1 : 0)) * (4 << m_dstrd);
		*raw_offset<volatile uint32_le*>(m_mbar, offset) = CPU_TO_LE32(value);
	}

	void reset_controller()
	{
		uint32_t nvme_cc = read_nvme_reg32(NVME_REG_CC);
		nvme_cc = (nvme_cc & ~(NVME_CC_EN_MASK)) | NVME_CC_DISABLE;
		write_nvme_reg32(NVME_REG_CC, nvme_cc);
	}

	void* allocate_queue(size_t length, paddr_t& paddr)
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

	bool timeout_check_reg_flags32(NVME_CONTROLLER_REGISTERS index, uint32_t mask, uint32_t value, uint32_t timeout)
	{
		uint64_t count = arch_get_system_timer();
		while (arch_get_system_timer() < count + timeout)
		{
			if ((read_nvme_reg32(index) & mask) == value)
				return true;
		}
		return false;
	}

	friend uint8_t nvme_interrupt(size_t vector, void* param);
	friend void  nvme_event_thread(void* param);

	uint8_t interrupt(size_t vector)
	{
		signal_semaphore(m_InterruptSemaphore, 1);
		//kprintf_a("NVMe Interrupt\n");
		return 1;
	}

	void eventThread()
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

#pragma pack(push, 1)
	typedef volatile struct _nvme_cap_reg {
		uint16_le MQES;
		uint8_t CQR_AMS;
		uint8_t timeout;
		uint16_le DSTRD_BPS;
		uint8_t MPS;
		uint8_t reserved;
	}NVME_CAP, *PNVME_CAP;
	static_assert(sizeof(NVME_CAP) == 8, "Packing error: NVMe");

	typedef volatile struct _nvme_sgl {
		uint64_t address;
		uint32_t length;
		uint16_t high;
		uint8_t highb;
		uint8_t sglid;
	}NVME_SGL, *PNVME_SGL;

	typedef volatile struct _nvme_command {
		//CDWORD 0
		uint8_t opcode;
		uint8_t prp_fuse;
		uint16_le command_id;
		
		uint32_le NSID;
		uint64_t reserved;
		uint64_le metadata_ptr;

		union {
			struct {
				uint64_le prp1;
				uint64_le prp2;
			};
			NVME_SGL scatter_gather;
		}data_ptr;

		uint32_le cdword_1x[6];
	}NVME_COMMAND, *PNVME_COMMAND;
	static_assert(sizeof(NVME_COMMAND) == 64, "Packing error: NVMe");

	typedef volatile struct _nvme_completion {
		//CDWORD 0
		uint32_le CommandSpecific;
		uint32_le Reserved;
		uint16_le SQHead;
		uint16_le SQIdent;
		uint16_le CommandIdent;
		uint16_le Status;
	}NVME_COMPLETION, *PNVME_COMPLETION;
	static_assert(sizeof(NVME_COMPLETION) == 16, "Packing error: NVMe");

	typedef volatile struct _nvme_controller_identify {
		uint16_le VendorId;
		uint16_le SubsystemVendor;
		char SerialNumber[20];
		char ModelNumber[40];
		char FirmwareRevision[8];
		uint8_t RecommendedArbitration;
		uint8_t IEEE[3];
		uint8_t CMIC;
		uint8_t MaximumTransferSize;
		uint16_le ControllerId;
		uint32_le Ver;
		uint32_le RTD3R;
		uint32_le RTD3E;
		uint32_le OAES;
		uint32_le CTRATT;
		uint32_le Reserved[3];
		uint8_t FGUID[16];
		uint8_t Reserved2[112];
		uint8_t NVMMIS[16];

		uint8_t ADMINCONTCAP[256];

		uint8_t SQES;
		uint8_t CQES;
		uint16_le MAXCMD;
	}NVME_CONTROLLER, *PNVME_CONTROLLER;
	static_assert(sizeof(NVME_CONTROLLER) == 516, "Packing error: NVMe");

	typedef volatile struct _nvme_controller_lbaformat {
		uint16_le metadataSize;
		uint8_t lbadataSize;
		uint8_t relativePerformance;
	}NVME_LBA_FORMAT, *PNVME_LBA_FORMAT;

	typedef volatile struct _nvme_controller_namespace {
		uint64_le namespaceSize;
		uint64_le namespaceCapacity;
		uint64_le namespaceUtilisation;
		uint8_t features;
		uint8_t numLbaFormats;
		uint8_t formattedLbaSize;
		uint8_t metadataCapabilities;

		uint8_t blah[20];

		uint64_le namespaceBytesLow;
		uint64_le namespaceBytesHigh;

		uint8_t Reserved[40];

		GUID NGUID;
		uint64_le EUI64;

		NVME_LBA_FORMAT lbaformats[16];
	}NVME_NAMESPACE, *PNVME_NAMESPACE;
	static_assert(sizeof(NVME_NAMESPACE) == 192, "Packing error: NVMe");
#pragma pack(pop)

	static const uint8_t NVME_PRP = 0;
	static const uint8_t NVME_SGL_OM = (1 << 6);
	static const uint8_t NVME_SGL_METASGL = (2 << 6);
	static const uint8_t NVME_NOTFUSED = 0;
	static const uint8_t NVME_FIRSTFUSED = 1;
	static const uint8_t NVME_SECONDFUSED = 2;

	inline uint8_t nvme_cap_cqr(PNVME_CAP cap) { return (cap->CQR_AMS & 1); }
	inline uint8_t nvme_cap_ams(PNVME_CAP cap) { return ((cap->CQR_AMS >> 1) & 0x3); }

	inline uint8_t nvme_cap_dstrd(PNVME_CAP cap) { return (LE_TO_CPU16(cap->DSTRD_BPS) & 0xF); }
	inline uint8_t nvme_cap_nssrs(PNVME_CAP cap) { return ((LE_TO_CPU16(cap->DSTRD_BPS) >> 4) & 1); }
	inline uint8_t nvme_cap_css(PNVME_CAP cap) { return ((LE_TO_CPU16(cap->DSTRD_BPS) >> 5) & 0xFF); }
	inline uint8_t nvme_cap_bps(PNVME_CAP cap) { return ((LE_TO_CPU16(cap->DSTRD_BPS) >> 13) & 1); }

	inline uint8_t nvme_cap_mpsmin(PNVME_CAP cap) { return (cap->MPS & 0xF); }
	inline uint8_t nvme_cap_mpsmax(PNVME_CAP cap) { return ((cap->MPS >> 4) & 0xF); }

	static const uint32_t NVME_CC_DISABLE = 0;
	static const uint32_t NVME_CC_EN = 0x1;
	static const uint32_t NVME_CC_EN_MASK = 0x1;

	static const uint32_t NVME_CC_CSNVME = 0x0;
	static const uint32_t NVME_CC_CSS_MASK = 0x70;

	static const uint32_t NVME_CC_MPS_MASK = 0x780;
	static const uint32_t NVME_CC_MPS_SHIFT = 7;

	static const uint32_t NVME_CC_AMS_ROUNDROBIN = 0;
	static const uint32_t NVME_CC_AMS_WEIGHTED = (1<<11);
	static const uint32_t NVME_CC_AMS_MASK = (0x7 << 11);

	static const uint32_t NVME_CC_SHN_NONE = 0;
	static const uint32_t NVME_CC_SHN_NORMAL = (1<<14);
	static const uint32_t NVME_CC_SHN_ABRUPT = (2<<14);
	static const uint32_t NVME_CC_SHN_MASK = (0x3 << 14);

	static const uint32_t NVME_CC_IOSQES_MASK = (0xF << 16);
	static const uint32_t NVME_CC_IOSQES_SHIFT = 16;

	static const uint32_t NVME_CC_IOCQES_MASK = (0xF << 20);
	static const uint32_t NVME_CC_IOCQES_SHIFT = 20;

	static const uint32_t NVME_CSTS_RDY = 0x1;

	static const uint32_t NVME_ADMIN_QUEUE_ID = 0;

	enum NVME_ADMIN_COMMAND {
		DELETE_IO_SUBMISSION = 0,
		CREATE_IO_SUBMISSION = 1,
		GET_LOG_PAGE = 2,
		DELETE_IO_COMPLETION = 4,
		CREATE_IO_COMPLETION = 5,
		IDENTIFY = 6,
		ABORT = 8,
		SET_FEATURES = 9,
		GET_FEATURES = 0xA,
	};

	enum NVME_COMMAND_SET {
		NVME_FLUSH = 0,
		NVME_WRITE = 1,
		NVME_READ = 2
	};

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
		CommandQueue(NVME* parent, paddr_t& pqueue, size_t length, size_t queueid)
			:m_parent(parent), m_queuebase(parent->allocate_queue(length, m_addr)), m_entries(length / ENTRY_SIZE), m_queueid(queueid)
		{
			pqueue = m_addr;
			list_tail = 0;
			m_waiting_count = 0;
			m_cmd_id = 0;
		}

		PNVME_COMMAND get_entry()
		{
			size_t token = list_tail;
			++m_waiting_count;
			while(!arch_cas(&list_tail, token, next_after(token)))
				token = list_tail;
			return raw_offset<PNVME_COMMAND>(m_queuebase, token * sizeof(NVME_COMMAND));
		}

		uint16_t submit_entry(PNVME_COMMAND command)
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

		virtual bool is_valid()
		{
			return m_queuebase != nullptr;
		}

	private:
		size_t next_after(size_t val)
		{
			return (val + 1) % m_entries;
		}

		NVME* const m_parent;
		void* const m_queuebase;
		const size_t m_entries;
		const size_t m_queueid;

		static const size_t ENTRY_SIZE = 64;

		paddr_t m_addr;
		size_t m_waiting_count;

		volatile uint16_t m_cmd_id;
		volatile size_t list_tail;
	};

	class CompletionQueue : public NvmeQueue
	{
	public:
		CompletionQueue(NVME* parent, paddr_t& pqueue, size_t length, size_t queueid)
			:m_parent(parent), m_queuebase(parent->allocate_queue(length, m_addr)), m_entries(length / ENTRY_SIZE), m_queueid(queueid)
		{
			pqueue = m_addr;
			m_list_head = 0;
			m_flag = 1;
			m_waiting = 0;
			memset(m_queuebase, 0, length);
			m_treelock = create_spinlock();
		}

		void dispatch_events()
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

		semaphore_t async_event(uint16_t subqId, uint16_t cmdid)
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
			}
			else
			{
				inf = iter->second;
			}
			release_spinlock(m_treelock, st);
			return inf->wait_sem;
		}

		PCOMPLETION_INFO get_completion(uint16_t subqId, uint16_t cmdid)
		{
			uint32_t completionTag = NVME::completionTag(subqId, cmdid);

			auto st = acquire_spinlock(m_treelock);
			PWAITING_INFO inf = m_waiters[completionTag];
			PCOMPLETION_INFO result = inf->comp_info;
			delete_semaphore(inf->wait_sem);
			m_waiters.remove(completionTag);
			delete inf;
			release_spinlock(m_treelock, st);
			return result;
		}

		PCOMPLETION_INFO wait_event(uint64_t timeout, uint16_t subqId, uint16_t cmdid)
		{
			semaphore_t sem = async_event(subqId, cmdid);

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

		virtual bool is_valid()
		{
			return m_queuebase != nullptr;
		}

	private:
		typedef struct _waiting_info {
			semaphore_t wait_sem;
			PCOMPLETION_INFO comp_info;
		}WAITING_INFO, *PWAITING_INFO;

		size_t next_after(size_t val, size_t& flag)
		{
			val = (val + 1) % m_entries;
			flag = (val == 0 ? (!flag & 1) : flag);
			return val;
		}

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

public:
	class NvmeNamespace {
	public:
		NvmeNamespace(NVME* parent, uint32_t nsid)
			:m_parent(parent), m_nsid(nsid)
		{

		}

		void initialize()
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

			m_parent->m_adminCompletionQueue->wait_event(1000, NVME_ADMIN_QUEUE_ID, token);
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

			m_diskdriver.write_fn = nullptr;
			m_diskdriver.read_fn = &nvme_disk_read;
			m_diskdriver.get_params = &nvme_disk_params;
			m_diskdriver.async_status = &nvme_complete_async;
			m_diskdriver.fn_param = this;
			RegisterVdsDisk(&m_diskdriver);

#if 0
			void* block0bufffer = new uint8_t[blocksize];
			read(0, 1, block0bufffer, nullptr);
			delete[] block0bufffer;
#endif
			delete[] nsbuf;
		}

	protected:

		vds_err_t read(lba_t blockaddr, vds_length_t count, void* buffer, semaphore_t* completionEvent)
		{
			//kprintf(u"NVMe read: block %d, length %d\n", blockaddr, count);
			uint16_t subQueue = 1;
			auto cmd = m_parent->m_IoSubmissionQueues[subQueue - 1]->get_entry();
			cmd->opcode = NVME_READ;
			cmd->NSID = m_nsid;
			cmd->prp_fuse = NVME_PRP | NVME_NOTFUSED;
			cmd->reserved = 0;
			cmd->metadata_ptr = 0;
			cmd->data_ptr.prp1 = get_physical_address(buffer);
			cmd->data_ptr.prp2 = 0;

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
				auto Completion = completionQueue->wait_event(1000, subQueue, token);
				return Completion->status;
			}
			else
			{
				*completionEvent = completionQueue->async_event(subQueue, token);
				return completionTag(subQueue, token);
			}
		}

		vds_err_t completeAsync(vds_err_t token, semaphore_t completionEvent)
		{
			uint16_t subQueue, cmdId;
			breakCompletionTag(token, subQueue, cmdId);
			auto& ioQueue = m_parent->m_IoCompletionQueues[subQueue - 1];
			auto completion = ioQueue->get_completion(subQueue, cmdId);
			return completion->status;
		}

	private:
		NVME* const m_parent;
		const uint32_t m_nsid;
		CHAIOS_VDS_DISK m_diskdriver;
		CHAIOS_VDS_PARAMS m_diskparams;

		friend vds_err_t nvme_disk_read(void* param, lba_t blockaddr, vds_length_t count, void* buffer, semaphore_t* completionEvent);
		friend vds_err_t nvme_complete_async(void* param, vds_err_t token, semaphore_t completionEvent);
		friend PCHAIOS_VDS_PARAMS nvme_disk_params(void* param);
	};


private:
	CommandQueue* m_adminCommandQueue;
	CompletionQueue* m_adminCompletionQueue;

	CommandQueue** m_IoSubmissionQueues;
	CompletionQueue** m_IoCompletionQueues;
	size_t m_countIoSubmission;
	size_t m_countIoCompletion;
	bool m_completionReady;

	const void* m_mbar;
	const size_t m_mbarsize;
	const pci_address m_busaddr;

	uint_fast8_t m_dstrd;
	uint8_t m_maxTransferSize;

	semaphore_t m_InterruptSemaphore;
};

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

vds_err_t nvme_disk_read(void* param, lba_t blockaddr, vds_length_t count, void* buffer, semaphore_t* completionEvent)
{
	NVME::NvmeNamespace* namesp = (NVME::NvmeNamespace*)param;
	return namesp->read(blockaddr, count, buffer, completionEvent);
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

bool nvme_finder(uint16_t segment, uint16_t bus, uint16_t device, uint8_t function)
{
	//This is an NVME device
	kprintf(u"NVMe found\n");
	size_t BARSIZE = 0;
	paddr_t pmmio = read_pci_bar(segment, bus, device, function, NVME_PCI_MBAR, &BARSIZE);
	void* mappedmbar = find_free_paging(BARSIZE);
	if (!paging_map(mappedmbar, pmmio, BARSIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_EXECUTE | PAGE_ATTRIBUTE_NO_CACHING))
	{
		kprintf(u"Could not map NVMe MMIO\n");
		return false;
	}
	uint64_t commstat;
	read_pci_config(segment, bus, device, function, 1, 32, &commstat);
	commstat |= (1 << 10);	//Mask pinned interrupts
	commstat |= 0x6;		//Memory space and bus mastering
	write_pci_config(segment, bus, device, function, 1, 32, commstat);

	pci_address addr;
	addr.segment = segment;
	addr.bus = bus;
	addr.device = device;
	addr.function = function;

	NVME* controller = new NVME(mappedmbar, BARSIZE, addr);

	controller->init();
	return false;
}

static pci_device_declaration nvme_devices[] =
{
	{PCI_VENDOR_ANY, PCI_VENDOR_ANY, 0x01, 0x08, 0x02},
	PCI_DEVICE_END
};

static pci_device_registration nvme_pci =
{
	nvme_devices,
	nvme_finder
};

EXTERN int CppDriverEntry(void* param)
{
	//Find relevant devices
	register_pci_driver(&nvme_pci);
	//pci_bus_scan(&nvme_finder);
	return 0;
}