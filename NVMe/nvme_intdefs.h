#ifndef CHAIOS_NVME_INTERNAL_DEFS_H
#define CHAIOS_NVME_INTERNAL_DEFS_H

#include <stdint.h>
#include <endian.h>

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

enum NVME_SGL_TYPE {
	SGL_DATA_BLOCK = 0,
	SGL_BIT_BUCKET = 1,
	SGL_SEGMENT = 2,
	SGL_LAST_SEGMENT = 3,
	SGL_KEYED = 4
};

enum NVME_SGL_SUBTYPE {
	SGL_ADDRESS = 0,
	SGL_OFFSET = 1
};

inline static uint8_t sglid(NVME_SGL_TYPE type, NVME_SGL_SUBTYPE subt)
{
	return ((type << 4) | subt);
}

typedef volatile struct _nvme_power_state_descriptor {
	uint8_t data[32];
}NVME_POWER_STATE_DESC, *PNVME_POWER_STATE_DESC;

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
	uint32_le NumNamespaces;
	uint16_le OptNvmeCommands;
	uint16_le FusedOperationSupport;
	uint8_t FormatNvmAttributes;
	uint8_t VolatileWriteCache;
	uint16_le AtomicWriteUnitNormal;
	uint16_le AtomicWriteUnitPowerFail;
	uint8_t VendorSpecificCommands;
	uint8_t Reserved3;
	uint16_le AtomicCompareWriteUnit;
	uint16_le Reserved4;
	uint32_le SglSupport;
	uint8_t Reserved5[228];
	char NvmeQualifiedName[256];
	uint8_t Reserved6[768];
	uint8_t NvmeOverFabrics[256];
	NVME_POWER_STATE_DESC PowerStates[32];
	uint8_t VendorSpecific[1024];
}NVME_CONTROLLER, *PNVME_CONTROLLER;
static_assert(sizeof(NVME_CONTROLLER) == 4096, "Packing error: NVMe");

enum NVME_SGL_SUPPORT {
	NVME_COMMAND_NOSGLS = 0,
	NVME_COMMAND_BYTESGLS = 1,
	NVME_COMMAND_DWORDSGLS = 2,
	NVME_COMMAND_SGLMASK = 0x3,
	NVME_COMMAND_KEYEDSGL = 0x4,
	NVME_COMMAND_SGLBITBUCKET = (1 << 16),
	NVME_COMMAND_BYTECONTIGMETADATA = (1 << 17),
	NVME_COMMAND_LONGSGLS = (1 << 18),
	NVME_COMMAND_METASGLS = (1 << 19),
	NVME_COMMAND_OFFSETSGLS = (1 << 20)
};

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
static const uint32_t NVME_CC_AMS_WEIGHTED = (1 << 11);
static const uint32_t NVME_CC_AMS_MASK = (0x7 << 11);

static const uint32_t NVME_CC_SHN_NONE = 0;
static const uint32_t NVME_CC_SHN_NORMAL = (1 << 14);
static const uint32_t NVME_CC_SHN_ABRUPT = (2 << 14);
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

typedef struct _nvme_dma_descriptor {
	PPAGING_PHYADDR_DESC physicalAddresses;
	size_t entryCount;
	void* __user buffer;
	size_t bufLength;
}NVME_DMA_DESCRIPTOR, *PNVME_DMA_DESCRIPTOR;

#include <redblack.h>
typedef RedBlackTree<paddr_t, void*> command_memory, *pcommand_memory;

#endif
