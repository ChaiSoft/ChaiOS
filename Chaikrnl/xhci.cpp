#include <usb.h>
#include <pciexpress.h>
#include <redblack.h>
#include <kstdio.h>
#include <arch/paging.h>
#include <arch/cpu.h>
#include <string.h>
#include <scheduler.h>
#include <semaphore.h>
#include "xhci_registers.h"
#include <liballoc.h>
#include "usb_private.h"

class XHCI;

#pragma pack(push, 1)
typedef struct _xhci_slot_context {
	uint32_t RouteString : 20;
	uint32_t Speed : 4;
	uint32_t RZ : 1;
	uint32_t MTT : 1;
	uint32_t Hub : 1;
	uint32_t ContextEntries : 5;
	uint16_t MaxExitLatency;
	uint8_t RootHubPortNum;
	uint8_t NumberPorts;
	uint8_t TTHubSlotId;
	uint8_t TTPortNum;
	uint16_t TTT:2;
	uint16_t RsvdZ:4;
	uint16_t InterruptTarget:10;
	uint32_t DeviceAddress : 8;
	uint32_t RsvdZ2 : 19;
	uint32_t SlotState : 5;
}xhci_slot_context;

typedef struct _xhci_endpoint_context {
	//DWORD 0
	uint8_t EpState : 3;
	uint8_t RsvdZ : 5;
	uint8_t Mult : 2;
	uint8_t MaxPrimaryStreams : 5;
	uint8_t LSA : 1;
	uint8_t Interval;
	uint8_t MaxEsitHigh;
	//DWORD 1
	uint8_t RZ : 1;
	uint8_t CErr : 2;
	uint8_t EpType : 3;
	uint8_t R : 1;
	uint8_t HID : 1;
	uint8_t MaxBurstSize;
	uint16_t MaxPacketSize;
	//DWORD 2-3
	uint64_t TRDequeuePtr;
	//DWORD 4
	uint16_t AverageTrbLength;
	uint16_t MaxEsitLow;
	//DWORD 5-7
	uint32_t RsvdP[3];
}xhci_endpoint_context;

typedef struct _xhci_device_context {
	xhci_slot_context slot;
}xhci_device_context;
#pragma pack(pop)


typedef RedBlackTree<size_t, pci_address*> XhciPci;

struct xhci_evtring_info {
	void* ringbase;
	void* dequeueptr;
};

static pci_device_declaration xhci_devs[] = {
	{PCI_VENDOR_ANY, PCI_VENDOR_ANY, 0x0C, 0x03, 0x30},
	PCI_DEVICE_END
};

static int_fast8_t high_set_bit(size_t sz)
{
	int_fast8_t count = -1;
	while (sz)
	{
		sz >>= 1;
		count += 1;
	}
	return count;
}

static void xhci_pci_baseaddr(pci_address* addr, XHCI*& cinfo);

static bool xhci_pci_scan(uint16_t segment, uint16_t bus, uint16_t device, uint8_t function)
{
	//XHCI device
	uint64_t rev_reg = 0;
	read_pci_config(segment, bus, device, function, 0x18, 8, &rev_reg);
	kprintf_a("Found XHCI USB controller at %d:%d:%d:%d\n", segment, bus, device, function);
	kprintf_a(" USB version %d.%d\n", rev_reg >> 4, rev_reg & 0xF);
	pci_address* addr = new pci_address;
	addr->segment = segment; addr->bus = bus; addr->device = device; addr->function = function;

	XHCI* controller = nullptr;
	xhci_pci_baseaddr(addr, controller);

	RegisterHostController((USBHostController*)controller);

	return false;
}

static pci_device_registration xhci_pci_reg = {
	xhci_devs,
	&xhci_pci_scan
};

static uint8_t xhci_interrupt(size_t vector, void* param);


enum XHCI_PORT_SPEED {
	RESERVED,
	FULL_SPEED,
	LOW_SPEED,
	HIGH_SPEED,
	SUPER_SPEED,
	SUPER_SPEED_PLUS
};

struct protocol_speed {
	uint32_t ecap_protocol;
	uint8_t slottype;
	uint8_t issymmetric;
	uint8_t linkprot;
	struct rx_tx {
		uint8_t fullduplex;
		uint8_t spd_exponent;
		uint16_t spd_mantissa;
	}rx, tx;

};

struct xhci_port_info_data {
	uint8_t flags;
	uint8_t PairedPortNum;
	uint8_t ProtoOffset;
	uint8_t Reserved;
};

#define PORTINF_USB3 (1<<0)
#define PORTINF_HIGHSPEED_ONLY (1<<1)
#define PORTINF_HAS_PAIR (1<<2)
#define PORTINF_ACTIVE (1<<3)



#define MK_SLOT_CONTEXT_DWORD0(entries, hub, MultiTT, Speed, RouteString) \
(((entries & 0x1F) << 27) | ((hub & 1) << 26) | ((MultiTT & 1) << 25) | ((Speed & 0xF) << 20) | (RouteString & ((1<<20) - 1)))

#define MK_SLOT_CONTEXT_DWORD1(NumberPorts, RootHubPort, MaxExitLatency) \
(((NumberPorts & 0xFF) << 24) | ((RootHubPort & 0xFF) << 16) | (MaxExitLatency & 0xFFFF))

#define MK_ENDPOINT_CONTEXT_DWORD0(MaxESITHigh, Interval, LSA, MaxPStreams, Mult, EpState) \
(((MaxESITHigh & 0xFF) << 24) | ((Interval & 0xFF) << 16) | ((LSA & 1) << 15) | ((MaxPStreams & 0x1F) << 10) | ((Mult & 0x3) << 8) | (EpState & 0x7))

#define MK_ENDPOINT_CONTEXT_DWORD1(MaxPacketSize, MaxBurstSize, HID, EPType, CErr) \
(((MaxPacketSize & 0xFFFF) << 16) | ((MaxBurstSize & 0xFF) << 8) | ((HID & 1) << 7) | ((EPType & 0x7) << 3) | ((CErr & 0x3) << 1))

#define MK_ENDPOINT_CONTEXT_DWORD2(TRDP, DCS) \
((TRDP & 0xFFFFFFF0)| (DCS & 1))

#define MK_ENDPOINT_CONTEXT_DWORD3(TRDP) \
((TRDP >> 32) & 0xFFFFFFFF)

#define MK_ENDPOINT_CONTEXT_DWORD4(MaxESITLo, AverageTRBLen) \
(((MaxESITLo & 0xFFFF) << 16) | (AverageTRBLen & 0xFFFF))




static size_t get_trb_completion_code(void* trb)
{
	if (!trb)
		return 0x100;
	return (((uint64_t*)trb)[1] >> 24) & 0xFF;
}

static auto get_debug_flags()
{
	auto flags = arch_disable_interrupts();
	arch_restore_state(flags);
	return flags;
}

class XHCI : public USBHostController {
public:
	XHCI(void* basea)
		:cmdring(this, XHCI_DOORBELL_HOST, XHCI_DOORBELL_HOST_COMMAND),
		Capabilities(baseaddr),
		Operational(oppbase),
		Runtime(runbase),
		pRootHub(*this)
	{
		baseaddr = basea;
		event_available = 0;
		tree_lock = 0;
		port_tree_lock = 0;
		interrupt_msi = false;
	}
	virtual char16_t* ControllerType()
	{
		return u"XHCI";
	}
private:
	usb_status_t init()
	{
		if (Capabilities.HCCPARAMS1.AC64 == 0)
		{
			kprintf(u"XHCI: Warning - not a 64 bit controller!\n");
		}
		fill_baseaddress();
		//Request control of host controller, if supported
		take_bios_control();
		//Stop host controller first
		halt();
		//Reset the host controller, now it's halted
		reset();
		//Reset is complete
		//Create device context array
		createDeviceContextBuffer();
		//XHCI knows that we're loaded, and has device slots available
		pPortInfo = new xhci_port_info_data[getMaxPorts()];
		//Look up protocols
		xhci_protocol_speeds();

		Operational.USBSTS = Operational.USBSTS;
		//Get command ring
		paddr_t crb = cmdring.getBaseAddress();
		writeOperationalRegister(XHCI_OPREG_CRCR, crb | 1, 64);
#if 0
		Operational.CRCR.CommandRingPtr = crb;
		Operational.CRCR.RCS = 1;
#endif
		//Set up event rings and primary interrupter
		createPrimaryEventRing();
		//Set up synchronisation
		tree_lock = create_spinlock();
		port_tree_lock = create_spinlock();
		event_available = create_semaphore(0, u"USB-XHCI Interrupt Semaphore");
		//Start driver threads
		evtthread = create_thread(&xhci_event_thread, this, THREAD_PRIORITY_NORMAL, DRIVER_EVENT);

		//TODO - interrupts go here
		//Start USB!
		Runtime.Interrupter(0).IMAN = Runtime.Interrupter(0).IMAN;
		Runtime.Interrupter(0).IMOD.InterruptInterval = 0x3F8;
		Runtime.Interrupter(0).IMOD.InterruptCounter = 0;
		Runtime.Interrupter(0).IMOD.write();
		Operational.USBCMD.INTE = 1;
		Operational.USBCMD.HSEE = 1;
		Runtime.Interrupter(0).IMAN.InterruptEnable = 1;

		Operational.USBCMD.RunStop = 1;		//Start USB!
		while (Operational.USBSTS.HcHalted != 0);
		//Just make sure we don't miss interrupts
		
		//Work on ports
		kprintf(u"XHCI controller enabled, %d ports\n", getMaxPorts());
		return USB_SUCCESS;
#if 0
		for (size_t n = 1; n <= getMaxPorts(); ++n)
		{
			xhci_thread_port_startup* sup = new xhci_thread_port_startup;
			sup->cinfo = this;
			sup->port = n;
			//create_thread(&xhci_port_startup, sup);
			xhci_port_startup(sup);
		}
#endif
	}

	void halt()
	{
		Operational.USBCMD.RunStop = 0;
		while (Operational.USBSTS.HcHalted == 0);
	}

	void set_msi_mode(bool isMSI)
	{
		interrupt_msi = isMSI;
	}

private:

	XhciCapabilityRegisterBlock Capabilities;
	XhciOperationalRegisterBlock Operational;
	XhciRuntimeRegisterBlock Runtime;

	void reset()
	{
		Operational.USBCMD.HcReset = 1;
		//Intel xHCIs have a quirk
		Stall(1);
		while (Operational.USBCMD.HcReset != 0);
	}

	class XhciRootHub : public USBRootHub
	{
	public:
		XhciRootHub(XHCI& parent)
			:m_Parent(parent)
		{
		}
		virtual usb_status_t Init()
		{
			return m_Parent.init();
		}
		virtual usb_status_t Reset(uint8_t port)
		{
			if (!m_Parent.port_reset(port))
				return USB_FAIL;
			return USB_SUCCESS;
		}
		virtual bool PortConnected(uint8_t port)
		{
			XhciPortRegisterBlock portregs = m_Parent.Operational.Port(port);
			return (portregs.PORTSC.CCS == 1);
		}

		virtual uint8_t HubDepth()
		{
			return 0;
		}

		virtual uint8_t NumberPorts()
		{
			return m_Parent.getMaxPorts();
		}

		virtual usb_status_t AssignSlot(uint8_t port, UsbDeviceInfo*& slot, uint32_t PortSpeed, UsbDeviceInfo* parent, uint32_t routestring = 0)
		{
			XhciPortRegisterBlock portregs = m_Parent.Operational.Port(port);
			if(PortSpeed == USB_DEVICE_INVALIDSPEED)
				PortSpeed = portregs.PORTSC.PortSpeed;
			//Enable slot command
			void* last_command = nullptr;
			last_command = m_Parent.cmdring.enqueue(create_enableslot_command(m_Parent.protocol_speeds[PortSpeed].slottype));
			//Now get a command completion event from the event ring
			uint64_t* curevt = (uint64_t*)m_Parent.waitComplete(last_command, 1000);
			if (get_trb_completion_code(curevt) != XHCI_COMPLETION_SUCCESS)
			{
				kprintf(u"Error enabling slot: %x!\n", get_trb_completion_code(curevt));
				return USB_XHCI_TRB_ERR(get_trb_completion_code(curevt));
			}
			uint8_t slotid = curevt[1] >> 56;
			if (slotid == 0)
				return USB_FAIL;

			xhci_device_info* port_info = new xhci_device_info(&m_Parent, slotid, port);
			port_info->controller = &m_Parent;
			port_info->portindex = port;
			port_info->slotid = slotid;
			if (!m_Parent.createSlotContext(PortSpeed, port_info, parent, routestring))
				return USB_FAIL;

			slot = port_info;
			return USB_SUCCESS;
		}
	private:
		XHCI& m_Parent;
	};

	bool port_reset(size_t portindex)
	{
		static const uint32_t xHC_PortUSB_CHANGE_BITS = ((1 << 17) | (1 << 18) | (1 << 20) | (1 << 21) | (1 << 22));
		static const uint32_t PortPower = (1 << 9);
		XhciPortRegisterBlock portregs = Operational.Port(portindex);

		//Clear change bits
		uint32_t HCPortStatusOff = XHCI_OPREG_PORTSC(portindex);
		if ((readOperationalRegister(HCPortStatusOff, 32) & PortPower) == 0)
		{
			kprintf(u"Powering Port %d\n", portindex);
			writeOperationalRegister(HCPortStatusOff, PortPower, 32);
			Stall(20);
			if ((readOperationalRegister(HCPortStatusOff, 32) & PortPower) == 0)
			{
				kprintf(u"Failed to reset Port %d\n", portindex);
				return false;
			}
		}
		writeOperationalRegister(HCPortStatusOff, PortPower | xHC_PortUSB_CHANGE_BITS, 32);

		auto& PortInfo = pPortInfo[portindex];
		auto proto = PortInfo.flags & PORTINF_USB3;

		if (portregs.PORTSC.PED == 0 || true)
		{

			uint32_t resetBit = (proto != 0) ? (1 << 31) : (1 << 4);
			uint32_t resetChange = (proto != 0) ? (1 << 19) : (1 << 21);
			writeOperationalRegister(HCPortStatusOff, PortPower | resetBit, 32);
			int timeout = 500;
			while (timeout > 0)
			{
				if (readOperationalRegister(HCPortStatusOff, 32) & resetChange)
					break;
				Stall(1);
			}
			if (timeout == 0)
			{
				//kprintf(u"Reset Port %d timed out\n", portindex);
				return false;
			}
			Stall(20);
			//kprintf(u"Reset Port %d\n", portindex);
		}
		else
		{
			//kprintf(u"Reset Port %d skipped\n", portindex);
		}
		
		if (portregs.PORTSC.PED != 0)
		{
			//Port enabled
			//clear status
			writeOperationalRegister(HCPortStatusOff, PortPower | xHC_PortUSB_CHANGE_BITS, 32);
			if (proto == 0)
			{
				//USB2 reset
				PortInfo.flags |= PORTINF_ACTIVE;
				if (PortInfo.flags & PORTINF_HAS_PAIR)
				{
					pPortInfo[PortInfo.PairedPortNum].flags &= ~PORTINF_ACTIVE;
				}
			}
			return true;
		}
		else if(proto != 0)
		{
			//Disable this port and enable paired USB2
			if (PortInfo.flags & PORTINF_HAS_PAIR)
			{
				pPortInfo[PortInfo.PairedPortNum].flags |= PORTINF_ACTIVE;
			}
		}
		return false;
		//
		
	}

	uint64_t readCapabilityRegister(size_t reg, size_t width)
	{
		switch (width)
		{
		case 8:
			return *raw_offset<volatile uint8_t*>(baseaddr, reg);
		case 16:
			return *raw_offset<volatile uint16_t*>(baseaddr, reg);
		case 32:
			return *raw_offset<volatile uint32_t*>(baseaddr, reg);
		case 64:
			return *raw_offset<volatile uint64_t*>(baseaddr, reg);
		}
		return 0;
	}

	void writeCapabilityRegister(size_t reg, uint64_t value, size_t width)
	{
		switch (width)
		{
		case 8:
			*raw_offset<volatile uint8_t*>(baseaddr, reg) = value;
			break;
		case 16:
			*raw_offset<volatile uint16_t*>(baseaddr, reg) = value;
			break;
		case 32:
			*raw_offset<volatile uint32_t*>(baseaddr, reg) = value;
			break;
		case 64:
			*raw_offset<volatile uint64_t*>(baseaddr, reg) = value;
			break;
		}
	}

	uint64_t readOperationalRegister(size_t reg, size_t width)
	{
		switch (width)
		{
		case 8:
			return *raw_offset<volatile uint8_t*>(oppbase, reg);
		case 16:
			return *raw_offset<volatile uint16_t*>(oppbase, reg);
		case 32:
			return *raw_offset<volatile uint32_t*>(oppbase, reg);
		case 64:
			return *raw_offset<volatile uint64_t*>(oppbase, reg);
		}
		return 0;
	}

	void writeOperationalRegister(size_t reg, uint64_t value, size_t width)
	{
		switch (width)
		{
		case 8:
			*raw_offset<volatile uint8_t*>(oppbase, reg) = value;
			break;
		case 16:
			*raw_offset<volatile uint16_t*>(oppbase, reg) = value;
			break;
		case 32:
			*raw_offset<volatile uint32_t*>(oppbase, reg) = value;
			break;
		case 64:
			*raw_offset<volatile uint64_t*>(oppbase, reg) = value;
			break;
		}
	}

	size_t getMaxPorts()
	{
		return (readCapabilityRegister(XHCI_CAPREG_HCSPARAMS1, 32) >> 24) & 0xFF;
	}

	void xhci_protocol_speeds()
	{
		//Initialise default values
		for (size_t i = 0; i <= 5; ++i)
		{
			protocol_speeds[i].ecap_protocol = 0;
			protocol_speeds[i].issymmetric = 1;
			protocol_speeds[i].linkprot = 0;
			protocol_speeds[i].slottype = 0;
			if (i >= 4)
			{
				protocol_speeds[i].rx.fullduplex = 1;
				protocol_speeds[i].rx.spd_exponent = 3;
			}
			else if (i == 1 || i == 3)
			{
				protocol_speeds[i].rx.fullduplex = 0;
				protocol_speeds[i].rx.spd_exponent = 2;
			}
			else
			{
				protocol_speeds[i].rx.fullduplex = 0;
				protocol_speeds[i].rx.spd_exponent = 1;
				protocol_speeds[i].rx.spd_mantissa = 1500;
			}
		}
		protocol_speeds[1].rx.spd_mantissa = 12;
		protocol_speeds[3].rx.spd_mantissa = 480;
		protocol_speeds[4].rx.spd_mantissa = 5;
		protocol_speeds[5].rx.spd_mantissa = 10;

		uint32_t supportp = 0;
		do
		{
			supportp = findExtendedCap(XHCI_ECAP_SUPPORT, supportp);
			if (supportp != 0)
			{
				uint32_t supreg = readCapabilityRegister(supportp, 32);
				union {
					char usb_str[5];
					uint32_t namestr;
				}u;
				u.namestr = readCapabilityRegister(supportp + 4, 32);
				u.usb_str[4] = 0;
				uint16_t revision = supreg >> 16;
				uint32_t PortProtocol = readCapabilityRegister(supportp + 8, 32);
				uint32_t psic = PortProtocol >> 28;
				uint32_t CompatPortOffset = PortProtocol & 0xFF;
				uint32_t CompatPortCount = (PortProtocol >> 8) & 0xFF;
				uint32_t ProtoDefined = (PortProtocol >> 16) & 0x0FFF;
				uint32_t slott = readCapabilityRegister(supportp + 0xC, 32) & 0x1F;
				for (int i = 0; i < CompatPortCount; ++i)
				{
					auto& portInf = pPortInfo[i + CompatPortOffset - 1];
					portInf.ProtoOffset = i;
					if ((revision >> 8) == 2)
					{
						if (ProtoDefined & 2)
							portInf.flags |= PORTINF_HIGHSPEED_ONLY;
					}
					else
					{
						portInf.flags |= PORTINF_USB3;
					}
				}
				//kprintf_a("%s%x.%02x, %d descriptors, offset %d, count %d, slot type %d\n", u.usb_str, revision >> 8, revision & 0xFF, psic, CompatPortOffset, CompatPortCount, slott);
				for (size_t i = 0; i < psic; ++i)
				{
					uint32_t data = readCapabilityRegister(supportp + 0x10 + i * 4, 32);
					uint32_t psiv = data & 0xF;
					protocol_speeds[psiv].ecap_protocol = supportp;
					protocol_speeds[psiv].linkprot = (data >> 14) & 3;
					protocol_speeds[psiv].slottype = slott;
					protocol_speed::rx_tx* relstruct;
					uint32_t psitype = (data >> 6) & 0x3;
					if (psitype == 0)		//Symmetric
					{
						protocol_speeds[psiv].issymmetric = 1;
						relstruct = &protocol_speeds[psiv].rx;
					}
					else if (psitype == 2)		//Rx
					{
						protocol_speeds[psiv].issymmetric = 0;
						relstruct = &protocol_speeds[psiv].rx;
					}
					else
					{
						protocol_speeds[psiv].issymmetric = 0;
						relstruct = &protocol_speeds[psiv].tx;
					}
					relstruct->fullduplex = (data >> 8) & 1;
					relstruct->spd_exponent = (data >> 4) & 3;
					relstruct->spd_mantissa = (data >> 16);
					//kprintf_a(" Protocol %d, speed %d*10^%d\n", psiv, relstruct->spd_mantissa, relstruct->spd_exponent);
				}
			}
		} while (supportp != 0);
		//Pair up ports
		auto numPorts = getMaxPorts();
		for (int i = 0; i < numPorts; ++i)
		{
			auto& lhsPort = pPortInfo[i];
			for (int j = 0; j < numPorts; ++j)
			{
				auto& rhsPort = pPortInfo[j];
				if (lhsPort.ProtoOffset == rhsPort.ProtoOffset && (lhsPort.flags & PORTINF_USB3) != (rhsPort.flags & PORTINF_USB3))
				{
					lhsPort.PairedPortNum = j;
					rhsPort.PairedPortNum = i;
					lhsPort.flags |= PORTINF_HAS_PAIR;
					rhsPort.flags |= PORTINF_HAS_PAIR;
				}
			}
		}
		for (int j = 0; j < numPorts; ++j)
		{
			auto& Port = pPortInfo[j];
			auto shouldBeActive = !(Port.flags & PORTINF_HAS_PAIR) || (Port.flags & PORTINF_USB3);
			if (shouldBeActive)
				Port.flags |= PORTINF_ACTIVE;
		}
		//kprintf(u"XHCI supported protocols loaded\n");
	}

	static void Stall(uint32_t milliseconds)
	{
		uint64_t current = arch_get_system_timer();
		while (arch_get_system_timer() - current < milliseconds);
	}

	struct xhci_thread_port_startup {
		XHCI* cinfo;
		size_t port;
	};

	class TransferRingBase {
	public:
		TransferRingBase(XHCI* parent, size_t slot, size_t endpoint)
			: m_parent(parent), m_slot(slot), m_endpoint(endpoint)
		{
			ring_lock = create_spinlock();
			m_ringbase = m_enqueue = pmmngr_allocate(1);
			m_mapped_enqueue = (xhci_trb*)find_free_paging(PAGESIZE);
			paging_map(m_mapped_enqueue, m_enqueue, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING);
			memset(m_mapped_enqueue, 0, PAGESIZE);
			slotbit = XHCI_TRB_ENABLED;

			WriteLink(raw_offset<xhci_trb*>(m_ringbase, PAGESIZE), m_ringbase, m_mapped_enqueue, true);
		}

		paddr_t getBaseAddress()
		{
			return m_ringbase;
		}

	protected:
		void* EnqueueTrbs(xhci_trb** trbs)
		{
			auto st = acquire_spinlock(ring_lock);
			//Write command
			size_t n = 0;
			while (trbs[n])
			{
				paddr_t lastcmd = m_enqueue;
				m_mapped_enqueue = place_trb(m_mapped_enqueue, trbs[n]->lowval, trbs[n]->highval, m_enqueue);
				m_parent->EnqueueTrb(trbs[n], m_enqueue);
				auto st2 = acquire_spinlock(m_parent->tree_lock);
				m_parent->PendingCommands[lastcmd] = trbs;
				release_spinlock(m_parent->tree_lock, st2);
				delete trbs[n];
				++n;
			}
			m_parent->ringDoorbell(m_slot, m_endpoint);
			release_spinlock(ring_lock, st);
			return trbs;
		}

	private:
		static void WriteLink(xhci_trb* segmentEnd, paddr_t NextSegment, void* NextSegmentMapped, bool togglecycle)
		{
			auto cycleback = create_link_trb(NextSegment, togglecycle);
			paddr_t discard = 0;
			xhci_write_crb(&segmentEnd[-2], cycleback->lowval, cycleback->highval, discard);
			auto mappedhelper = create_event_data_trb((paddr_t)NextSegmentMapped);
			xhci_write_crb(&segmentEnd[-1], mappedhelper->lowval, mappedhelper->highval, discard);
		}

		static const int TRB_SIZE = 0x10;

		static xhci_trb* xhci_write_crb(xhci_trb* crbc, uint64_t low, uint64_t high, paddr_t& penq)
		{
			//kprintf(u"Writing TRB to %x(%x) <- %x:%x\n", crbc, penq, high, low);
			*raw_offset<volatile uint64_t*>(crbc, 8) = high;
			arch_memory_barrier();
			*raw_offset<volatile uint64_t*>(crbc, 0) = low;
			penq += TRB_SIZE;
			return raw_offset<xhci_trb*>(crbc, TRB_SIZE);
		}

		xhci_trb* place_trb(xhci_trb* crbc, uint64_t low, uint64_t high, paddr_t& penq)
		{
			xhci_trb* ret = xhci_write_crb(crbc, low, high | slotbit, penq);
			if (XHCI_GET_TRB_TYPE(ret->highval) == XHCI_TRB_TYPE_LINK)
			{
				//Enable the link TRB for the current cycle
				ret->highval &= (~XHCI_TRB_TOGGLECYCLE);
				ret->highval |= slotbit;

				penq = ret->lowval;
				if(ret->highval & XHCI_TRB_TOGGLECYCLE)
					slotbit = (slotbit == 0) ? XHCI_TRB_ENABLED : 0;

				ret = (xhci_trb*)ret[1].lowval;				
			}
			return ret;
		}
		XHCI* const m_parent;
		spinlock_t ring_lock;
		paddr_t m_enqueue;
		paddr_t m_ringbase;
		xhci_trb* m_mapped_enqueue;
		size_t m_slot;
		size_t m_endpoint;
		uint64_t slotbit;
	};

	class TransferRing : public TransferRingBase {
	public:
		TransferRing(XHCI* parent, size_t slot, size_t endpoint)
			: TransferRingBase(parent, slot, endpoint)
		{

		}

		void* enqueue(xhci_trb* trb)
		{
			xhci_trb* trbs[2];
			trbs[0] = trb;
			trbs[1] = nullptr;
			return enqueue_trbs(trbs);
		}

		void* enqueue_trbs(xhci_trb** tbs)
		{
			return EnqueueTrbs(tbs);
		}
	};

	class CommandRing : public TransferRingBase {
	public:
		CommandRing(XHCI* parent, size_t slot, size_t endpoint)
			: TransferRingBase(parent, slot, endpoint)
		{
		}

		void* enqueue(xhci_command* command)
		{
			xhci_command* cmds[2];
			cmds[0] = command;
			cmds[1] = nullptr;
			return enqueue_commands(cmds);
		}

		void* enqueue_commands(xhci_command** commands)
		{
			return EnqueueTrbs((xhci_trb**)commands);
		}
	};

	void EnqueueTrb(void* command, paddr_t enqueue)
	{
		auto st = acquire_spinlock(tree_lock);
		PendingCommands[enqueue] = command;
		release_spinlock(tree_lock, st);
	}

	class xhci_endpoint_info : public UsbEndpoint {
	public:
		xhci_endpoint_info(XHCI* parent, size_t slot, uint8_t epindex)
			:transferRing(parent, slot, XHCI_DOORBELL_ENDPOINT(epindex, 0))
		{
		}

		void InitEndpointContext(volatile xhci_endpoint_context* context)
		{
			context->TRDequeuePtr = transferRing.getBaseAddress() | 1;
		}

		virtual usb_status_t CreateBuffers(void** bufferptr, size_t bufsize, size_t bufcount)
		{
			size_t BufsPerPage = PAGESIZE / bufsize;	//Avoid individual buffer spanning two pages
			uint8_t* ScratchBlock = new uint8_t[DIV_ROUND_UP(bufcount, BufsPerPage) * PAGESIZE + PAGESIZE - 1];
			ScratchBlock = (uint8_t*)ALIGN_UP((size_t)ScratchBlock, PAGESIZE);
			*bufferptr = ScratchBlock;
			xhci_trb** trbs = new xhci_trb*[bufcount + 1];
			size_t offset = 0;
			for (int i = 0; i < bufcount; ++i)
			{
				auto buffer = raw_offset<void*>(ScratchBlock, offset);
				paddr_t phyBase = get_physical_address(buffer);
				trbs[i] = create_normal_trb(phyBase, bufsize, 1, 0, false);
				offset += bufsize;
				if (ALIGN_UP(offset + bufsize, PAGESIZE) != ALIGN_UP(offset, PAGESIZE))
					offset = ALIGN_UP(offset, PAGESIZE);
			}
			trbs[bufcount] = nullptr;
			transferRing.enqueue_trbs(trbs);
			delete[] trbs;
			return USB_SUCCESS;		
		}

		virtual usb_status_t AddBuffer(void* buffer, size_t length)
		{
			//Build scatter gather
			paddr_t phyBase = get_physical_address(buffer);
			size_t curLength = ALIGN_UP(phyBase, PAGESIZE) - phyBase;
			curLength = curLength > length ? length : curLength;

			size_t WorstCase = DIV_ROUND_UP(length - curLength, PAGESIZE) + 1;
			xhci_trb** trbs = new xhci_trb*[WorstCase + 1];
			auto trbindex = 0;
			trbs[trbindex++] = create_normal_trb(phyBase, curLength, WorstCase - 1, 0, curLength < length);
			while (curLength < length)
			{
				length -= curLength;
				buffer = raw_offset<void*>(buffer, curLength);
				phyBase = get_physical_address(buffer);
				curLength = PAGESIZE;
				for (int n = 1; (PAGESIZE * n < length) && (phyBase + PAGESIZE * n == get_physical_address(raw_offset<void*>(buffer, PAGESIZE * n))); ++n)
					curLength += PAGESIZE;
				curLength = curLength > length ? length : curLength;
				curLength = curLength > MAX_TRANSFER_SIZE ? MAX_TRANSFER_SIZE : curLength;
				trbs[trbindex++] = create_normal_trb(phyBase, curLength, DIV_ROUND_UP(length - curLength, PAGESIZE), 0, curLength < length);
			}
			trbs[trbindex] = nullptr;
			transferRing.enqueue_trbs(trbs);
			delete[] trbs;
			return USB_SUCCESS;
		}
	private:
		const int MAX_TRANSFER_SIZE = 64 * 1024;
		TransferRing transferRing;
	};

	class xhci_device_info : public UsbDeviceInfo {
	public:
		xhci_device_info(XHCI* parent, size_t slot, uint8_t port)
			:UsbDeviceInfo(parent->pRootHub, port),
			cmdring(parent, slot, XHCI_DOORBELL_ENPOINT0)
		{
			controller = parent;
			command_lock = create_spinlock();
		}
		XHCI* controller;
		uint32_t portindex;
		uint32_t slotid;
		CommandRing cmdring;
		xhci_device_context* device_context;
		paddr_t phy_context;
		spinlock_t command_lock;

		virtual size_t OperatingPacketSize()
		{
			return controller->GetEndpointContext(device_context, 0, 0)->MaxPacketSize;
		}
		virtual usb_status_t UpdatePacketSize(size_t size)
		{
			paddr_t inContext;
			auto devctxt = controller->CreateInputContext(inContext, 1 << 1, 0, 0, 0, 0);
			auto epdef = controller->GetEndpointContext(devctxt, 0, 1);
			epdef->MaxPacketSize = size;
			auto cmd = create_evaluate_command(inContext, slotid);
			void* last_command = controller->cmdring.enqueue(cmd);
			uint64_t* curevt = (uint64_t*)controller->waitComplete(last_command, 1000);
			if (get_trb_completion_code(curevt) != XHCI_COMPLETION_SUCCESS)
			{
				kprintf(u"Error reconfiguring max packet size: %x\n", get_trb_completion_code(curevt));
				return USB_XHCI_TRB_ERR(get_trb_completion_code(curevt));
			}
			Stall(USB_DELAY_PORT_RESET);
			return USB_SUCCESS;
		}
		virtual usb_status_t RequestData(REQUEST_PACKET& device_packet, void** resultData)
		{
			usb_status_t status = USB_FAIL;
			void* ptr = kmalloc(device_packet.length);

			set_paging_attributes(ptr, device_packet.length, PAGE_ATTRIBUTE_NO_CACHING, 0);
			status = controller->xhci_control_in(this, &device_packet, ptr, device_packet.length);
			if (USB_FAILED(status))
				return status;
			set_paging_attributes(ptr, device_packet.length, 0, PAGE_ATTRIBUTE_NO_CACHING);
			*resultData = ptr;
			return USB_SUCCESS;
		}

		virtual usb_status_t DoConfigureEndpoint(UsbEndpointDesc* pEndpoints, uint8_t config, uint8_t iface, uint8_t alternate, uint8_t downstreamports, bool clearold = false)
		{
			paddr_t incontxt;
			size_t epAdd = 1;
			size_t highbit = 0;
			void* last_command = nullptr;
			if (config == 0)
			{
				last_command = controller->cmdring.enqueue(create_configureendpoint_command(NULL, slotid, true));
			}
			else
			{

				auto parseep = [](usb_endpoint_descriptor* ep, uint8_t& addr, uint8_t& dirIn)
				{
					addr = ep->bEndpointAddress & 0xF;
					dirIn = ep->bEndpointAddress >> 7;
				};

				auto BitToSet = [&](usb_endpoint_descriptor* ep)
				{
					uint8_t addr, dirIn;
					parseep(ep, addr, dirIn);
					return 2 * addr + dirIn;
				};

				for (int n = 0; pEndpoints[n].epDesc; ++n)
				{
					auto bitset = BitToSet(pEndpoints[n].epDesc);
					epAdd |= (1 << bitset);
					highbit = bitset > highbit ? bitset : highbit;
				}

				if (clearold)
				{
					epAdd = epAdd & (~(size_t)1);
					auto mapclear = controller->CreateInputContext(incontxt, 0, epAdd, config, iface, alternate);
					void* last_command = controller->cmdring.enqueue(create_configureendpoint_command(incontxt, slotid, true));
					uint64_t* curevt = (uint64_t*)controller->waitComplete(last_command, 1000);
					if (get_trb_completion_code(curevt) != XHCI_COMPLETION_SUCCESS)
					{
						kprintf(u"Error configuring endpoint: %x\n", get_trb_completion_code(curevt));
						return USB_XHCI_TRB_ERR(get_trb_completion_code(curevt));
					}
				}

				auto mapin = controller->CreateInputContext(incontxt, epAdd, 0, config, iface, alternate);
				mapin->slot.ContextEntries = highbit;
				if (downstreamports > 0)
				{
					mapin->slot.Hub = 1;
					mapin->slot.NumberPorts = downstreamports;
				}

				for (int n = 0; pEndpoints[n].epDesc; ++n)
				{
					auto ep = pEndpoints[n].epDesc;
					auto companion = pEndpoints[n].companionDesc;
					uint8_t addr, dirIn;
					parseep(ep, addr, dirIn);
					auto epct = controller->GetEndpointContext(mapin, addr, dirIn);
					epct->MaxPacketSize = ep->wMaxPacketSize & 0x7FF;

					auto MaxBurstSize = companion ? companion->bMaxBurst : 0;
					epct->MaxBurstSize = MaxBurstSize;

					auto usbtyp = ep->bmAttributes & USB_EP_ATTR_TYPE_MASK;
					epct->EpType = usbtyp | (dirIn << 2);

					auto epclass = new xhci_endpoint_info(controller, slotid, GetXhciEndpointIndex(addr, dirIn));
					pEndpoints[n].pEndpoint = epclass;
					epclass->InitEndpointContext(epct);

					auto MaxEsit = epct->MaxPacketSize * (MaxBurstSize + 1);
					epct->HID = 1;

					auto IntervalExp = high_set_bit(ep->bInterval);

					switch (usbtyp)
					{
					case USB_EP_ATTR_TYPE_CONTROL:
						break;
					case USB_EP_ATTR_TYPE_BULK:
						epct->MaxPrimaryStreams = 0;
						//TODO: Support streams
						//epct->MaxPrimaryStreams = companion ? companion->Bulk.MaxStream : 0;
						break;
					case USB_EP_ATTR_TYPE_INTERRUPT:
						epct->CErr = 3;
						epct->MaxEsitLow = MaxEsit & 0xFFFF;
						epct->MaxEsitHigh = MaxEsit >> 16;
						epct->Interval = ep->bInterval;
						epct->AverageTrbLength = 0x400;
						break;
					case USB_EP_ATTR_TYPE_ISOCHRONOUS:
						epct->Mult = companion ? companion->Isochronous.Mult : 0;
						break;
					default:
						break;
					}
				}

				//Issue address device command
				last_command = controller->cmdring.enqueue(create_configureendpoint_command(incontxt, slotid, false));
			}
			uint64_t* curevt = (uint64_t*)controller->waitComplete(last_command, 1000);
			if (get_trb_completion_code(curevt) != XHCI_COMPLETION_SUCCESS)
			{
				kprintf(u"Error configuring endpoint: %x\n", get_trb_completion_code(curevt));
				return USB_XHCI_TRB_ERR(get_trb_completion_code(curevt));
			}

			return USB_SUCCESS;
		}
	};

	volatile xhci_endpoint_context* GetEndpointContext(volatile xhci_device_context* devct, uint8_t epNum, uint8_t dirIn)
	{
		auto stride = 0x20 * (1 + Capabilities.HCCPARAMS1.CSZ);
		return raw_offset<volatile xhci_endpoint_context*>(devct, stride * GetXhciEndpointIndex(epNum, dirIn));
	}

	//bool update_packet_size_fs(xhci_device_info* port)
	//{
	//	uint8_t* buffer = new uint8_t[8];
	//	REQUEST_PACKET device_packet;
	//	device_packet.request_type = USB_BM_REQUEST_INPUT | USB_BM_REQUEST_STANDARD | USB_BM_REQUEST_DEVICE;
	//	device_packet.request = USB_BREQUEST_GET_DESCRIPTOR;
	//	device_packet.value = USB_DESCRIPTOR_WVALUE(USB_DESCRIPTOR_DEVICE, 0);
	//	device_packet.index = 0;
	//	device_packet.length = 8;

	//	if (size_t res = xhci_control_in(port, &device_packet, buffer, 8))
	//	{
	//		kprintf(u"Error getting FullSpeed packet size: %d\n", res);
	//		return false;
	//	}

	//	usb_device_descriptor* devdes = (usb_device_descriptor*)buffer;
	//	if (devdes->bDeviceClass == 0x09)
	//		kprintf(u"USB Hub\n");
	//	//Update information
	//	if (devdes->bMaxPacketSize0 != 64)
	//	{
	//		//Create input context
	//		//Allocate input context
	//		paddr_t incontext = pmmngr_allocate(1);
	//		void* mapincontxt = find_free_paging(PAGESIZE);
	//		paging_map(mapincontxt, incontext, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING);
	//		memset(mapincontxt, 0, PAGESIZE);
	//		//Set input control context
	//		volatile uint32_t* aflags = raw_offset<volatile uint32_t*>(mapincontxt, 4);
	//		*aflags = 2;
	//		//Copy existing data structures
	//		memcpy(raw_offset<void*>(mapincontxt, 0x20), port->device_context, 0x20 * 2);
	//		*raw_offset<uint16_t*>(mapincontxt, 0x40 + 0x6) = devdes->bMaxPacketSize0;
	//		//Notify xHCI
	//		void* lastcmd = cmdring.enqueue(create_evaluate_command(incontext, port->slotid));
	//		void* res = waitComplete(lastcmd, 1000);
	//		if (get_trb_completion_code(res) != XHCI_COMPLETION_SUCCESS)
	//		{
	//			kprintf(u"Error updating device context\n");
	//			return false;
	//		}
	//	}
	//	return true;
	//}

	void ringDoorbell(size_t slot, size_t endpoint)
	{
		arch_memory_barrier();
		*raw_offset<volatile uint32_t*>(doorbell, XHCI_DOORBELL(slot)) = endpoint;
		//Flush PCI writes
		volatile uint32_t st = *raw_offset<volatile uint32_t*>(doorbell, XHCI_DOORBELL(slot));
	}
	

	uint32_t findExtendedCap(uint8_t capid, uint32_t searchstart = 0)
	{
		uint32_t ptr = 0;
		if (searchstart == 0)
		{
			ptr = Capabilities.HCCPARAMS1.EcapPtr * 4;
		}
		else
		{
			ptr = ecap_get_next(searchstart) * 4;
			if (ptr == 0)
				return 0;
			else
				ptr += searchstart;
		}
		for (; ptr; ptr += ecap_get_next(ptr) * 4)
		{
			if (ecap_get_id(ptr) == capid)
				return ptr;
			if (ecap_get_next(ptr) == 0)
				ptr = 0;
		}
		return 0;
	}

#pragma pack(push, 1)
	struct xhci_ecap {
		uint8_t capid;
		uint8_t next;
		uint16_t data;
	};
#pragma pack(pop)

	uint8_t ecap_get_next(uint32_t ecap)
	{
		return (readCapabilityRegister(ecap, 32) >> 8) & 0xFF;
	}

	uint8_t ecap_get_id(uint32_t ecap)
	{
		return (readCapabilityRegister(ecap, 32)) & 0xFF;
	}

	void eventThread()
	{
		while (1)
		{
			wait_semaphore(event_available, 1, TIMEOUT_INFINITY);
			//kprintf(u"Events available\n");

			volatile uint64_t* ret = (volatile uint64_t*)primaryevt.dequeueptr;
			uint64_t erdp = Runtime.Interrupter(0).ERDP.DequeuePtr;
			while (ret[1] & XHCI_TRB_ENABLED)
			{
#if 1
				if (get_trb_completion_code((void*)ret) == XHCI_COMPLETION_STALL)
				{
					//STALL, reset endpoint
					size_t slotid = ret[1] >> 56;
					cmdring.enqueue(create_resetendpoint_command(slotid, 1));
				}
				//kprintf(u"Event fired: type %d, value %x:%x\n", XHCI_GET_TRB_TYPE(ret[1]), ret[1], ret[0]);
				if (XHCI_GET_TRB_TYPE(ret[1]) == XHCI_TRB_TYPE_COMMAND_COMPLETE || XHCI_GET_TRB_TYPE(ret[1]) == XHCI_TRB_TYPE_TRANSFER_EVENT)
				{
					paddr_t cmd_trb = ret[0];
					auto st = acquire_spinlock(tree_lock);
					void* waiting_handle = nullptr;
					auto it = PendingCommands.find(cmd_trb);
					if (it != PendingCommands.end())
					{
						waiting_handle = it->second;
						PendingCommands.remove(cmd_trb);
					}
					//kprintf(u"Event for TRB %x\n", waiting_handle);
					if (waiting_handle)
					{
						CompletedCommands[waiting_handle] = (void*)ret;
						if (WaitingCommands.find(waiting_handle) != WaitingCommands.end())
						{
							semaphore_t waiter = WaitingCommands[waiting_handle];
							release_spinlock(tree_lock, st);
							signal_semaphore(waiter, 1);
						}
						else
						{
							release_spinlock(tree_lock, st);
						}
					}
					else
						release_spinlock(tree_lock, st);
#if 1
					if (XHCI_GET_TRB_TYPE(ret[1]) == XHCI_TRB_TYPE_TRANSFER_EVENT)
					{
						if (((ret[1] >> 48) & 0x1F) > 1)
						{
							//kprintf(u"Event from non-control endpoint %x!\n", (ret[1] >> 48) & 0x1F);
						}
						//kprintf(u"Event for rethandle %x (TRB %x)\n", waiting_handle, cmd_trb);
					}
#endif
				}

#if 0
				else if (XHCI_GET_TRB_TYPE(ret[1]) == XHCI_TRB_TYPE_PORT_STATUS_CHANGE)
				{
					uint32_t port = (ret[0] >> 24) & 0xFF;
					auto st = acquire_spinlock(port_tree_lock);
					CompletedPorts[port] = (void*)ret;
					if (PortStatusChange.find(port) != PortStatusChange.end())
					{
						semaphore_t waiter = PortStatusChange[port];
						signal_semaphore(waiter, 1);
					}
					else
					{
					}
					release_spinlock(port_tree_lock, st);
				}
#endif
#endif

				ret += 2;
				erdp += 0x10;		//next TRB
			}
			write_semaphore(event_available, 0);
			primaryevt.dequeueptr = (void*)ret;
			Runtime.Interrupter(0).ERDP.update(erdp, false);
		}
	}

	static void xhci_event_thread(void* param)
	{
		XHCI* cinfo = reinterpret_cast<XHCI*>(param);
		cinfo->eventThread();
	}

	friend static uint8_t xhci_interrupt(size_t vector, void* info);

	void fill_baseaddress()
	{
		oppbase = raw_offset<void*>(baseaddr, Capabilities.CAPLENGTH);
		runbase = raw_offset<void*>(baseaddr, Capabilities.RUNTIMEOFF.RuntimeOffset);
		//Now find doorbell registers
		doorbell = raw_offset<void*>(baseaddr, Capabilities.DBOFF.Dboffset);
	}

	void take_bios_control()
	{
		uint32_t legacy = findExtendedCap(XHCI_ECAP_LEGSUP);
		if (legacy != 0)
		{
			uint32_t legreg = readCapabilityRegister(legacy, 32);
			legreg |= (1 << 24);
			writeCapabilityRegister(legacy, legreg, 32);
			while (1)
			{
				legreg = readCapabilityRegister(legacy, 32);
				if (((legreg & (1 << 24)) != 0) && ((legreg & (1 << 16)) == 0))
					break;
			}
			kprintf(u"Taken control of XHCI from firmware\n");
			writeCapabilityRegister(legacy, legreg & ~(1 << 24), 32);
		}
		//Disable SMIs
		uint32_t legctlsts = readCapabilityRegister(legacy + 4, 32);
		legctlsts &= XHCI_LEGCTLSTS_DISABLE_SMI;
		legctlsts |= XHCI_LEGCTLSTS_EVENTS_SMI;
		writeCapabilityRegister(legacy + 4, legctlsts, 32);
	}

	xhci_evtring_info* MakeEventRing(uint8_t interrupter)
	{
		xhci_evtring_info* pinf = new xhci_evtring_info;
		paddr_t evtseg = pmmngr_allocate(1);
		void* evtringseg = find_free_paging(PAGESIZE);
		paging_map(evtringseg, evtseg, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING);
		memset(evtringseg, 0, PAGESIZE);
		//Create one event ring segment
		paddr_t evt = pmmngr_allocate(1);
		void* evtring = find_free_paging(PAGESIZE);
		void* evtdp = evtring;
		paging_map(evtring, evt, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING);
		memset(evtring, 0, PAGESIZE);
		//Write it to the segment table
		*raw_offset<volatile uint64_t*>(evtringseg, 0) = evt;
		*raw_offset<volatile uint64_t*>(evtringseg, 8) = PAGESIZE / 0x10;
		//Write the event ring info
		pinf->dequeueptr = evtdp;
		pinf->ringbase = evtring;
		//Set segment table size
		Runtime.Interrupter(interrupter).ERSTSZ.Size = 1;
		//Set dequeue pointer
		Runtime.Interrupter(interrupter).ERDP.update(evt, false);
		//Enable event ring
		Runtime.Interrupter(interrupter).ERSTBA.SegTableBase = evtseg;
		Runtime.Interrupter(interrupter).IMAN = Runtime.Interrupter(0).IMAN;
		return pinf;
	}

	void createPrimaryEventRing()
	{
		paddr_t evtseg = pmmngr_allocate(1);
		void* evtringseg = find_free_paging(PAGESIZE);
		paging_map(evtringseg, evtseg, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING);
		memset(evtringseg, 0, PAGESIZE);
		//Create one event ring segment
		paddr_t evt = pmmngr_allocate(1);
		void* evtring = find_free_paging(PAGESIZE);
		void* evtdp = evtring;
		paging_map(evtring, evt, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING);
		memset(evtring, 0, PAGESIZE);
		//Write it to the segment table
		*raw_offset<volatile uint64_t*>(evtringseg, 0) = evt;
		*raw_offset<volatile uint64_t*>(evtringseg, 8) = PAGESIZE / 0x10;
		//Write the event ring info
		primaryevt.dequeueptr = evtdp;
		primaryevt.ringbase = evtring;
		//Set segment table size
		Runtime.Interrupter(0).ERSTSZ.Size = 1;
		//Set dequeue pointer
		Runtime.Interrupter(0).ERDP.update(evt, false);
		//Enable event ring
		Runtime.Interrupter(0).ERSTBA.SegTableBase = evtseg;
		Runtime.Interrupter(0).IMAN = Runtime.Interrupter(0).IMAN;
	}



	void createDeviceContextBuffer()
	{
		paddr_t cont = pmmngr_allocate(1);
		devctxt = find_free_paging(PAGESIZE);
		paging_map(devctxt, cont, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING);
		memset(devctxt, 0, PAGESIZE);
		writeOperationalRegister(XHCI_OPREG_DCBAAP, cont, 64);
		//Operational.DCBAAP.DeviceContextPtr = cont;
		//Give XHCI a scratchpad
		uint32_t scratchsize = (Capabilities.HCSPARAMS2.MaxScratchHigh << 5) | Capabilities.HCSPARAMS2.MaxScratchLow;
		if (scratchsize > 512)
			kprintf(u"Warning: scratchpad too big!\n");
		paddr_t spad = pmmngr_allocate(1);
		*reinterpret_cast<volatile uint64_t*>(devctxt) = spad;
		Operational.CONFIG.EnabledDeviceSlots = Capabilities.HCSPARAMS1.MaxSlots;
		writeOperationalRegister(XHCI_OPREG_DNCTRL, 1 << 1, 32);
		void* mappedspa = find_free_paging(PAGESIZE);
		paging_map(mappedspa, spad, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING);
		for (size_t n = 0; n < scratchsize && n < 512; ++n)
		{
			paddr_t* spadi = (paddr_t*)mappedspa;
			spadi[n] = pmmngr_allocate(1);
			void* mappedspd = find_free_paging(PAGESIZE);
			paging_map(mappedspd, spadi[n], PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING);
			memset(mappedspd, 0, PAGESIZE);
			paging_free(mappedspd, PAGESIZE, false);
		}
		paging_free(mappedspa, PAGESIZE, false);
	}

	protocol_speed protocol_speeds[16];		//Indexed by PSIV

	size_t max_packet_size(uint8_t sp)
	{
		switch (sp)
		{
		case LOW_SPEED:
			return 8;
		case FULL_SPEED:
		case HIGH_SPEED:
			return 64;
		case SUPER_SPEED:
		case SUPER_SPEED_PLUS:
			return 512;
		default:
			return 512;
		}
	}


	size_t calculatePortSpeed(uint8_t speed)
	{
		size_t mul = 1;
		for (size_t i = 0; i < protocol_speeds[speed].rx.spd_exponent; ++i)
			mul *= 1000;
		return mul * protocol_speeds[speed].rx.spd_mantissa;
	}

	XHCI_PORT_SPEED portSpeed(uint8_t protocol)
	{
		if (protocol_speeds[protocol].rx.spd_exponent > 3)
		{
			kprintf(u"Warning: ChaiOS is not rated for Tbps USB 3\n");
			return SUPER_SPEED_PLUS;
		}
		else if (protocol_speeds[protocol].rx.spd_exponent == 3)
		{
			if (protocol_speeds[protocol].rx.spd_mantissa > 5)
				return SUPER_SPEED_PLUS;
			else
				return SUPER_SPEED;
		}
		else if (protocol_speeds[protocol].rx.spd_exponent == 2)
		{
			if (protocol_speeds[protocol].rx.spd_mantissa >= 480)
				return HIGH_SPEED;
			else
				return FULL_SPEED;
		}
		else
			return LOW_SPEED;
	}

	const char16_t* ReadablePortSpeed(XHCI_PORT_SPEED speed)
	{
		switch (speed)
		{
		case RESERVED:
			return u"Invalid";
		case FULL_SPEED:
			return u"Full Speed";
		case LOW_SPEED:
			return u"Low Speed";
		case HIGH_SPEED:
			return u"High Speed";
		case SUPER_SPEED:
			return u"SuperSpeed";
		case SUPER_SPEED_PLUS:
			return u"SuperSpeed+";
		default:
			return u"Unknown";
		}

	}

	semaphore_t event_available;


	spinlock_t tree_lock;
	RedBlackTree<void*, semaphore_t> WaitingCommands;
	RedBlackTree<paddr_t, void*> PendingCommands;
	RedBlackTree<void*, void*> CompletedCommands;

	spinlock_t port_tree_lock;
	RedBlackTree<uint32_t, semaphore_t> PortStatusChange;
	RedBlackTree<uint32_t, void*> CompletedPorts;

	void* waitComplete(void* commandtrb, size_t timeout)
	{
		semaphore_t waiter = create_semaphore(0, u"USB-XHCI Command Waiter");
		auto st = acquire_spinlock(tree_lock);
		auto it = CompletedCommands.find(commandtrb);
		if (it != CompletedCommands.end())
		{
			auto result = it->second;
			release_spinlock(tree_lock, st);
			return result;
		}
		WaitingCommands[commandtrb] = waiter;
		release_spinlock(tree_lock, st);
		//kprintf(u"Waiting for TRB %x, flags %x\n", commandtrb, get_debug_flags());
		if (wait_semaphore(waiter, 1, 50) == 0)
		{
			//An interrupt might not have been generated, poll TRB
			signal_semaphore(event_available, 1);
			auto time = (timeout - 50) < 0 ? 0 : (timeout - 50);
			if (wait_semaphore(waiter, 1, time) == 0)
			{
				st = acquire_spinlock(tree_lock);
				WaitingCommands.remove(commandtrb);
				release_spinlock(tree_lock, st);
				delete_semaphore(waiter);
				return nullptr;
			}
		}
		st = acquire_spinlock(tree_lock);
		void* result = CompletedCommands[commandtrb];
		CompletedCommands.remove(commandtrb);
		WaitingCommands.remove(commandtrb);
		release_spinlock(tree_lock, st);
		delete_semaphore(waiter);
		return result;
	}

	void* waitPortStatus(size_t port, size_t timeout)
	{
		semaphore_t waiter = create_semaphore(0, u"USB-XHCI Port Reset waiter");
		auto st = acquire_spinlock(port_tree_lock);
		auto it = CompletedPorts.find(port);
		if (it != CompletedPorts.end())
		{
			release_spinlock(port_tree_lock, st);
			return it->second;
		}
		PortStatusChange[port] = waiter;
		release_spinlock(port_tree_lock, st);
		if (wait_semaphore(waiter, 1, timeout) == 0)
			return nullptr;
		st = acquire_spinlock(port_tree_lock);
		void* result = CompletedPorts[port];
		CompletedPorts.remove(port);
		release_spinlock(port_tree_lock, st);
		return result;
	}

	bool AddressDevice(paddr_t incontext, uint16_t slotid, bool bsr = false)
	{
		//Issue address device command
		void* last_command = cmdring.enqueue(create_address_command(bsr, incontext, slotid));
		uint64_t* curevt = (uint64_t*)waitComplete(last_command, 1000);
		if (get_trb_completion_code(curevt) != XHCI_COMPLETION_SUCCESS)
		{
			kprintf(u"Error addressing device (BSR1): %x\n", get_trb_completion_code(curevt));
			return false;
		}
		return true;
	}

	void* AllocateSlotContext(paddr_t& paddr)
	{
		//Allocate input context
		paddr = pmmngr_allocate(1);
		void* mapincontxt = find_free_paging(PAGESIZE);
		paging_map(mapincontxt, paddr, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING);
		memset(mapincontxt, 0, PAGESIZE);
		return mapincontxt;
	}

	volatile xhci_device_context* CreateInputContext(paddr_t& paddr, uint32_t add, uint32_t drop, uint8_t config, uint8_t iface, uint8_t alternate)
	{
		void* mapincontxt = AllocateSlotContext(paddr);
		volatile uint32_t* dflags = raw_offset<volatile uint32_t*>(mapincontxt, 0);
		volatile uint32_t* aflags = raw_offset<volatile uint32_t*>(mapincontxt, 4);
		*aflags = add;
		*dflags = drop;

		volatile uint32_t* confep = raw_offset<volatile uint32_t*>(mapincontxt, 0x1C);
		*confep = config | (iface << 8) | (alternate << 16);

		uint16_t offset = 0x20 * (1 + Capabilities.HCCPARAMS1.CSZ);
		return raw_offset<volatile xhci_device_context*>(mapincontxt, offset);
	}

	bool createSlotContextOld(size_t portspeed, xhci_device_info* pinfo, uint32_t routeString)
	{
		//Allocate device context
		paddr_t dctxt = pmmngr_allocate(1);
		void* mapdctxt = find_free_paging(PAGESIZE);
		paging_map(mapdctxt, dctxt, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING);
		memset(mapdctxt, 0, PAGESIZE);
		//Write to device context array slot
		*raw_offset<volatile uint64_t*>(devctxt, pinfo->slotid * 8) = dctxt;

		//Allocate input context
		paddr_t incontext = pmmngr_allocate(1);
		void* mapincontxt = find_free_paging(PAGESIZE);
		paging_map(mapincontxt, incontext, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING);
		memset(mapincontxt, 0, PAGESIZE);
		//Set input control context
		volatile uint32_t* aflags = raw_offset<volatile uint32_t*>(mapincontxt, 4);
		*aflags = 3;

		//Setup slot context
		*raw_offset<volatile uint32_t*>(mapincontxt, 0x20) = MK_SLOT_CONTEXT_DWORD0(1, 0, 0, portspeed, routeString);
		*raw_offset<volatile uint32_t*>(mapincontxt, 0x20 + 4) = MK_SLOT_CONTEXT_DWORD1(0, pinfo->portindex, 0); //Root hub port number	
		//Initialise endpoint 0 context
		//Control endpoint, CErr = 3
		*raw_offset<volatile uint32_t*>(mapincontxt, 0x40) = MK_ENDPOINT_CONTEXT_DWORD0(0, 0, 0, 0, 0, 0);
		*raw_offset<volatile uint32_t*>(mapincontxt, 0x40 + 4) = MK_ENDPOINT_CONTEXT_DWORD1(max_packet_size(portspeed), 0, 0, 4, 3);
		paddr_t dtrb = pinfo->cmdring.getBaseAddress();
		*raw_offset<volatile uint32_t*>(mapincontxt, 0x40 + 8) = MK_ENDPOINT_CONTEXT_DWORD2(dtrb, 1);
		*raw_offset<volatile uint32_t*>(mapincontxt, 0x40 + 12) = MK_ENDPOINT_CONTEXT_DWORD3(dtrb);
		*raw_offset<volatile uint32_t*>(mapincontxt, 0x40 + 0x10) = MK_ENDPOINT_CONTEXT_DWORD4(0, 8);		//Average TRB length 8 for control

		//Apparently we write these to the device context
		memcpy(mapdctxt, raw_offset<void*>(mapincontxt, 0x20), 0x40);

		if (!AddressDevice(incontext, pinfo->slotid))
			return false;

		Stall(100);
		pinfo->device_context = (xhci_device_context*)mapdctxt;
		pinfo->phy_context = dctxt;

		/*last_command = cmdring.enqueue(create_address_command(false, incontext, pinfo->slotid));
		curevt = (uint64_t*)waitComplete(last_command, 1000);
		if (get_trb_completion_code(curevt) != XHCI_COMPLETION_SUCCESS)
		{
			kprintf(u"Error addressing device (NO BSR): %x\n", get_trb_completion_code(curevt));
			return false;
		}
		Stall(100);*/
		return true;
	}


	bool createSlotContext(size_t portspeed, xhci_device_info* pinfo, UsbDeviceInfo* parent, uint32_t routeString)
	{
		//Allocate device context
		paddr_t dctxt, incontext;
		void* mapdctxt = AllocateSlotContext(dctxt);
		//Write to device context array slot
		*raw_offset<volatile uint64_t*>(devctxt, pinfo->slotid * 8) = dctxt;

		//Allocate input context
		auto inctxt = CreateInputContext(incontext, 3, 0, 0, 0, 0);

		//Setup slot context
		*raw_offset<volatile uint32_t*>(inctxt, 0) = MK_SLOT_CONTEXT_DWORD0(1, 0, 0, portspeed, routeString);
		*raw_offset<volatile uint32_t*>(inctxt, 4) = MK_SLOT_CONTEXT_DWORD1(0, pinfo->portindex, 0); //Root hub port number	

		if (parent && portspeed < USB_DEVICE_HIGHSPEED)
		{
			//TT 
			auto xhdev = (xhci_device_info*)parent;
			inctxt->slot.TTHubSlotId = xhdev->slotid;
			inctxt->slot.TTPortNum = routeString & 0xF;
			inctxt->slot.TTT = 0;
		}
		//Initialise endpoint 0 context
		//Control endpoint, CErr = 3

		auto epdesc = GetEndpointContext(inctxt, 0, 0);
		*raw_offset<volatile uint32_t*>(epdesc, 0) = MK_ENDPOINT_CONTEXT_DWORD0(0, 0, 0, 0, 0, 0);
		*raw_offset<volatile uint32_t*>(epdesc, 4) = MK_ENDPOINT_CONTEXT_DWORD1(max_packet_size(portspeed), 0, 0, 4, 3);
		paddr_t dtrb = pinfo->cmdring.getBaseAddress();
		*raw_offset<volatile uint32_t*>(epdesc, 8) = MK_ENDPOINT_CONTEXT_DWORD2(dtrb, 1);
		*raw_offset<volatile uint32_t*>(epdesc,  12) = MK_ENDPOINT_CONTEXT_DWORD3(dtrb);
		*raw_offset<volatile uint32_t*>(epdesc, 0x10) = MK_ENDPOINT_CONTEXT_DWORD4(0, 8);		//Average TRB length 8 for control

		//Apparently we write these to the device context
		memcpy(mapdctxt, inctxt, 0x40);

		if (!AddressDevice(incontext, pinfo->slotid))
			return false;

		Stall(100);
		pinfo->device_context = (xhci_device_context*)mapdctxt;
		pinfo->phy_context = dctxt;
		/*last_command = cmdring.enqueue(create_address_command(false, incontext, pinfo->slotid));
		curevt = (uint64_t*)waitComplete(last_command, 1000);
		if (get_trb_completion_code(curevt) != XHCI_COMPLETION_SUCCESS)
		{
			kprintf(u"Error addressing device (NO BSR): %x\n", get_trb_completion_code(curevt));
			return false;
		}
		Stall(100);*/
		return true;
	}

	virtual USBHub& RootHub()
	{
		return pRootHub;
	}


	static size_t GetXhciEndpointIndex(uint8_t epNum, uint8_t dirIn)
	{
		if (epNum == 0)
			dirIn = 1;
		return epNum * 2 + dirIn;
	}

	usb_status_t xhci_control_in(xhci_device_info* pinfo, const REQUEST_PACKET* request, void* dest, const size_t len)
	{
		xhci_command** devcmds = new xhci_command*[5];
		devcmds[0] = create_setup_stage_trb(request->request_type, request->request, request->value, request->index, request->length, 3);		//IN data stage
		devcmds[1] = create_data_stage_trb(get_physical_address(dest), len, true);
		//devcmds[2] = create_event_data_trb(get_physical_address(dest));
		devcmds[2] = create_status_stage_trb(true);
		devcmds[3] = nullptr;

		if (len == 0)
		{
			devcmds[1] = devcmds[2];
			devcmds[2] = nullptr;
		}

		void* statusevt = pinfo->cmdring.enqueue_commands(devcmds);
		void* res = waitComplete(statusevt, 1000);

		size_t result = get_trb_completion_code(res);
		if (result == XHCI_COMPLETION_SUCCESS)
			return USB_SUCCESS;
		return USB_XHCI_TRB_ERR(result);

		/*devcmds[0] = create_status_stage_trb(true);
		devcmds[1] = nullptr;
		statusevt = pinfo->cmdring.enqueue_commands(devcmds);
		res = waitComplete(statusevt, 1000);
		result = get_trb_completion_code(res);
		if (result != XHCI_COMPLETION_SUCCESS)
		{
			return (result == 0 ? 0x100 : result);
		}
		delete[] devcmds;*/

		return 0;
	}
private:
	void* baseaddr;
	void* oppbase;
	void* runbase;
	void* doorbell;
	void* devctxt;
	CommandRing cmdring;
	xhci_evtring_info primaryevt;
	xhci_port_info_data* pPortInfo;
	HTHREAD evtthread;
	bool interrupt_msi;
	XhciRootHub pRootHub;

	friend static void xhci_pci_baseaddr(pci_address* addr, XHCI*& cinfo);
};

static uint8_t xhci_interrupt(size_t vector, void* info)
{
	XHCI* inf = reinterpret_cast<XHCI*>(info);
	if (inf->event_available)
	{
		signal_semaphore(inf->event_available, 1);
	}
	if(!inf->interrupt_msi)
		inf->Operational.USBSTS.EINT = 1;
	inf->Runtime.Interrupter(0).IMAN.InterruptPending = 1;
	inf->Runtime.Interrupter(0).IMAN.InterruptEnable = 1;
	return 1;
}

static void xhci_pci_baseaddr(pci_address* addr, XHCI*& cinfo)
{
	paddr_t baseaddr;
	uint64_t barsize;
	//Enable interrupts, memory space, and bus mastering
	uint64_t commstat;
	read_pci_config(addr->segment, addr->bus, addr->device, addr->function, 1, 32, &commstat);
	commstat |= (1 << 10);	//Mask pinned interrupts
	commstat |= 0x6;		//Memory space and bus mastering
	write_pci_config(addr->segment, addr->bus, addr->device, addr->function, 1, 32, commstat);
	//Write FLADJ incase BIOS didn't
	write_pci_config(addr->segment, addr->bus, addr->device, addr->function, 0x61, 8, 0x20);
	baseaddr = read_pci_bar(addr->segment, addr->bus, addr->device, addr->function, 0, &barsize);
	void* mapped_controller = find_free_paging(barsize);
	if (!paging_map(mapped_controller, baseaddr, barsize, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING))
	{
		kprintf(u"Mapping failed\n");
	}
	cinfo = new XHCI(mapped_controller);
	uint32_t vector = PciAllocateMsi(addr->segment, addr->bus, addr->device, addr->function, 1, &xhci_interrupt, cinfo);
	if (vector == -1)
	{
		kprintf(u"Error: no MSI(-X) support\n");
	}
	else
	{
		cinfo->set_msi_mode(true);
	}
}

void xhci_init()
{
	register_pci_driver(&xhci_pci_reg);
}