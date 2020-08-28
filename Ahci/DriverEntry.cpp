#include <chaikrnl.h>
#include <pciexpress.h>
#include <kstdio.h>
#include <string.h>
#include <endian.h>

#define AHCI_PCI_ABAR 5

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

uint8_t ahci_interrupt(size_t vector, void* param);

class AHCI {
public:
	AHCI(void* abar, size_t barsize, pci_address busaddr)
		:m_abar(abar), m_abarsize(barsize), m_busaddr(busaddr)
	{

	}

	void init()
	{
		take_bios_control();
		//reset_controller();
		uint32_t vect = PciAllocateMsi(m_busaddr.segment, m_busaddr.bus, m_busaddr.device, m_busaddr.function, 1, &ahci_interrupt, this);
		//AHCI aware
		write_ahci_register(GHC_GHC, read_ahci_register(GHC_GHC) | GHC_AHCI_ENABLE);

		m_64bitdma = ((read_ahci_register(GHC_CAP) & GHC_CAP_DMA64) == GHC_CAP_DMA64);

		//Now allocate buffers and such
		uint64_t num_ports = (read_ahci_register(GHC_CAP) & 0x1F) + 1;
		uint32_t ports = read_ahci_register(GHC_PI);
		//Safety net for ports implemented
		ports &= (((uint64_t)1 << num_ports) - 1);
		uint32_t index = 0;
		uint32_t curports = ports;
		//Stop all ports
		while (curports)
		{
			if ((curports & 1) != 0)
			{
				//Port implemented
				stop_port(index);
			}
			curports >>= 1;
			++index;
		}
		//Clear pending interrupts
		write_ahci_register(GHC_IS, UINT32_MAX);
		//Start interrupts and ports
		write_ahci_register(GHC_GHC, read_ahci_register(GHC_GHC) | GHC_INTERRUPT_ENABLE);
		index = 0;
		while (ports)
		{
			if ((ports & 1) != 0)
			{
				//Port implemented
				init_port(index);
			}
			ports >>= 1;
			++index;
		}
	}

protected:
	void init_port(uint32_t index)
	{
		//Clear error register
		uint32_t perrindex = PortIndex(index, Px_SERR);
		write_ahci_register(perrindex, read_ahci_register(perrindex));
		FlushPostedWrites(index);

		paddr_t pcmdlist = 0;
		const size_t CMD_LIST_LEN = 0x400;
		PHBA_CMD_HEADER port_command_list = (PHBA_CMD_HEADER)allocate_dma_buffer(CMD_LIST_LEN, pcmdlist);
		if (!port_command_list)
			return kprintf(u"Failed to allocate port command list\n");
		memset(port_command_list, 0, CMD_LIST_LEN);
		write_ahci_register(PortIndex(index, Px_CLB), pcmdlist);
		if(m_64bitdma)
			write_ahci_register(PortIndex(index, Px_CLBU), pcmdlist >> 32);

		//Allocate receieved FIS
		paddr_t prxfis = 0;
		PHBA_FIS port_rx_fis = (PHBA_FIS)allocate_dma_buffer(sizeof(PHBA_FIS), prxfis);
		if (!port_rx_fis)
			return kprintf(u"Failed to allocate port receieved FIS structure\n");

		write_ahci_register(PortIndex(index, Px_FB), prxfis);
		if (m_64bitdma)
			write_ahci_register(PortIndex(index, Px_FBU), prxfis >> 32);

		write_ahci_register(PortIndex(index, Px_CMD), read_ahci_register(PortIndex(index, Px_CMD)) | PX_CMD_FRE);		//Enable recieving FIS

		//Power up and spin up device
		write_ahci_register(PortIndex(index, Px_CMD), read_ahci_register(PortIndex(index, Px_CMD)) | PX_CMD_POD | PX_CMD_SUD);

		//Device detection
		write_ahci_register(PortIndex(index, Px_SCTL), PX_SCTL_DETECT | PX_SCTL_NOSPEEDLIM | PX_SCTL_PM_DISABLE);
		//Spin for over 1 millisecond
		auto time = arch_get_system_timer();
		while (arch_get_system_timer() < time + 2);

		write_ahci_register(PortIndex(index, Px_SCTL), PX_SCTL_NODETECT | PX_SCTL_NOSPEEDLIM | PX_SCTL_PM_DISABLE);

		//Allocate PRDT
		const size_t PRDT_ENTIRES = 16;
		PHBA_COMMAND_TABLE cmdtab = (PHBA_COMMAND_TABLE)create_command_table(&port_command_list[0], PRDT_ENTIRES);


		//Wait for the port to stop being busy
#if 0
		if (!timeout_check_reg_flags(PortIndex(index, Px_TFD), PX_TFD_BUSY | PX_TFD_DRQ, 0, 1000))
			return kprintf(u"Timed out waiting on port: status %x\n", read_ahci_register(PortIndex(index, Px_TFD)));
#endif

		uint32_t ssts = read_ahci_register(PortIndex(index, Px_SSTS));
		if ((ssts & 0xF) != HBA_PORT_DET_PRESENT)
		{
			return;
			//kprintf(u"Device not present on port %d, status %d\n", index, ssts & 0xF);
		}
		else if (((ssts >> 8) & 0xF) != HBA_PORT_IPM_ACTIVE)
		{
			return;
			//kprintf(u"Device not active on port %d, status %d\n", index, (ssts >> 8) & 0xF);
		}

		uint32_t portsig = read_ahci_register(PortIndex(index, Px_SIG));
		kprintf(u"%s drive on port %d (sig %x)\n", getReadableType(portsig), index, portsig);

		if (portsig == SATA_SIG_ATAPI)
			write_ahci_register(PortIndex(index, Px_CMD), read_ahci_register(PortIndex(index, Px_CMD)) | PX_CMD_ATAPI);
		else
			write_ahci_register(PortIndex(index, Px_CMD), read_ahci_register(PortIndex(index, Px_CMD)) & (~PX_CMD_ATAPI));
		FlushPostedWrites(index);

		//Start the port
		write_ahci_register(PortIndex(index, Px_CMD), read_ahci_register(PortIndex(index, Px_CMD)) | PX_CMD_START);
		//Enable all interrupts
		write_ahci_register(PortIndex(index, Px_IE), UINT32_MAX);

		//Identify device
		FIS_REG_H2D fis;
		memset(&fis, 0, sizeof(fis));
		fis.fis_type = FIS_TYPE_REG_H2D;
		fis.command = ATA_CMD_IDENTIFY;
		fis.device = 0;
		fis.ctl_byte = FIS_REG_H2D_CTRL_INTERRUPT;		//COMMAND

		paddr_t devidphy = 0;
		void* deviceid = allocate_dma_buffer(512, devidphy);
		if (!deviceid)
			return kprintf(u"Could not allocate device ID buffer\n");

		memcpy(cmdtab->cmdfis, &fis, sizeof(fis));
		cmdtab->prdt[0].DataBaseAddress = devidphy & UINT32_MAX;
		cmdtab->prdt[0].DBAU = (devidphy >> 32);
		cmdtab->prdt[0].DataByteCount = (512 - 1) | HBA_CMD_PRDT_DBC_INTERRUPT;

		//Issue the command!
		write_ahci_register(PortIndex(index, Px_CI), 1 << 0);

		auto tm = arch_get_system_timer();
		while (arch_get_system_timer() < tm + 5000)
		{
			if ((read_ahci_register(PortIndex(index, Px_CI)) & (1 << 0)) == 0)
				break;
		}
		if (arch_get_system_timer() >= tm + 5000)
			kprintf(u"Timed out IDENTIFY command\n");

		char* model = raw_offset<char*>(deviceid, 54);
		for (int i = 0; i < 20; ++i)
		{
			char tmp = model[i * 2];
			model[i * 2] = model[i * 2 + 1];
			model[i * 2 + 1] = tmp;
		}
		model[39] = 0;
		kprintf_a("Drive model: %s\n", model);

	}

	volatile void* create_command_table(volatile void* slot, size_t entries)
	{
		PHBA_CMD_HEADER hdr = (PHBA_CMD_HEADER)slot;
		
		paddr_t pcmdtable = 0;
		size_t bufsize = sizeof(PHBA_COMMAND_TABLE) + (entries - 1) * sizeof(PHBA_CMD_PRDT);
		PHBA_COMMAND_TABLE table = (PHBA_COMMAND_TABLE)allocate_dma_buffer(bufsize, pcmdtable);
		if (!table)
			return nullptr;

		memset(table, 0, bufsize);
		//Set up entry
		hdr->ctbau = (pcmdtable >> 32);
		hdr->ctba = pcmdtable & SIZE_MAX;
		hdr->prdtl = entries;
		hdr->prdbc = 0;

		return table;
	}

	static int getType(uint32_t sig)
	{
		switch (sig)
		{
		case SATA_SIG_ATAPI:
			return AHCI_DEV_SATAPI;
		case SATA_SIG_PM:
			return AHCI_DEV_PM;
		case SATA_SIG_SEMB:
			return AHCI_DEV_SEMB;
		case SATA_SIG_ATA:
			return AHCI_DEV_SATA;
		default:
			return AHCI_DEV_NULL;
		}
	}

	inline void FlushPostedWrites(uint32_t pindex)
	{
		uint32_t reg = PortIndex(pindex, Px_CMD);
		write_ahci_register(reg, read_ahci_register(reg));
	}


	static const char16_t* getReadableType(uint32_t sig)
	{
		static const char16_t* deviceTypes[] = { u"Unknown", u"SATA", u"SEMB", u"PM", u"SATAPI" };
		int tp = getType(sig);
		return deviceTypes[tp];
	}

	uint8_t interrupt(size_t vector)
	{
		kprintf(u"AHCI Interrupt\n");
		return 1;
	}
	friend uint8_t ahci_interrupt(size_t vector, void* param);

	enum GHC_REGISTERS {
		GHC_CAP = 0,
		GHC_GHC = 4,
		GHC_IS = 0x8,
		GHC_PI = 0xC,
		GHC_VS = 0x10,
		GHC_CCC_CTL = 0x14,
		GHC_CCC_PORTS = 0x18,
		GHC_EM_LOC = 0x1C,
		GHC_EM_CTL = 0x20,
		GHC_CAP2 = 0x24,
		GHC_BOHC = 0x28
	};

	enum GHC_CAP {
		GHC_CAP_NCQ = (1<<30),
		GHC_CAP_DMA64 = (1 << 31)
	};

	enum GHC_GHC {
		GHC_HBA_RESET = 1,
		GHC_INTERRUPT_ENABLE = 2,
		GHC_GHC_MRSM = 4,
		GHC_AHCI_ENABLE = (1<<31)
	};

	enum GHC_CAP2 {
		GHC_CAP2_BOH = 1
	};

	enum GHG_BOHC {
		GHC_BOHC_BOS = 1,
		GHC_BOHC_OOS = 2,
		GHC_BOHC_SMIE = 4,
		GHC_BOHC_OOC = 8,
		GHC_BOHC_BB = 0x10,
	};

	enum PORT_REGISTERS {
		Px_CLB = 0,
		Px_CLBU = 4,
		Px_FB = 8,
		Px_FBU = 0xC,
		Px_IS = 0x10,
		Px_IE = 0x14,
		Px_CMD = 0x18,
		Px_TFD = 0x20,
		Px_SIG = 0x24,
		Px_SSTS = 0x28,
		Px_SCTL = 0x2C,
		Px_SERR = 0x30,
		Px_SACT = 0x34,
		Px_CI = 0x38,
		Px_SNTF = 0x3C,
		Px_FBS = 0x40,
		Px_DEVSLP = 0x44
	};

	enum PX_CMD {
		PX_CMD_START = 1,
		PX_CMD_POD = 2,
		PX_CMD_SUD = 4,
		PX_CMD_FRE = (1<<4),
		PX_CMD_FR = (1<<14),
		PX_CMD_CR = (1<<15),
		PX_CMD_ATAPI = (1<<24)
	};

	enum PX_TFD {
		PX_TFD_ERR = 1,
		PX_TFD_DRQ = (1<<3),
		PX_TFD_BUSY = (1<<7)
	};

	enum PX_SCTL {
		PX_SCTL_NODETECT = 0x0,
		PX_SCTL_DETECT = 0x1,
		PX_SCTL_NOSPEEDLIM = 0x0,
		PX_SCTL_PM_DISABLE = (0x7 << 8)
	};

	enum FIS_TYPE
	{
		FIS_TYPE_REG_H2D = 0x27,	// Register FIS - host to device
		FIS_TYPE_REG_D2H = 0x34,	// Register FIS - device to host
		FIS_TYPE_DMA_ACT = 0x39,	// DMA activate FIS - device to host
		FIS_TYPE_DMA_SETUP = 0x41,	// DMA setup FIS - bidirectional
		FIS_TYPE_DATA = 0x46,	// Data FIS - bidirectional
		FIS_TYPE_BIST = 0x58,	// BIST activate FIS - bidirectional
		FIS_TYPE_PIO_SETUP = 0x5F,	// PIO setup FIS - device to host
		FIS_TYPE_DEV_BITS = 0xA1,	// Set device bits FIS - device to host
	};

#pragma pack(push, 1)
	typedef volatile struct _command_list_header {
		uint16_t ctrlword;
		uint16_t prdtl;
		uint32_t prdbc;
		uint32_t ctba;
		uint32_t ctbau;
		uint32_t reserved[4];
	}HBA_CMD_HEADER, *PHBA_CMD_HEADER;

	static_assert(sizeof(HBA_CMD_HEADER) == 0x20, "AHCI: Wrong struct size");

	typedef volatile struct tagFIS_REG_H2D
	{
		// DWORD 0
		uint8_t  fis_type;	// FIS_TYPE_REG_H2D

		uint8_t  ctl_byte;

		uint8_t  command;	// Command register
		uint8_t  featurel;	// Feature register, 7:0

		// DWORD 1
		uint8_t  lba0;		// LBA low register, 7:0
		uint8_t  lba1;		// LBA mid register, 15:8
		uint8_t  lba2;		// LBA high register, 23:16
		uint8_t  device;		// Device register

		// DWORD 2
		uint8_t  lba3;		// LBA register, 31:24
		uint8_t  lba4;		// LBA register, 39:32
		uint8_t  lba5;		// LBA register, 47:40
		uint8_t  featureh;	// Feature register, 15:8

		// DWORD 3
		uint8_t  countl;		// Count register, 7:0
		uint8_t  counth;		// Count register, 15:8
		uint8_t  icc;		// Isochronous command completion
		uint8_t  control;	// Control register

		// DWORD 4
		uint8_t  rsv1[4];	// Reserved
	} FIS_REG_H2D;
	static_assert(sizeof(FIS_REG_H2D) == 0x14, "AHCI: Wrong struct size");

	enum FIS_REG_H2D_CTRLBYTE {
		FIS_REG_H2D_CTRL_INTERRUPT = (1<<7)
	};

	typedef volatile struct tagFIS_REG_D2H
	{
		// DWORD 0
		uint8_t  fis_type;    // FIS_TYPE_REG_D2H

		uint8_t  ctl_byte;

		uint8_t  status;      // Status register
		uint8_t  error;       // Error register

		// DWORD 1
		uint8_t  lba0;        // LBA low register, 7:0
		uint8_t  lba1;        // LBA mid register, 15:8
		uint8_t  lba2;        // LBA high register, 23:16
		uint8_t  device;      // Device register

		// DWORD 2
		uint8_t  lba3;        // LBA register, 31:24
		uint8_t  lba4;        // LBA register, 39:32
		uint8_t  lba5;        // LBA register, 47:40
		uint8_t  rsv2;        // Reserved

		// DWORD 3
		uint8_t  countl;      // Count register, 7:0
		uint8_t  counth;      // Count register, 15:8
		uint8_t  rsv3[2];     // Reserved

		// DWORD 4
		uint8_t  rsv4[4];     // Reserved
	} FIS_REG_D2H;
	static_assert(sizeof(FIS_REG_D2H) == 0x14, "AHCI: Wrong struct size");

	typedef volatile struct tagFIS_DATA
	{
		// DWORD 0
		uint8_t  fis_type;	// FIS_TYPE_DATA

		uint8_t  pmport;

		uint8_t  rsv1[2];	// Reserved

		// DWORD 1 ~ N
		uint32_t data[1];	// Payload
	} FIS_DATA;
	static_assert(sizeof(FIS_DATA) == 0x8, "AHCI: Wrong struct size");		//Variable size, but we check packing here

	typedef volatile struct tagFIS_PIO_SETUP
	{
		// DWORD 0
		uint8_t  fis_type;	// FIS_TYPE_PIO_SETUP

		uint8_t  ctlbyte;

		uint8_t  status;		// Status register
		uint8_t  error;		// Error register

		// DWORD 1
		uint8_t  lba0;		// LBA low register, 7:0
		uint8_t  lba1;		// LBA mid register, 15:8
		uint8_t  lba2;		// LBA high register, 23:16
		uint8_t  device;		// Device register

		// DWORD 2
		uint8_t  lba3;		// LBA register, 31:24
		uint8_t  lba4;		// LBA register, 39:32
		uint8_t  lba5;		// LBA register, 47:40
		uint8_t  rsv2;		// Reserved

		// DWORD 3
		uint8_t  countl;		// Count register, 7:0
		uint8_t  counth;		// Count register, 15:8
		uint8_t  rsv3;		// Reserved
		uint8_t  e_status;	// New value of status register

		// DWORD 4
		uint16_t tc;		// Transfer count
		uint8_t  rsv4[2];	// Reserved
	} FIS_PIO_SETUP;
	static_assert(sizeof(FIS_PIO_SETUP) == 0x14, "AHCI: Wrong struct size");

	typedef volatile struct tagFIS_DMA_SETUP
	{
		// DWORD 0
		uint8_t  fis_type;	// FIS_TYPE_DMA_SETUP

		uint8_t  pmport : 4;	// Port multiplier
		uint8_t  rsv0 : 1;		// Reserved
		uint8_t  d : 1;		// Data transfer direction, 1 - device to host
		uint8_t  i : 1;		// Interrupt bit
		uint8_t  a : 1;            // Auto-activate. Specifies if DMA Activate FIS is needed

		uint8_t  rsved[2];       // Reserved

	//DWORD 1&2

		uint64_t DMAbufferID;    // DMA Buffer Identifier. Used to Identify DMA buffer in host memory. SATA Spec says host specific and not in Spec. Trying AHCI spec might work.

		//DWORD 3
		uint32_t rsvd;           //More reserved

		//DWORD 4
		uint32_t DMAbufOffset;   //Byte offset into buffer. First 2 bits must be 0

		//DWORD 5
		uint32_t TransferCount;  //Number of bytes to transfer. Bit 0 must be 0

		//DWORD 6
		uint32_t resvd;          //Reserved

	} FIS_DMA_SETUP;
	static_assert(sizeof(FIS_DMA_SETUP) == 0x1C, "AHCI: Wrong struct size");

	typedef volatile struct tagHBA_CMD_PRDT {
		uint32_t DataBaseAddress;
		uint32_t DBAU;
		uint32_t reserved;
		uint32_t DataByteCount;
	}HBA_CMD_PRDT, *PHBA_CMD_PRDT;
	static_assert(sizeof(HBA_CMD_PRDT) == 0x10, "AHCI: Wrong struct size");

	enum HBA_CMD_PRDT_DBC {
		HBA_CMD_PRDT_DBC_INTERRUPT = (1<<31)
	};

	typedef volatile struct tagHBA_COMMAND_TABLE {
		uint8_t cmdfis[0x40];
		uint8_t atapicmd[0x10];
		uint8_t reserved[0x30];
		HBA_CMD_PRDT prdt[1];
	}HBA_COMMAND_TABLE, *PHBA_COMMAND_TABLE;
	static_assert(sizeof(HBA_COMMAND_TABLE) == 0x90, "AHCI: Wrong struct size");		//Variable length, check packing

	typedef volatile struct tagHBA_FIS
	{
		// 0x00
		FIS_DMA_SETUP	dsfis;		// DMA Setup FIS
		uint8_t         pad0[4];

		// 0x20
		FIS_PIO_SETUP	psfis;		// PIO Setup FIS
		uint8_t         pad1[12];

		// 0x40
		FIS_REG_D2H	rfis;		// Register – Device to Host FIS
		uint8_t         pad2[4];

		// 0x58
		uint64_t		sdbfis;		// Set Device Bit FIS

		// 0x60
		uint8_t         ufis[64];

		// 0xA0
		uint8_t   	rsv[0x100 - 0xA0];
	} HBA_FIS, *PHBA_FIS;
	static_assert(sizeof(HBA_FIS) == 0x100, "AHCI: Wrong struct size");

#pragma pack(pop)

	inline uint32_t PortIndex(uint_fast8_t port, PORT_REGISTERS reg)
	{
		return 0x100 + (port * 0x80) + reg;
	}
	uint32_t read_ahci_register(uint32_t index)
	{
		return *raw_offset<volatile uint32_t*>(m_abar, index);
	}
	void write_ahci_register(uint32_t index, uint32_t value)
	{
		*raw_offset<volatile uint32_t*>(m_abar, index) = value;
	}

	void take_bios_control()
	{
		if ((read_ahci_register(GHC_CAP2) & GHC_CAP2_BOH) == 0)
			return;		//BOH not supported
		write_ahci_register(GHC_BOHC, read_ahci_register(GHC_BOHC) | GHC_BOHC_OOC);
		//Wait for control
		if (!timeout_check_reg_flags(GHC_BOHC, GHC_BOHC_BOS | GHC_BOHC_OOS, GHC_BOHC_OOS, 100))		//100 milliseconds timeout
			return kprintf(u"Failed to take control of AHCI from firmware\n");
		return kprintf(u"Took control of AHCI from firmware\n");

	}

	void reset_controller()
	{
		write_ahci_register(GHC_GHC, read_ahci_register(GHC_GHC) | GHC_HBA_RESET);
		if (!timeout_check_reg_flags(GHC_GHC, GHC_HBA_RESET, 0, 100))		//100 milliseconds timeout
			return kprintf(u"Failed to reset AHCI controller\n");
	}

	void stop_port(uint_fast8_t index)
	{
		uint32_t pxcmd = PortIndex(index, Px_CMD);
		write_ahci_register(pxcmd, read_ahci_register(pxcmd) & ~(PX_CMD_START | PX_CMD_FRE));		//Stop port

		if (!timeout_check_reg_flags(pxcmd, PX_CMD_CR | PX_CMD_FR, 0, 500))		//500 milliseconds timeout
			return kprintf(u"Failed to stop AHCI port %d\n", index);

		//Clear IS
		write_ahci_register(PortIndex(index, Px_IS), UINT32_MAX);

	}

	bool timeout_check_reg_flags(uint32_t index, uint32_t mask, uint32_t value, uint32_t timeout)
	{
		uint64_t count = arch_get_system_timer();
		while (arch_get_system_timer() < count + timeout)
		{
			if ((read_ahci_register(index) & mask) == value)
				return true;
		}
		return false;
	}

	void* allocate_dma_buffer(size_t length, paddr_t& phyaddr)
	{
		uint8_t memregion = ARCH_PHY_REGION_NORMAL;
		if (!m_64bitdma)
			memregion = ARCH_PHY_REGION_PCIDMA;
		phyaddr = pmmngr_allocate(DIV_ROUND_UP(length, PAGESIZE), memregion);
		if (phyaddr == NULL)
			return nullptr;
		void* alloc = find_free_paging(length);
		if (!paging_map(alloc, phyaddr, length, PAGE_ATTRIBUTE_NO_CACHING | PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_EXECUTE))
		{
			pmmngr_free(phyaddr, DIV_ROUND_UP(length, PAGESIZE));
			return nullptr;
		}
		return alloc;
	}
private:
	const pci_address m_busaddr;
	const void* m_abar;
	const size_t m_abarsize;
	bool m_64bitdma;
};

uint8_t ahci_interrupt(size_t vector, void* param)
{
	AHCI* ctrl = (AHCI*)param;
	return ctrl->interrupt(vector);
}

bool ahci_finder(uint16_t segment, uint16_t bus, uint16_t device, uint8_t function)
{
	//This is an AHCI device
	kprintf(u"AHCI found\n");
	size_t BARSIZE = 0;
	paddr_t pmmio = read_pci_bar(segment, bus, device, function, AHCI_PCI_ABAR, &BARSIZE);
	void* mappedabar = find_free_paging(BARSIZE);
	if (!paging_map(mappedabar, pmmio, BARSIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_EXECUTE | PAGE_ATTRIBUTE_NO_CACHING))
	{
		kprintf(u"Could not map AHCI MMIO\n");
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

	AHCI* controller = new AHCI(mappedabar, BARSIZE, addr);

	controller->init();
	return false;
}

static pci_device_declaration ahci_devices[] =
{
	{PCI_VENDOR_ANY, PCI_VENDOR_ANY, 0x01, 0x06, 0x01},
	PCI_DEVICE_END
};

static pci_device_registration ahci_pci =
{
	ahci_devices,
	ahci_finder
};

EXTERN int CppDriverEntry(void* param)
{
	//Find relevant devices
	register_pci_driver(&ahci_pci);
	//pci_bus_scan(&ahci_finder);
	return 0;
}