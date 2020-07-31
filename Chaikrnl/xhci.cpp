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


typedef RedBlackTree<size_t, pci_address*> XhciPci;

static XhciPci pcicontrollers;
static size_t handle_alloc = 0;

struct xhci_evtring_info {
	void* ringbase;
	void* dequeueptr;
};

static bool xhci_pci_scan(uint16_t segment, uint16_t bus, uint16_t device, uint8_t function)
{
	if (pci_get_classcode(segment, bus, device, function) == 0x0C0330)
	{
		//XHCI device
		uint64_t rev_reg = 0;
		read_pci_config(segment, bus, device, function, 0x18, 8, &rev_reg);
		size_t handle = ++handle_alloc;
		kprintf_a("Found XHCI USB controller (handle %x) at %d:%d:%d:%d\n", handle, segment, bus, device, function);
		kprintf_a(" USB version %d.%d\n", rev_reg >> 4, rev_reg & 0xF);
		pci_address* addr = new pci_address;
		addr->segment = segment; addr->bus = bus; addr->device = device; addr->function = function;
		pcicontrollers[handle] = addr;
	}
	return false;
}

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

static size_t pow2(size_t p)
{
	size_t result = 1;
	while (p--)
		result *= 2;
	return result;
}

static size_t get_trb_completion_code(void* trb)
{
	if (!trb)
		return 0;
	return (((uint64_t*)trb)[1] >> 24) & 0xFF;
}

#pragma pack(push, 1)
typedef struct _usb_device_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t bcdUSB;
	uint8_t bDeviceClass;
	uint8_t bDeviceSublass;
	uint8_t bDeviceProtocol;
	uint8_t bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t iManufacturer;
	uint8_t iProduct;
	uint8_t iSerialNumber;
	uint8_t bNumConfigurations;
}usb_device_descriptor;
#pragma pack(pop)

static auto get_debug_flags()
{
	auto flags = arch_disable_interrupts();
	arch_restore_state(flags);
	return flags;
}

class XHCI {
public:
	XHCI(void* basea)
		:cmdring(this, XHCI_DOORBELL_HOST, XHCI_DOORBELL_HOST_COMMAND),
		Capabilities(baseaddr),
		Operational(oppbase),
		Runtime(runbase)
	{
		baseaddr = basea;
		event_available = 0;
		tree_lock = 0;
		port_tree_lock = 0;
		interrupt_msi = false;
	}
	void init(size_t handle)
	{
		kprintf_a("Initalizing XHCI controller %x\n", handle);
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
		for (size_t n = 1; n <= getMaxPorts(); ++n)
		{
			xhci_thread_port_startup* sup = new xhci_thread_port_startup;
			sup->cinfo = this;
			sup->port = n;
			//create_thread(&xhci_port_startup, sup);
			xhci_port_startup(sup);
		}
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

	void port_reset(size_t portindex)
	{
		static const uint32_t xHC_PortUSB_CHANGE_BITS = ((1 << 17) | (1 << 18) | (1 << 20) | (1 << 21) | (1 << 22));
		XhciPortRegisterBlock portregs = Operational.Port(portindex);

		//Clear change bits
		uint32_t HCPortStatusOff = XHCI_OPREG_PORTSC(portindex);
		writeOperationalRegister(HCPortStatusOff, xHC_PortUSB_CHANGE_BITS | (1 << 9), 32);

		if (portregs.PORTSC.CCS == 1)
		{
			if (portregs.PORTSC.PED == 0)
			{
				kprintf(u"Resetting Port %d\n", portindex);
				//Reset port
				portregs.PORTSC.PortReset = 1;
				while (portregs.PORTSC.PortReset != 0);

				if (((uint32_t)portregs.PORTSC) & (1 << 1))
				{
					writeOperationalRegister(HCPortStatusOff, xHC_PortUSB_CHANGE_BITS | (1 << 9), 32);
				}
			}
		}
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
				uint32_t psic = readCapabilityRegister(supportp + 8, 32) >> 28;
				uint32_t slott = readCapabilityRegister(supportp + 0xC, 32) & 0x1F;
				//kprintf_a("%s%x.%02x, %d descriptors, slot type %d\n", u.usb_str, revision >> 8, revision & 0xFF, psic, slott);
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
		kprintf(u"XHCI supported protocols loaded\n");
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
	void xhci_port_startup(void* pinf)
	{
		XHCI* cinfo = ((xhci_thread_port_startup*)pinf)->cinfo;
		size_t n = ((xhci_thread_port_startup*)pinf)->port;

		XhciPortRegisterBlock portregs = Operational.Port(n);
		if (portregs.PORTSC.CCS == 1)
		{
			port_reset(n);
			//Port successfully reset
			uint8_t portspeed = portregs.PORTSC.PortSpeed;
			kprintf(u" Device attatched on port %d, %d Kbps (%s)\n", n, calculatePortSpeed(portspeed) / 1000, ReadablePortSpeed(portSpeed(portspeed)));
			Stall(100);
			//Enable slot command
			void* last_command = nullptr;
			last_command = cmdring.enqueue(create_enableslot_command(protocol_speeds[portspeed].slottype));
			//Now get a command completion event from the event ring
			//liballoc_dump();
			uint64_t* curevt = (uint64_t*)waitComplete(last_command, 1000);
			if (get_trb_completion_code(curevt) != XHCI_COMPLETION_SUCCESS)
			{
				kprintf(u"Error enabling slot!\n");
				return;
			}
			uint8_t slotid = curevt[1] >> 56;
			if (slotid == 0)
				return;
			//kprintf(u"  DSE command complete: %d\n", slotid);
			xhci_port_info* port_info = new xhci_port_info(this, slotid);
			port_info->controller = cinfo;
			port_info->portindex = n;
			port_info->slotid = slotid;
			if (!createSlotContext(portspeed, port_info))
				return;
#if 1
			//Now we get device descriptor
			xhci_command** devcmds = new xhci_command*[4];
			devcmds[0] = create_setup_stage_trb(USB_BM_REQUEST_INPUT | USB_BM_REQUEST_STANDARD | USB_BM_REQUEST_DEVICE,
				USB_BREQUEST_GET_DESCRIPTOR, USB_DESCRIPTOR_WVALUE(USB_DESCRIPTOR_DEVICE, 0), 0, 18, 3);
			volatile usb_device_descriptor* devdesc = (usb_device_descriptor*)new uint8_t[64];
			devcmds[1] = create_data_stage_trb(get_physical_address((void*)devdesc), 18, true);
			devcmds[2] = create_status_stage_trb(true);
			devcmds[3] = nullptr;
			//port_info->cmdring.enqueue(devcmds[0]);
			void* statusevt = port_info->cmdring.enqueue_commands(devcmds);
			void* resulttrb = waitComplete(statusevt, 10000);
			if (get_trb_completion_code(resulttrb) != XHCI_COMPLETION_SUCCESS)
			{
				kprintf(u"Error getting device descriptor (code %d)\n", get_trb_completion_code(resulttrb));
				return;
			}
			if (devdesc->iManufacturer != 0)
			{
				//Get device string
				devcmds[0] = create_setup_stage_trb(USB_BM_REQUEST_INPUT | USB_BM_REQUEST_STANDARD | USB_BM_REQUEST_DEVICE, 
					USB_BREQUEST_GET_DESCRIPTOR, USB_DESCRIPTOR_WVALUE(USB_DESCRIPTOR_STRING, devdesc->iManufacturer), 0, 256, 3);
				volatile wchar_t* devstr = new wchar_t[256];
				devcmds[1] = create_data_stage_trb(get_physical_address((void*)devstr), 256, true);
				devcmds[2] = create_status_stage_trb(true);
				devcmds[3] = nullptr;
				statusevt = port_info->cmdring.enqueue_commands(devcmds);
				resulttrb = waitComplete(statusevt, 1000);
				kprintf(u"   Device vendor %s\n", ++devstr);
			}
			if (devdesc->iProduct != 0)
			{
				//Get device string
				devcmds[0] = create_setup_stage_trb(USB_BM_REQUEST_INPUT | USB_BM_REQUEST_STANDARD | USB_BM_REQUEST_DEVICE,
					USB_BREQUEST_GET_DESCRIPTOR, USB_DESCRIPTOR_WVALUE(USB_DESCRIPTOR_STRING, devdesc->iProduct), 0, 256, 3);
				volatile wchar_t* devstr = new wchar_t[256];
				devcmds[1] = create_data_stage_trb(get_physical_address((void*)devstr), 256, true);
				devcmds[2] = create_status_stage_trb(true);
				devcmds[3] = nullptr;
				statusevt = port_info->cmdring.enqueue_commands(devcmds);
				resulttrb = waitComplete(statusevt, 1000);
				kprintf(u"   %s\n", ++devstr);
			}
			
			{
				kprintf_a("   Device %x:%x class %x:%x:%x USB version %x\n", devdesc->idVendor, devdesc->idProduct, devdesc->bDeviceClass, devdesc->bDeviceSublass, devdesc->bDeviceProtocol, devdesc->bcdUSB);
			}
			//devdesc = raw_offset<volatile usb_device_descriptor*>(devdesc, 64);
#endif
		}
	}

	class CommandRing {
	public:
		CommandRing(XHCI* parent, size_t slot, size_t endpoint)
			: m_parent(parent), m_slot(slot), m_endpoint(endpoint)
		{
			ring_lock = create_spinlock();
			m_ringbase = m_enqueue = pmmngr_allocate(1);
			m_mapped_enqueue = find_free_paging(PAGESIZE);
			paging_map(m_mapped_enqueue, m_enqueue, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING);
			memset(m_mapped_enqueue, 0, PAGESIZE);
			slotbit = XHCI_TRB_ENABLED;
		}
		void* enqueue(xhci_command* command)
		{
			auto st = acquire_spinlock(m_parent->tree_lock);
			m_parent->PendingCommands[m_enqueue] = command;
			release_spinlock(m_parent->tree_lock, st);
			st = acquire_spinlock(ring_lock);
			//Write command
			m_mapped_enqueue = place_trb(m_mapped_enqueue, command->lowval, command->highval, m_enqueue);
			m_parent->ringDoorbell(m_slot, m_endpoint);
			release_spinlock(ring_lock, st);
			delete command;
			return command;
		}

		void* enqueue_commands(xhci_command** commands)
		{
			auto st = acquire_spinlock(ring_lock);
			//Write command
			size_t n = 0;
			while (commands[n])
			{
				paddr_t lastcmd = m_enqueue;
				m_mapped_enqueue = place_trb(m_mapped_enqueue, commands[n]->lowval, commands[n]->highval, m_enqueue);
				auto st2 = acquire_spinlock(m_parent->tree_lock);
				m_parent->PendingCommands[lastcmd] = commands;
				release_spinlock(m_parent->tree_lock, st2);
				delete commands[n];
				++n;
			}
			m_parent->ringDoorbell(m_slot, m_endpoint);
			release_spinlock(ring_lock, st);
			return commands;
		}

		paddr_t getBaseAddress()
		{
			return m_ringbase;
		}

		static void* xhci_write_crb(void* crbc, uint64_t low, uint64_t high, paddr_t& penq)
		{
			//kprintf(u"Writing TRB to %x(%x) <- %x:%x\n", crbc, penq, high, low);
			*raw_offset<volatile uint64_t*>(crbc, 8) = high;
			arch_memory_barrier();
			*raw_offset<volatile uint64_t*>(crbc, 0) = low;
			penq += 0x10;
			return raw_offset<void*>(crbc, 16);
		}
	private:
		void* place_trb(void* crbc, uint64_t low, uint64_t high, paddr_t& penq)
		{
			void* ret = xhci_write_crb(crbc, low, high | slotbit, penq);
			if (penq == m_ringbase + PAGESIZE)
			{
				penq = m_ringbase;
				slotbit = (slotbit == 0) ? XHCI_TRB_ENABLED : 0;
				ret = raw_offset<void*>(ret, -PAGESIZE);
			}
			return ret;
		}
		XHCI* const m_parent;
		spinlock_t ring_lock;
		paddr_t m_enqueue;
		paddr_t m_ringbase;
		void* m_mapped_enqueue;
		size_t m_slot;
		size_t m_endpoint;
		uint64_t slotbit;
	};

	class xhci_port_info {
	public:
		xhci_port_info(XHCI* parent, size_t slot)
			:cmdring(parent, slot, XHCI_DOORBELL_ENPOINT0)
		{
			controller = parent;
			command_lock = create_spinlock();
		}
		XHCI* controller;
		uint32_t portindex;
		uint32_t slotid;
		CommandRing cmdring;
		void* device_context;
		spinlock_t command_lock;
	};

	

	bool update_packet_size_fs(xhci_port_info* port)
	{
		uint8_t* buffer = new uint8_t[8];
		xhci_command** devcmds = new xhci_command*[4];
		devcmds[0] = create_setup_stage_trb(0x80, 6, 0x100, 0, 8, 3);
		devcmds[1] = create_data_stage_trb(get_physical_address(buffer), 8, true);
		devcmds[2] = create_status_stage_trb(true);
		devcmds[3] = nullptr;
		void* statusevt = port->cmdring.enqueue_commands(devcmds);
		void* res = waitComplete(statusevt, 1000);
		if (get_trb_completion_code(res) != XHCI_COMPLETION_SUCCESS)
		{
			kprintf(u"Error getting FullSpeed packet size: %d\n", get_trb_completion_code(res));
			return false;
		}
		//Update information
		usb_device_descriptor* devdes = (usb_device_descriptor*)buffer;
		if (devdes->bMaxPacketSize0 != 8)
		{
			//Create input context
			//Allocate input context
			paddr_t incontext = pmmngr_allocate(1);
			void* mapincontxt = find_free_paging(PAGESIZE);
			paging_map(mapincontxt, incontext, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING);
			memset(mapincontxt, 0, PAGESIZE);
			//Set input control context
			volatile uint32_t* aflags = raw_offset<volatile uint32_t*>(mapincontxt, 4);
			*aflags = 2;
			//Copy existing data structures
			memcpy(raw_offset<void*>(mapincontxt, 0x20), port->device_context, 0x20 * 2);
			*raw_offset<uint16_t*>(mapincontxt, 0x40 + 0x6) = devdes->bMaxPacketSize0;
			//Notify xHCI
			void* lastcmd = cmdring.enqueue(create_evaluate_command(incontext, port->slotid));
			res = waitComplete(lastcmd, 1000);
			if (get_trb_completion_code(res) != XHCI_COMPLETION_SUCCESS)
			{
				kprintf(u"Error updating device context\n");
				return false;
			}
		}
		return true;
	}

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
		case FULL_SPEED:
			return 8;
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

	bool createSlotContext(size_t portspeed, xhci_port_info* pinfo)
	{
		//Allocate input context
		paddr_t incontext = pmmngr_allocate(1);
		void* mapincontxt = find_free_paging(PAGESIZE);
		paging_map(mapincontxt, incontext, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING);
		memset(mapincontxt, 0, PAGESIZE);
		//Set input control context
		volatile uint32_t* aflags = raw_offset<volatile uint32_t*>(mapincontxt, 4);
		*aflags = 3;
		//Setup slot context
		*raw_offset<volatile uint32_t*>(mapincontxt, 0x20) = (1 << 27) | (portspeed << 20);
		*raw_offset<volatile uint32_t*>(mapincontxt, 0x20 + 4) = (pinfo->portindex << 16); //Root hub port number	
		//Initialise endpoint 0 context
		//Control endpoint, CErr = 3
		*raw_offset<volatile uint32_t*>(mapincontxt, 0x40 + 4) = (4 << 3) | (max_packet_size(portspeed) << 16) | (3 << 1);
		paddr_t dtrb = pinfo->cmdring.getBaseAddress();
		*raw_offset<volatile uint32_t*>(mapincontxt, 0x40 + 8) = (dtrb & UINT32_MAX) | 1;
		*raw_offset<volatile uint32_t*>(mapincontxt, 0x40 + 12) = dtrb >> 32;
		*raw_offset<volatile uint32_t*>(mapincontxt, 0x40 + 0x10) = PAGESIZE / 0x10;

		//Allocate device context
		paddr_t dctxt = pmmngr_allocate(1);
		void* mapdctxt = find_free_paging(PAGESIZE);
		paging_map(mapdctxt, dctxt, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING);
		memset(mapdctxt, 0, PAGESIZE);
		//Write to device context array slot
		*raw_offset<volatile uint64_t*>(devctxt, pinfo->slotid * 8) = dctxt;
		//Issue address device command
		void* last_command = cmdring.enqueue(create_address_command(false, incontext, pinfo->slotid));
		uint64_t* curevt = (uint64_t*)waitComplete(last_command, 1000);
		if (get_trb_completion_code(curevt) != XHCI_COMPLETION_SUCCESS)
		{
			kprintf(u"Error addressing device\n");
			return false;
		}
		Stall(100);
		pinfo->device_context = mapdctxt;

		//If the device is full speed, we need to work out the correct packet size
		if (portSpeed(portspeed) == FULL_SPEED)
		{
			//kprintf(u"Full speed device, calculating packet size\n");
			if (!update_packet_size_fs(pinfo))
				return false;
		}

#if 0
		last_command = cmdring.enqueue(create_address_command(false, incontext, pinfo->slotid));
		curevt = (uint64_t*)waitComplete(last_command, 1000);
		if (get_trb_completion_code(curevt) != XHCI_COMPLETION_SUCCESS)
		{
			kprintf(u"Error addressing device\n");
			return false;
		}
#endif
		return true;
	}
private:
	void* baseaddr;
	void* oppbase;
	void* runbase;
	void* doorbell;
	void* devctxt;
	CommandRing cmdring;
	xhci_evtring_info primaryevt;
	HTHREAD evtthread;
	bool interrupt_msi;
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
	uint32_t vector = pci_allocate_msi(addr->segment, addr->bus, addr->device, addr->function, 1, &xhci_interrupt, cinfo);
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
	handle_alloc = 0;
	pci_bus_scan(&xhci_pci_scan);
	//Now init controllers
	for (auto it = pcicontrollers.begin(); it != pcicontrollers.end(); ++it)
	{
		XHCI* controller = nullptr;
		xhci_pci_baseaddr(it->second, controller);
		controller->init(it->first);
	}
}