#include <chaikrnl.h>
#include <pciexpress.h>
#include <kstdio.h>
#include <string.h>
#include <endian.h>

#include <lwip/netifapi.h>
#include <lwip/etharp.h>
#include <lwip/ethip6.h>

/* Registers */
typedef enum
{
	E1000_REG_CTRL = 0x00000, /* Device Control - RW */
	E1000_REG_STATUS = 0x00008, /* Device Status - RO */
	E1000_REG_EECD = 0x00010, /* EEPROM/Flash Control - RW */
	E1000_REG_EERD = 0x00014, /* EEPROM Read - RW */
	E1000_REG_CTRL_EXT = 0x00018, /* Extended Device Control - RW */
	E1000_REG_FLA = 0x0001C, /* Flash Access - RW */
	E1000_REG_MDIC = 0x00020, /* MDI Control - RW */
	E1000_REG_SCTL = 0x00024, /* SerDes Control - RW */
	E1000_REG_FCAL = 0x00028, /* Flow Control Address Low - RW */
	E1000_REG_FCAH = 0x0002C, /* Flow Control Address High -RW */
	E1000_REG_FEXT = 0x0002C, /* Future Extended - RW */
	E1000_REG_FEXTNVM = 0x00028, /* Future Extended NVM - RW */
	E1000_REG_FEXTNVM3 = 0x0003C, /* Future Extended NVM 3 - RW */
	E1000_REG_FEXTNVM4 = 0x00024, /* Future Extended NVM 4 - RW */
	E1000_REG_FEXTNVM6 = 0x00010, /* Future Extended NVM 6 - RW */
	E1000_REG_FEXTNVM7 = 0x000E4, /* Future Extended NVM 7 - RW */
	E1000_REG_FEXTNVM9 = 0x5BB4, /* Future Extended NVM 9 - RW */
	E1000_REG_FEXTNVM11 = 0x5BBC, /* Future Extended NVM 11 - RW */
	E1000_REG_PCIEANACFG = 0x00F18, /* PCIE Analog Config */
	E1000_REG_FCT = 0x00030, /* Flow Control Type - RW */
	E1000_REG_VET = 0x00038, /* VLAN Ether Type - RW */
	E1000_REG_ICR = 0x000C0, /* Interrupt Cause Read - R/clr */
	E1000_REG_ITR = 0x000C4, /* Interrupt Throttling Rate - RW */
	E1000_REG_ICS = 0x000C8, /* Interrupt Cause Set - WO */
	E1000_REG_IMS = 0x000D0, /* Interrupt Mask Set - RW */
	E1000_REG_IMC = 0x000D8, /* Interrupt Mask Clear - WO */
	E1000_REG_IAM = 0x000E0, /* Interrupt Acknowledge Auto Mask */
	E1000_REG_IVAR = 0x000E4, /* Interrupt Vector Allocation Register - RW */
	E1000_REG_SVCR = 0x000F0,
	E1000_REG_SVT = 0x000F4,
	E1000_REG_LPIC = 0x000FC, /* Low Power IDLE control */
	E1000_REG_RCTL = 0x00100, /* Rx Control - RW */
	E1000_REG_FCTTV = 0x00170, /* Flow Control Transmit Timer Value - RW */
	E1000_REG_TXCW = 0x00178, /* Tx Configuration Word - RW */
	E1000_REG_RXCW = 0x00180, /* Rx Configuration Word - RO */
	E1000_REG_PBA_ECC = 0x01100, /* PBA ECC Register */
	E1000_REG_TCTL = 0x00400, /* Tx Control - RW */
	E1000_REG_TCTL_EXT = 0x00404, /* Extended Tx Control - RW */
	E1000_REG_TIPG = 0x00410, /* Tx Inter-packet gap -RW */
	E1000_REG_AIT = 0x00458, /* Adaptive Interframe Spacing Throttle - RW */
	E1000_REG_LEDCTL = 0x00E00, /* LED Control - RW */
	E1000_REG_LEDMUX = 0x08130, /* LED MUX Control */
	E1000_REG_EXTCNF_CTRL = 0x00F00, /* Extended Configuration Control */
	E1000_REG_EXTCNF_SIZE = 0x00F08, /* Extended Configuration Size */
	E1000_REG_PHY_CTRL = 0x00F10, /* PHY Control Register in CSR */
	E1000_REG_PBA = 0x01000, /* Packet Buffer Allocation - RW */
	E1000_REG_PBS = 0x01008, /* Packet Buffer Size */
	E1000_REG_PBECCSTS = 0x0100C, /* Packet Buffer ECC Status - RW */
	E1000_REG_IOSFPC = 0x00F28, /* TX corrupted data  */
	E1000_REG_EEMNGCTL = 0x01010, /* MNG EEprom Control */
	E1000_REG_EEWR = 0x0102C, /* EEPROM Write Register - RW */
	E1000_REG_FLOP = 0x0103C, /* FLASH Opcode Register */
	E1000_REG_ERT = 0x02008, /* Early Rx Threshold - RW */
	E1000_REG_FCRTL = 0x02160, /* Flow Control Receive Threshold Low - RW */
	E1000_REG_FCRTH = 0x02168, /* Flow Control Receive Threshold High - RW */
	E1000_REG_PSRCTL = 0x02170, /* Packet Split Receive Control - RW */
	E1000_REG_RDFH = 0x02410, /* Rx Data FIFO Head - RW */
	E1000_REG_RDFT = 0x02418, /* Rx Data FIFO Tail - RW */
	E1000_REG_RDFHS = 0x02420, /* Rx Data FIFO Head Saved - RW */
	E1000_REG_RDFTS = 0x02428, /* Rx Data FIFO Tail Saved - RW */
	E1000_REG_RDFPC = 0x02430, /* Rx Data FIFO Packet Count - RW */
	E1000_REG_RDTR = 0x02820, /* Rx Delay Timer - RW */
	E1000_REG_RXDCTL = 0x02828,	/* Receive Descriptor Control - RW */
	E1000_REG_RADV = 0x0282C, /* Rx Interrupt Absolute Delay Timer - RW */

	E1000_REG_RAL = 0x05400, // Receive Address Low
	E1000_REG_RAH = 0x05404, // Receive Address High
	E1000_REG_RDBAL = 0x02800, // RX Descriptor Base Address Low
	E1000_REG_RDBAH = 0x02804, // RX Descriptor Base Address High
	E1000_REG_RDLEN = 0x02808, // RX Descriptor Length
	E1000_REG_RDH = 0x02810, // RX Descriptor Head
	E1000_REG_RDT = 0x02818, // RX Descriptor Tail
	E1000_REG_TDBAL = 0x03800, // TX Descriptor Base Address Low
	E1000_REG_TDBAH = 0x03804, // TX Descriptor Base Address High
	E1000_REG_TDLEN = 0x03808, // TX Descriptor Length
	E1000_REG_TDH = 0x03810, // TX Descriptor Head
	E1000_REG_TDT = 0x03818, // TX Descriptor Tail

	E1000_REG_MTA = 0x05200
} e1000_register_t;

/* Device Control */
typedef enum
{
	E1000_CTRL_FD = 0x00000001, /* Full duplex.0=half; 1=full */
	E1000_CTRL_GIO_MASTER_DISABLE = 0x00000004, /* Blocks new Master reqs */
	E1000_CTRL_LRST = 0x00000008, /* Link reset. 0=normal,1=reset */
	E1000_CTRL_ASDE = 0x00000020, /* Auto-speed detect enable */
	E1000_CTRL_SLU = 0x00000040, /* Set link up (Force Link) */
	E1000_CTRL_ILOS = 0x00000080, /* Invert Loss-Of Signal */
	E1000_CTRL_SPD_SEL = 0x00000300, /* Speed Select Mask */
	E1000_CTRL_SPD_10 = 0x00000000, /* Force 10Mb */
	E1000_CTRL_SPD_100 = 0x00000100, /* Force 100Mb */
	E1000_CTRL_SPD_1000 = 0x00000200, /* Force 1Gb */
	E1000_CTRL_FRCSPD = 0x00000800, /* Force Speed */
	E1000_CTRL_FRCDPX = 0x00001000, /* Force Duplex */
	E1000_CTRL_LANPHYPC_OVERRIDE = 0x00010000, /* SW control of LANPHYPC */
	E1000_CTRL_LANPHYPC_VALUE = 0x00020000, /* SW value of LANPHYPC */
	E1000_CTRL_MEHE = 0x00080000, /* Memory Error Handling Enable */
	E1000_CTRL_SWDPIN0 = 0x00040000, /* SWDPIN 0 value */
	E1000_CTRL_SWDPIN1 = 0x00080000, /* SWDPIN 1 value */
	E1000_CTRL_ADVD3WUC = 0x00100000, /* D3 WUC */
	E1000_CTRL_EN_PHY_PWR_MGMT = 0x00200000, /* PHY PM enable */
	E1000_CTRL_SWDPIO0 = 0x00400000, /* SWDPIN 0 Input or output */
	E1000_CTRL_RST = 0x04000000, /* Global reset */
	E1000_CTRL_RFCE = 0x08000000, /* Receive Flow Control enable */
	E1000_CTRL_TFCE = 0x10000000, /* Transmit flow control enable */
	E1000_CTRL_VME = 0x40000000, /* IEEE VLAN mode enable */
	E1000_CTRL_PHY_RST = 0x80000000, /* PHY Reset */
} e1000_ctrl_flags_t;

/* Extended Device Control */
typedef enum
{
	E1000_CTRL_EXT_LPCD = 0x00000004, /* LCD Power Cycle Done */
	E1000_CTRL_EXT_SDP3_DATA = 0x00000080, /* SW Definable Pin 3 data */
	E1000_CTRL_EXT_FORCE_SMBUS = 0x00000800, /* Force SMBus mode */
	E1000_CTRL_EXT_EE_RST = 0x00002000, /* Reinitialize from EEPROM */
	E1000_CTRL_EXT_SPD_BYPS = 0x00008000, /* Speed Select Bypass */
	E1000_CTRL_EXT_RO_DIS = 0x00020000, /* Relaxed Ordering disable */
	E1000_CTRL_EXT_DMA_DYN_CLK_EN = 0x00080000, /* DMA Dynamic Clk Gating */
	E1000_CTRL_EXT_LINK_MODE_MASK = 0x00C00000,
	E1000_CTRL_EXT_LINK_MODE_PCIE_SERDES = 0x00C00000,
	E1000_CTRL_EXT_EIAME = 0x01000000,
	E1000_CTRL_EXT_DRV_LOAD = 0x10000000, /* Drv loaded bit for FW */
	E1000_CTRL_EXT_IAME = 0x08000000, /* Int ACK Auto-mask */
	E1000_CTRL_EXT_PBA_CLR = 0x80000000, /* PBA Clear */
	E1000_CTRL_EXT_LSECCK = 0x00001000,
	E1000_CTRL_EXT_PHYPDEN = 0x00100000,
} e1000_ctrl_ext_flags_t;

/* Device Status */
typedef enum
{
	E1000_STATUS_FD = 0x00000001, /* Duplex 0=half 1=full */
	E1000_STATUS_LU = 0x00000002, /* Link up.0=no,1=link */
	E1000_STATUS_FUNC_MASK = 0x0000000C, /* PCI Function Mask */
	E1000_STATUS_FUNC_SHIFT = 2,
	E1000_STATUS_FUNC_1 = 0x00000004, /* Function 1 */
	E1000_STATUS_TXOFF = 0x00000010, /* transmission paused */
	E1000_STATUS_SPEED_MASK = 0x000000C0,
	E1000_STATUS_SPEED_10 = 0x00000000, /* Speed 10Mb/s */
	E1000_STATUS_SPEED_100 = 0x00000040, /* Speed 100Mb/s */
	E1000_STATUS_SPEED_1000 = 0x00000080, /* Speed 1000Mb/s */
	E1000_STATUS_LAN_INIT_DONE = 0x00000200, /* Lan Init Compltn by NVM */
	E1000_STATUS_PHYRA = 0x00000400, /* PHY Reset Asserted */
	E1000_STATUS_GIO_MASTER_ENABLE = 0x00080000, /* Master request status */
	E1000_STATUS_2P5_SKU = 0x00001000, /* Val of 2.5GBE SKU strap */
	E1000_STATUS_2P5_SKU_OVER = 0x00002000, /* Val of 2.5GBE SKU Over */
} e1000_device_status_flags_t;

/* Receive Control */
typedef enum
{
	E1000_RCTL_EN = 0x00000002, /* enable */
	E1000_RCTL_SBP = 0x00000004, /* store bad packet */
	E1000_RCTL_UPE = 0x00000008, /* unicast promisc enable */
	E1000_RCTL_MPE = 0x00000010, /* multicast promisc enable */
	E1000_RCTL_LPE = 0x00000020, /* long packet enable */
	E1000_RCTL_LBM_NO = 0x00000000, /* no loopback mode */
	E1000_RCTL_LBM_MAC = 0x00000040, /* MAC loopback mode */
	E1000_RCTL_LBM_TCVR = 0x000000C0, /* tcvr loopback mode */
	E1000_RCTL_DTYP_PS = 0x00000400, /* Packet Split descriptor */
	E1000_RCTL_RDMTS_HALF = 0x00000000, /* Rx desc min thresh size */
	E1000_RCTL_RDMTS_HEX = 0x00010000,
	E1000_RCTL_RDMTS1_HEX = E1000_RCTL_RDMTS_HEX,
	E1000_RCTL_MO_SHIFT = 12, /* multicast offset shift */
	E1000_RCTL_MO_3 = 0x00003000, /* multicast offset 15:4 */
	E1000_RCTL_BAM = 0x00008000, /* broadcast enable */
	/* these buffer sizes are valid if E1000_RCTL_BSEX is 0 */
	E1000_RCTL_SZ_2048 = 0x00000000, /* Rx buffer size 2048 */
	E1000_RCTL_SZ_1024 = 0x00010000, /* Rx buffer size 1024 */
	E1000_RCTL_SZ_512 = 0x00020000, /* Rx buffer size 512 */
	E1000_RCTL_SZ_256 = 0x00030000, /* Rx buffer size 256 */
	/* these buffer sizes are valid if E1000_RCTL_BSEX is 1 */
	E1000_RCTL_SZ_16384 = 0x00010000, /* Rx buffer size 16384 */
	E1000_RCTL_SZ_8192 = 0x00020000, /* Rx buffer size 8192 */
	E1000_RCTL_SZ_4096 = 0x00030000, /* Rx buffer size 4096 */
	E1000_RCTL_VFE = 0x00040000, /* vlan filter enable */
	E1000_RCTL_CFIEN = 0x00080000, /* canonical form enable */
	E1000_RCTL_CFI = 0x00100000, /* canonical form indicator */
	E1000_RCTL_DPF = 0x00400000, /* discard pause frames */
	E1000_RCTL_PMCF = 0x00800000, /* pass MAC control frames */
	E1000_RCTL_BSEX = 0x02000000, /* Buffer size extension */
	E1000_RCTL_SECRC = 0x04000000, /* Strip Ethernet CRC */
} e1000_rx_ctrl_flags_t;

/* Transmit Control */
typedef enum
{
	E1000_TCTL_EN = 0x00000002, /* enable Tx */
	E1000_TCTL_PSP = 0x00000008, /* pad short packets */
	E1000_TCTL_CT = 0x00000ff0, /* collision threshold */
	E1000_TCTL_COLD = 0x003ff000, /* collision distance */
	E1000_TCTL_RTLC = 0x01000000, /* Re-transmit on late collision */
	E1000_TCTL_MULR = 0x10000000, /* Multiple request support */
} e1000_tx_ctrl_flags_t;

/* Interrupt Cause Read */
typedef enum
{
	E1000_ICR_TXDW = 0x00000001, /* Transmit desc written back */
	E1000_ICR_LSC = 0x00000004, /* Link Status Change */
	E1000_ICR_RXSEQ = 0x00000008, /* Rx sequence error */
	E1000_ICR_RXDMT0 = 0x00000010, /* Rx desc min. threshold (0) */
	E1000_ICR_RXT0 = 0x00000080, /* Rx timer intr (ring 0) */
	E1000_ICR_ECCER = 0x00400000, /* Uncorrectable ECC Error */
	E1000_ICR_INT_ASSERTED = 0x80000000, /* If this bit asserted, the driver should claim the interrupt */
	E1000_ICR_RXQ0 = 0x00100000, /* Rx Queue 0 Interrupt */
	E1000_ICR_RXQ1 = 0x00200000, /* Rx Queue 1 Interrupt */
	E1000_ICR_TXQ0 = 0x00400000, /* Tx Queue 0 Interrupt */
	E1000_ICR_TXQ1 = 0x00800000, /* Tx Queue 1 Interrupt */
	E1000_ICR_OTHER = 0x01000000, /* Other Interrupts */
} e1000_intr_cause_flags_t;

/* Interrupt Mask Set */
typedef enum
{
	E1000_IMS_TXDW = E1000_ICR_TXDW, /* Tx desc written back */
	E1000_IMS_LSC = E1000_ICR_LSC, /* Link Status Change */
	E1000_IMS_RXSEQ = E1000_ICR_RXSEQ, /* Rx sequence error */
	E1000_IMS_RXDMT0 = E1000_ICR_RXDMT0, /* Rx desc min. threshold */
	E1000_IMS_RXT0 = E1000_ICR_RXT0, /* Rx timer intr */
	E1000_IMS_ECCER = E1000_ICR_ECCER, /* Uncorrectable ECC Error */
	E1000_IMS_RXQ0 = E1000_ICR_RXQ0, /* Rx Queue 0 Interrupt */
	E1000_IMS_RXQ1 = E1000_ICR_RXQ1, /* Rx Queue 1 Interrupt */
	E1000_IMS_TXQ0 = E1000_ICR_TXQ0, /* Tx Queue 0 Interrupt */
	E1000_IMS_TXQ1 = E1000_ICR_TXQ1, /* Tx Queue 1 Interrupt */
	E1000_IMS_OTHER = E1000_ICR_OTHER, /* Other Interrupt */
} e1000_intr_mask_flags_t;

// Structure of transmit descriptors.
#pragma pack(push, 1)
typedef struct
{
	// Physical address of the transmit descriptor packet buffer.
	uint64_t address;

	// Length of the packet part to transmit.
	uint16_t length;

	// Check sum offset.
	uint8_t cso;

	// Command field.
	uint8_t command;

	// Packet transmission status.
	uint8_t status;

	// Checksum start field.
	uint8_t css;

	// Unused.
	uint16_t special;
} tx_desc_t;

typedef struct {
	volatile uint64_t addr;
	volatile uint16_t length;
	volatile uint16_t checksum;
	volatile uint8_t status;
	volatile uint8_t errors;
	volatile uint16_t special;
}rx_desc_t;
#pragma pack(pop)

pci_device_declaration i219_devs[] =
{
	{0x8086, 0x10D3, PCI_CLASS_ANY},
	{0x8086, 0x15BC, PCI_CLASS_ANY},
	PCI_DEVICE_END
};

paddr_t pmmngr_allloc_contig(size_t numpages)
{
	if (numpages == 0)
		return 0;
	paddr_t phy_addr = pmmngr_allocate(1);
	for (int i = 1; i < numpages; ++i)
	{
		paddr_t alloc = pmmngr_allocate(1);
		if (alloc != phy_addr + i * PAGESIZE)
		{
			kprintf(u"Contiguity failure: %x -> %x, iteration %d\n", phy_addr, alloc, i);
			pmmngr_free(alloc, 1);
			pmmngr_free(phy_addr, i);
			return 0;
		}
	}
	return phy_addr;
}

static bool i219_allocate_hardware_buffer(void** mapped_address, paddr_t& phy_addr, size_t objectlength, size_t count)
{
	size_t numpages = DIV_ROUND_UP(objectlength * count, PAGESIZE);
	phy_addr = pmmngr_allloc_contig(numpages);
	if (!phy_addr)
		return false;
	*mapped_address = find_free_paging(numpages * PAGESIZE);
	if (!paging_map(*mapped_address, phy_addr, numpages * PAGESIZE, PAGE_ATTRIBUTE_NO_CACHING | PAGE_ATTRIBUTE_WRITABLE))
		return false;
	return true;
}

class I219Registers {
public:
	I219Registers(void* mappedio)
		:mappedmem(mappedio)
	{

	}

	uint32_t read(e1000_register_t reg)
	{
		return *raw_offset<volatile uint32_t*>(mappedmem, reg);
	}

	void write(e1000_register_t reg, uint32_t value)
	{
		*raw_offset<volatile uint32_t*>(mappedmem, reg) = value;
	}

private:
	const void* mappedmem;
};

struct i219_driver_info {
	void* mapped_controller;
	pci_address address;
	netif* netif;
	tx_desc_t* maptxdescs;
	void* maptxbuf;
	size_t TX_BUFFER_SIZE;
	size_t TX_DESC_COUNT;
	size_t txTail = 0;
	rx_desc_t* maprxdescs;
	void* maprxbuf;
	paddr_t rx_buf_phy;
	size_t RX_BUFFER_SIZE;
	size_t RX_DESC_COUNT;
	uint32_t rxCur = 0;
};

static uint8_t ethernet_interrupt(size_t vector, void* param)
{
	i219_driver_info* dinfo = (i219_driver_info*)param;
	I219Registers devregs(dinfo->mapped_controller);
	uint32_t status = devregs.read(E1000_REG_ICR);
	if (status & E1000_ICR_LSC)
	{
		uint32_t devstat = devregs.read(E1000_REG_STATUS);
		uint8_t up = devstat & (1 << 1);
		//kprintf(u"Link status interrupt fired: active %d\n", up);
		if (up)
		{
			netifapi_netif_set_link_up_async(dinfo->netif);
		}
		else
		{
			netifapi_netif_set_link_down_async(dinfo->netif);
		}
	}
	if (status & E1000_ICR_RXT0)
	{
		//kprintf(u"Packet interrupt: RX %d\n", dinfo->rxCur);
		while (dinfo->maprxdescs[dinfo->rxCur].status != 0)
		{
			uint8_t *buf = raw_offset<uint8_t*>(dinfo->maprxbuf, dinfo->maprxdescs[dinfo->rxCur].addr - dinfo->rx_buf_phy);
			uint16_t len = dinfo->maprxdescs[dinfo->rxCur].length;

			uint16_be* type = raw_offset<uint16_be*>(buf, 12);
			uint16_t tp = BE_TO_CPU16((*type));
			if (tp == 0x88A8)
				type = raw_offset<uint16_be*>(type, 8);
			else if(tp == 0x8100)
				type = raw_offset<uint16_be*>(type, 4);
			tp = BE_TO_CPU16((*type));
			//kprintf(u" Received a packet: type %x, length %d, status %d\n", tp, len, dinfo->maprxdescs[dinfo->rxCur].status);

			// Here you should inject the received packet into your network stack
			pbuf* p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
			memcpy(p->payload, buf, len);		//We release the device buffer
			p->if_idx = dinfo->netif->num;
			dinfo->netif->input(p, dinfo->netif);


			dinfo->maprxdescs[dinfo->rxCur].status = 0;
			auto old_cur = dinfo->rxCur;
			dinfo->rxCur = (dinfo->rxCur + 1) % dinfo->RX_DESC_COUNT;
			devregs.write(E1000_REG_RDT, old_cur);		//TODO: this is probably wrong
		}
	}
#if 0
	else
	{
		kprintf(u"Unknown ethernet interrupt: status %x\n", status);
	}
#endif
	devregs.write(E1000_REG_ICR, status);		//Interrupt handled
	return 1;
}

#pragma pack(push, 1)
struct arp_packet {
	uint16_be hw_type;
	uint16_be prot_type;
	uint8_t hw_addrlen;
	uint8_t pr_addrlen;
	uint16_be operation;
	uint8_t sender_hw_address[6];
	uint8_t sender_protaddr[4];
	uint8_t target_hw_address[6];
	uint8_t target_protaddr[4];
};
static_assert(sizeof(arp_packet) == 28, "bad ARP packet size");

template <size_t n> struct ethernet_frame {
	uint8_t mac_destination[6];
	uint8_t mac_source[6];
	uint16_be size_type;
	uint8_t payload[n];
	uint32_be crc;
};
#pragma pack(pop, 1)

err_t i219_tx(struct netif *netif, struct pbuf *p)
{
	uint16_be* type = raw_offset<uint16_be*>(p->payload, 12);
	uint16_t tp = BE_TO_CPU16((*type));
	if (tp == 0x88A8)
		type = raw_offset<uint16_be*>(type, 8);
	else if (tp == 0x8100)
		type = raw_offset<uint16_be*>(type, 4);
	tp = BE_TO_CPU16((*type));
	auto dinfo = (i219_driver_info*)netif->state;
	void* mapped_controller = dinfo->mapped_controller;
	I219Registers devregs(mapped_controller);

	size_t txTail = dinfo->txTail;
	size_t next = txTail + 1;
	if (next == dinfo->TX_DESC_COUNT)
		next = 0;

	//kprintf(u"Network stack tx: length %d, type %x, slot %d[n:%d]\n", p->len, tp, txTail, next);
	dinfo->txTail = next;

	tx_desc_t* descriptor = &dinfo->maptxdescs[txTail];

	if (descriptor->length)
		while (!(descriptor->status & 0xF)) // Catches "descriptor done" (DD) and various errors
			arch_pause();
	//Send
	memcpy(raw_offset<void*>(dinfo->maptxbuf, dinfo->TX_BUFFER_SIZE * txTail), p->payload, p->len);
	descriptor->length = p->len;
	descriptor->command = 0x8 | 0x2 | 0x1;
	descriptor->cso = 0;
	descriptor->css = 0;
	descriptor->status = 0;
	descriptor->special = 0;

	devregs.write(E1000_REG_TDT, next);

	return ERR_OK;
}

EXTERN CHAIKRNL_FUNC void test_netif(netif* ptr);

err_t i219_init(struct netif *netif)
{
	u8_t i;

	i219_driver_info* dinfo = (i219_driver_info*)netif->state;

	I219Registers devregs(dinfo->mapped_controller);

	unsigned char macAddress[6];
	uint32_t macLow = devregs.read(E1000_REG_RAL);
	if (macLow != 0x00000000)
	{
		// MAC can be read from RAL[0]/RAH[0] MMIO directly
		macAddress[0] = macLow & 0xFF;
		macAddress[1] = (macLow >> 8) & 0xFF;
		macAddress[2] = (macLow >> 16) & 0xFF;
		macAddress[3] = (macLow >> 24) & 0xFF;
		uint32_t macHigh = devregs.read(E1000_REG_RAH);
		macAddress[4] = macHigh & 0xFF;
		macAddress[5] = (macHigh >> 8) & 0xFF;
	}
	else
	{
		kprintf(u"Could not read MAC from MMIO\n");
		return ERR_ARG;
	}
	kprintf_a("MAC Address: %x:%x:%x:%x:%x:%x\n", macAddress[0], macAddress[1], macAddress[2], macAddress[3], macAddress[4], macAddress[5]);

	for (i = 0; i < ETH_HWADDR_LEN; i++) {
		netif->hwaddr[i] = macAddress[i];
	}
	netif->hwaddr_len = 6;
	netif->name[0] = 'e';
	netif->name[1] = 'n';
	netif->mtu = 1522;
	netif->num = 0;

	//Disable interrupts
	devregs.write(E1000_REG_IMC, UINT32_MAX);
	//Clear pending
	devregs.read(E1000_REG_ICR);

	// Clear multicast table array
	for (int i = 0; i < 128; ++i)
		devregs.write((e1000_register_t)(E1000_REG_MTA + 4 * i), 0x00000000);

	const size_t TX_BUFFER_SIZE = 2048;
	const size_t TX_DESC_COUNT = 8;

	paddr_t txbuf = 0;
	void* maptxbuf = nullptr;
	if (!i219_allocate_hardware_buffer(&maptxbuf, txbuf, TX_BUFFER_SIZE, TX_DESC_COUNT))
	{
		kprintf(u"Error: could not create Intel Gigabit Controller Tx Buffer: size %x\n", TX_BUFFER_SIZE * TX_DESC_COUNT);
		return ERR_MEM;
	}

	uint64_t txbufd;
	tx_desc_t* maptxdescs;
	if (!i219_allocate_hardware_buffer((void**)&maptxdescs, txbufd, sizeof(tx_desc_t), TX_DESC_COUNT))
	{
		kprintf(u"Error: could not create Intel Gigabit Controller Tx Buffer: size %x\n", TX_DESC_COUNT * sizeof(tx_desc_t));
		return ERR_MEM;
	}

	dinfo->maptxdescs = maptxdescs;
	dinfo->maptxbuf = maptxbuf;
	dinfo->TX_BUFFER_SIZE = TX_BUFFER_SIZE;
	dinfo->TX_DESC_COUNT = TX_DESC_COUNT;
	dinfo->txTail = 0;

	for (int i = 0; i < TX_DESC_COUNT; ++i)
	{
		// Initialize descriptor
		tx_desc_t *currDesc = &maptxdescs[i];
		currDesc->address = raw_offset<uint64_t>(txbuf, i * TX_BUFFER_SIZE);
		currDesc->length = 0;
		currDesc->status = 0;
		currDesc->cso = 0;
		currDesc->css = 0;
		currDesc->special = 0;
	}

	devregs.write(E1000_REG_TDBAH, txbufd >> 32);
	devregs.write(E1000_REG_TDBAL, txbufd & UINT32_MAX);
	devregs.write(E1000_REG_TDLEN, TX_DESC_COUNT * sizeof(tx_desc_t));
	devregs.write(E1000_REG_TDH, 0);
	devregs.write(E1000_REG_TDT, 0);

	//Receieve descriptors
	const size_t RX_BUFFER_SIZE = 2048;
	const size_t RX_DESC_COUNT = 8;

	paddr_t rxbuf;
	void* maprxbuf;
	if (!i219_allocate_hardware_buffer((void**)&maprxbuf, rxbuf, RX_BUFFER_SIZE, RX_DESC_COUNT))
	{
		kprintf(u"Error: could not create Intel Gigabit Controller Rx Buffer: size %x\n", RX_DESC_COUNT * RX_BUFFER_SIZE);
		return ERR_MEM;
	}

	uint64_t rxbufd;
	rx_desc_t* maprxdescs;
	if (!i219_allocate_hardware_buffer((void**)&maprxdescs, rxbufd, sizeof(rx_desc_t), RX_DESC_COUNT))
	{
		kprintf(u"Error: could not create Intel Gigabit Controller Rx Buffer: size %x\n", RX_DESC_COUNT * sizeof(rx_desc_t));
		return ERR_MEM;
	}

	for (int i = 0; i < RX_DESC_COUNT; ++i)
	{
		// Initialize descriptor
		rx_desc_t *currDesc = &maprxdescs[i];
		currDesc->addr = raw_offset<uint64_t>(rxbuf, i * RX_BUFFER_SIZE);
		currDesc->length = 0;
		currDesc->status = 0;
		currDesc->checksum = 0;
		currDesc->errors = 0;
		currDesc->special = 0;
	}

	devregs.write(E1000_REG_RDBAH, rxbufd >> 32);
	devregs.write(E1000_REG_RDBAL, rxbufd & UINT32_MAX);
	devregs.write(E1000_REG_RDLEN, RX_DESC_COUNT * sizeof(rx_desc_t));
	devregs.write(E1000_REG_RDH, 0);
	devregs.write(E1000_REG_RDT, RX_DESC_COUNT - 1);	//Index of descriptor beyond last valid
	devregs.write(E1000_REG_RDTR, 0);

	dinfo->maprxdescs = maprxdescs;
	dinfo->maprxbuf = maprxbuf;
	dinfo->rx_buf_phy = rxbuf;
	dinfo->RX_BUFFER_SIZE = RX_BUFFER_SIZE;
	dinfo->RX_DESC_COUNT = RX_DESC_COUNT;
	dinfo->rxCur = 0;

	pci_allocate_msi(dinfo->address.segment, dinfo->address.bus, dinfo->address.device, dinfo->address.function, 1, &ethernet_interrupt, dinfo);

	//Allocate PBUFs for receiving

	// Enable transmitter
	uint32_t tctl = devregs.read(E1000_REG_TCTL);
	tctl |= E1000_TCTL_EN; // EN (Transmitter Enable)
	tctl |= E1000_TCTL_PSP; // PSP (Pad Short Packets)
	tctl |= E1000_TCTL_RTLC; // RTLC (Re-transmit on Late Collision)
	devregs.write(E1000_REG_TCTL, tctl);

	//Disable prefetch
	devregs.write(E1000_REG_RXDCTL, 0);
	// Enable receiver
	uint32_t rctl = devregs.read(E1000_REG_RCTL);
	rctl |= E1000_RCTL_EN; // EN (Receiver Enable)
	rctl &= ~E1000_RCTL_SBP; // SBP (Store Pad Packets)
	rctl |= E1000_RCTL_BAM; // BAM (Broadcast Accept Mode)
	rctl &= ~E1000_RCTL_SZ_4096;
	rctl |= E1000_RCTL_SZ_2048; // BSIZE = 2048 (Receive Buffer Size)
	rctl &= ~E1000_RCTL_BSEX;
	rctl |= E1000_RCTL_SECRC; // SECRC (Strip Ethernet CRC)
	devregs.write(E1000_REG_RCTL, rctl);

	//Enable interrupts
	devregs.write(E1000_REG_IAM, 0);
	devregs.write(E1000_REG_IMS, E1000_IMS_RXT0 | E1000_IMS_LSC);
	//devregs.write(E1000_REG_IMS, UINT32_MAX);

	netif->flags |= NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_BROADCAST;
	netif->linkoutput = &i219_tx;
	netif->output = &etharp_output;
	netif->output_ip6 = &ethip6_output;

	uint32_t devstat = devregs.read(E1000_REG_STATUS);
	uint8_t up = devstat & (1 << 1);
	//kprintf(u"Link active %d\n", up);
	if (up)
	{
		netifapi_netif_set_link_up_async(netif);
	}
	test_netif(netif);
}

static ip4_addr_t testing_ip;
static ip4_addr_t testing_netmask;
static ip4_addr_t testing_gateway;

bool Intel219Finder(uint16_t segment, uint16_t bus, uint16_t device, uint8_t function)
{
	size_t barsize = 0;
	paddr_t devbase = read_pci_bar(segment, bus, device, function, 0, &barsize);
	void* mapped_controller = find_free_paging(barsize);
	if (!paging_map(mapped_controller, devbase, barsize, PAGE_ATTRIBUTE_NO_CACHING | PAGE_ATTRIBUTE_WRITABLE))
	{
		kprintf(u"Error: could not map Intel Gigabit Controller: size %x\n", barsize);
		return false;
	}
	uint64_t commstat;
	read_pci_config(segment, bus, device, function, 1, 32, &commstat);
	commstat |= (1 << 10);	//Mask pinned interrupts
	commstat |= 0x6;		//Memory space and bus mastering
	write_pci_config(segment, bus, device, function, 1, 32, commstat);

	kprintf(u"Found an Intel Gigabit Ethernet card! %d:%d:%d:%d\n", segment, bus, device, function);

	i219_driver_info* state = new i219_driver_info;
	state->mapped_controller = mapped_controller;
	state->address.bus = bus;
	state->address.device = device;
	state->address.segment = segment;
	state->address.function = function;

	netif* netdev = new netif;
	memset(netdev, 0, sizeof(netif));
	state->netif = netdev;
	//netdev->hwaddr
	IP4_ADDR(&testing_ip, 169, 254, 118, 124);
	IP4_ADDR(&testing_netmask, 255, 255, 255, 0);
	IP4_ADDR(&testing_gateway, 169, 254, 118, 1);
#if 1
	netifapi_netif_add(netdev, &testing_ip, &testing_netmask, &testing_gateway, state, &i219_init, &tcpip_input);
#else
	netifapi_netif_add(netdev, NULL, NULL, NULL, state, &i219_init, &tcpip_input);
#endif
	netifapi_netif_set_up_async(netdev);

	return false;
}

static pci_device_registration dev_reg_pci = {
	i219_devs,
	&Intel219Finder
};

EXTERN int CppDriverEntry(void* param)
{
	//Find relevant devices
	register_pci_driver(&dev_reg_pci);
	return 0;
}