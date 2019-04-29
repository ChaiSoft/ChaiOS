#include <usb.h>
#include <pciexpress.h>
#include <redblack.h>
#include <kstdio.h>
#include <arch/paging.h>
#include <arch/cpu.h>
#include <string.h>
#include <scheduler.h>
#include <semaphore.h>

typedef RedBlackTree<size_t, pci_address*> XhciPci;

static XhciPci pcicontrollers;
static size_t handle_alloc = 0;

typedef struct xhci_cmdring_info {
	void* ringbase;
	void* enqueue;
	paddr_t pbase;
	paddr_t penqueue;
};

typedef struct xhci_evtring_info {
	void* ringbase;
	void* dequeueptr;
};

typedef struct xhci_command {
	uint64_t lowval;
	uint64_t highval;
};

#define USE_COMMAND_THREADS 0

struct xhci_controller_info {
	void* baseaddr;
	void* oppbase;
	void* runbase;
	void* doorbell;
	void* devctxt;
	xhci_cmdring_info cmdring;
	xhci_evtring_info primaryevt;
	HTHREAD evtthread;
	HTHREAD cmdthread;
#if USE_COMMAND_THREADS
	semaphore_t command_available;
	semaphore_t command_buffer_lock;
	xhci_command** cmd_buffer;
	xhci_command** enqueue_cmd;
#else
	spinlock_t command_lock;
#endif
	size_t cmd_buf_size;
};

struct xhci_port_info {
	xhci_controller_info* controller;
	uint32_t portindex;
	uint32_t slotid;
	xhci_cmdring_info cmdring;
	void* device_context;
	HTHREAD cmdthread;
#if USE_COMMAND_THREADS
	semaphore_t command_available;
	semaphore_t command_buffer_lock;
	xhci_command** cmd_buffer;
	xhci_command** enqueue_cmd;
#else
	spinlock_t command_lock;
#endif
	size_t cmd_buf_size;
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

static void* xhci_pci_baseaddr(pci_address* addr, xhci_controller_info*& cinfo)
{
	paddr_t baseaddr;
	uint64_t barsize;
	//Enable interrupts, memory space, and bus mastering
	uint64_t commstat;
	read_pci_config(addr->segment, addr->bus, addr->device, addr->function, 1, 32, &commstat);
	commstat |= (1 << 10);	//Mask pinned interrupts
	commstat |= 0x6;		//Memory space and bus mastering
	write_pci_config(addr->segment, addr->bus, addr->device, addr->function, 1, 32, commstat);
	cinfo = new xhci_controller_info;
	baseaddr = read_pci_bar(addr->segment, addr->bus, addr->device, addr->function, 0, &barsize);
	void* mapped_controller = find_free_paging(barsize);
	cinfo->baseaddr = mapped_controller;
	if (!paging_map(mapped_controller, baseaddr, barsize, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING))
	{
		kprintf(u"Mapping failed\n");
	}
	uint32_t vector = pci_allocate_msi(addr->segment, addr->bus, addr->device, addr->function, 1);
	if (vector == -1)
	{
		kprintf(u"Error: no MSI(-X) support\n");
	}
	else
	{
		arch_register_interrupt_handler(INTERRUPT_SUBSYSTEM_DISPATCH, vector, &xhci_interrupt, cinfo);
	}
	return mapped_controller;
}

static uint64_t xhci_read_register(void* base, size_t reg, size_t width)
{
	switch (width)
	{
	case 8:
		return *raw_offset<volatile uint8_t*>(base, reg);
	case 16:
		return *raw_offset<volatile uint16_t*>(base, reg);
	case 32:
		return *raw_offset<volatile uint32_t*>(base, reg);
	case 64:
		return *raw_offset<volatile uint64_t*>(base, reg);
	}
	return 0;
}

static void xhci_write_register(void* base, size_t reg, uint64_t value, size_t width)
{
	switch (width)
	{
	case 8:
		*raw_offset<volatile uint8_t*>(base, reg) = value;
		break;
	case 16:
		*raw_offset<volatile uint16_t*>(base, reg) = value;
		break;
	case 32:
		*raw_offset<volatile uint32_t*>(base, reg) = value;
		break;
	case 64:
		*raw_offset<volatile uint64_t*>(base, reg) = value;
		break;
	}
}

#define XHCI_CAPREG_CAPLENGTH 0x0
#define XHCI_CAPREG_HCIVERSION 0x2
#define XHCI_CAPREG_HCSPARAMS1 0x4
#define XHCI_CAPREG_HCSPARAMS2 0x8
#define XHCI_CAPREG_HCSPARAMS3 0xC
#define XHCI_CAPREG_HCCPARAMS1 0x10
#define XHCI_CAPREG_DBOFF 0x14
#define XHCI_CAPREG_RTSOFF 0x18
#define XHCI_CAPREG_HCCPARAMS2 0x1C

#define XHCI_OPREG_USBCMD 0x0
#define XHCI_OPREG_USBSTS 0x4
#define XHCI_OPREG_PAGESIZE 0x8
#define XHCI_OPREG_DNCTRL 0x14
#define XHCI_OPREG_CRCR 0x18
#define XHCI_OPREG_DCBAAP 0x30
#define XHCI_OPREG_CONFIG 0x38

#define XHCI_OPREG_PORTSCBASE 0x400
#define XHCI_OPREG_PORTPMSCBASE 0x404
#define XHCI_OPREG_PORTLIBASE 0x408
#define XHCI_OPREG_PORTHLPMCBASE 0x40C
#define XHCI_PORTOFFSET 0x10

#define XHCI_OPREG_PORTSC(x) \
(XHCI_OPREG_PORTSCBASE + (x-1) * XHCI_PORTOFFSET)
#define XHCI_OPREG_PORTPMSC(x) \
(XHCI_OPREG_PORTPMSCBASE + (x-1) * XHCI_PORTOFFSET)
#define XHCI_OPREG_PORTLI(x) \
(XHCI_OPREG_PORTLIBASE + (x-1) * XHCI_PORTOFFSET)
#define XHCI_OPREG_PORTHLPMC(x) \
(XHCI_OPREG_PORTHLPMCBASE + (x-1) * XHCI_PORTOFFSET)

#define XHCI_PORTSC_SPEED_GET(x) \
((x >> 10) & 0xF)

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

static protocol_speed protocol_speeds[16];		//Indexed by PSIV

static uint8_t max_packet_size(uint8_t sp)
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


static size_t ReadablePortSpeed(uint8_t speed)
{
	size_t mul = 1;
	for (size_t i = 0; i < protocol_speeds[speed].rx.spd_exponent; ++i)
		mul *= 1000;
	return mul * protocol_speeds[speed].rx.spd_mantissa;
}

#define XHCI_RUNREG_MFINDEX 0x0
#define XHCI_RUNREG_IMANBASE 0x20
#define XHCI_RUNREG_IMODBASE 0x24
#define XHCI_RUNREG_ERSTSZBASE 0x28
#define XHCI_RUNREG_ERSTBABASE 0x30
#define XHCI_RUNREG_ERDPBASE 0x38
#define XHCI_INTERRUPTEROFFSET 0x20

#define XHCI_RUNREG_IMAN(x) \
(XHCI_RUNREG_IMANBASE + x*XHCI_INTERRUPTEROFFSET)
#define XHCI_RUNREG_IMOD(x) \
(XHCI_RUNREG_IMODBASE + x*XHCI_INTERRUPTEROFFSET)
#define XHCI_RUNREG_ERSTSZ(x) \
(XHCI_RUNREG_ERSTSZBASE + x*XHCI_INTERRUPTEROFFSET)
#define XHCI_RUNREG_ERSTBA(x) \
(XHCI_RUNREG_ERSTBABASE + x*XHCI_INTERRUPTEROFFSET)
#define XHCI_RUNREG_ERDP(x) \
(XHCI_RUNREG_ERDPBASE + x*XHCI_INTERRUPTEROFFSET)

#define XHCI_ECAP_LEGSUP 1
#define XHCI_ECAP_SUPPORT 2

#define XHCI_TRB_TYPE_NOOP 8
#define XHCI_TRB_TYPE_ENABLE_SLOT 9
#define XHCI_TRB_TYPE_ADDRESS_DEVICE 11
#define XHCI_TRB_TYPE_EVALUATE_CONTEXT 13
#define XHCI_TRB_TYPE_TRANSFER_EVENT 32
#define XHCI_TRB_TYPE_COMMAND_COMPLETE 33
#define XHCI_TRB_TYPE_PORT_STATUS_CHANGE 34
#define XHCI_TRB_TYPE_SETUP_STAGE 2
#define XHCI_TRB_TYPE_DATA_STAGE 3
#define XHCI_TRB_TYPE_STATUS_STAGE 4

#define XHCI_TRB_TYPE(x) ((uint64_t)x << 42)
#define XHCI_TRB_SLOTID(x) ((uint64_t)x << 56)

#define XHCI_GET_TRB_TYPE(x) \
((x >> 42) & 0x3F)

#define XHCI_TRB_ADDRESS_BSR ((uint64_t)1<<41)
#define XHCI_TRB_ENABLED 0x100000000
#define XHCI_TRB_ENT 0x200000000
#define XHCI_TRB_ISP 0x400000000
#define XHCI_TRB_IOC 0x2000000000
#define XHCI_TRB_IDT 0x4000000000
#define XHCI_TRB_TRT(x) ((uint64_t)x << 48)
#define XHCI_TRB_DIR_IN ((uint64_t)1 << 48)

#define XHCI_DOORBELL_HOST 0x0
#define XHCI_DOORBELL_HOST_COMMAND 0
#define XHCI_DOORBELL_ENPOINT0 1

#define XHCI_DOORBELL(slot) \
(XHCI_DOORBELL_HOST+slot*4)

#define XHCI_COMPLETION_SUCCESS 1

#pragma pack(push, 1)
struct xhci_ecap {
	uint8_t capid;
	uint8_t next;
	uint16_t data;
};
#pragma pack(pop)

static uint8_t ecap_get_next(void* capbase, uint32_t ecap)
{
	return (xhci_read_register(capbase, ecap, 32) >> 8) & 0xFF;
}

static uint8_t ecap_get_id(void* capbase, uint32_t ecap)
{
	return (xhci_read_register(capbase, ecap, 32)) & 0xFF;
}

static uint32_t xhci_find_extended_cap(void* capbase, uint8_t capid, uint32_t searchstart = 0)
{
	uint32_t ptr = 0;
	if (searchstart == 0)
	{
		ptr = (xhci_read_register(capbase, XHCI_CAPREG_HCCPARAMS1, 32) >> 16) * 4;
	}
	else
	{
		ptr = ecap_get_next(capbase, searchstart) * 4;
		if (ptr == 0)
			return 0;
		else
			ptr += searchstart;
	}
	for (; ptr ; ptr += ecap_get_next(capbase, ptr)*4)
	{
		if (ecap_get_id(capbase, ptr) == capid)
			return ptr;
		if (ecap_get_next(capbase, ptr) == 0)
			ptr = 0;
	}
	return 0;
}

static void* xhci_write_crb(void* crbc, uint64_t low, uint64_t high, paddr_t& penq)
{
	*raw_offset<volatile uint64_t*>(crbc, 8) = high;
	*raw_offset<volatile uint64_t*>(crbc, 0) = low;
	penq += 0x10;
	return raw_offset<void*>(crbc, 16);
}

static size_t pow2(size_t p)
{
	size_t result = 1;
	while (p--)
		result *= 2;
	return result;
}

semaphore_t event_available;

static uint8_t xhci_interrupt(size_t vector, void* info)
{
	xhci_controller_info* inf = reinterpret_cast<xhci_controller_info*>(info);
	signal_semaphore(event_available, 1);
	uint32_t status = xhci_read_register(inf->oppbase, XHCI_OPREG_USBSTS, 32);
	uint32_t temp = xhci_read_register(inf->runbase, XHCI_RUNREG_IMAN(0), 32);
	xhci_write_register(inf->oppbase, XHCI_OPREG_USBSTS, status, 32);
	xhci_write_register(inf->runbase, XHCI_RUNREG_IMAN(0), temp, 32);
	return 1;
}


spinlock_t tree_lock;
RedBlackTree<void*, semaphore_t> WaitingCommands;
RedBlackTree<paddr_t, void*> PendingCommands;
RedBlackTree<void*, void*> CompletedCommands;

spinlock_t port_tree_lock;
RedBlackTree<uint32_t, semaphore_t> PortStatusChange;
RedBlackTree<uint32_t, void*> CompletedPorts;

static void xhci_event_thread(void* param)
{
	xhci_controller_info* cinfo = reinterpret_cast<xhci_controller_info*>(param);
	while (1)
	{
		wait_semaphore(event_available, 1, TIMEOUT_INFINITY);
		volatile uint64_t* ret = (volatile uint64_t*)cinfo->primaryevt.dequeueptr;
		uint64_t erdp = xhci_read_register(cinfo->runbase, XHCI_RUNREG_ERDP(0), 64);
		while (ret[1] & XHCI_TRB_ENABLED)
		{
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
						signal_semaphore(waiter, 1);
					}
					else
					{
					}
				}
				release_spinlock(tree_lock, st);
#if 0
				if (XHCI_GET_TRB_TYPE(ret[1]) == XHCI_TRB_TYPE_TRANSFER_EVENT)
				{
					kprintf(u"Event for rethandle %x (TRB %x)\n", waiting_handle, cmd_trb);
				}
#endif
			}
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
			ret += 2;
			erdp += 0x10;		//next TRB
			cinfo->primaryevt.dequeueptr = (void*)ret;
			erdp |= (1 << 3);
			xhci_write_register(cinfo->runbase, XHCI_RUNREG_ERDP(0), erdp, 64);
		}
		
	}
}

#if USE_COMMAND_THREADS
static void xhci_command_thread(void* param)
{
	xhci_controller_info* cinfo = reinterpret_cast<xhci_controller_info*>(param);
	xhci_command** dequeue_ptr = cinfo->cmd_buffer;
	paddr_t command_paddr = cinfo->cmdring.pbase;
	void* cmdtrb_enq = cinfo->cmdring.ringbase;
	while (1)
	{
		wait_semaphore(cinfo->command_available, 1, TIMEOUT_INFINITY);
		xhci_command* curr = *dequeue_ptr;
		//Write command
		PendingCommands[command_paddr] = curr;
		cmdtrb_enq = xhci_write_crb(cmdtrb_enq, curr->lowval, curr->highval, command_paddr);
		xhci_write_register(cinfo->doorbell, XHCI_DOORBELL_HOST, XHCI_DOORBELL_HOST, 32);

		signal_semaphore(cinfo->command_buffer_lock, 1);
		delete curr;
		++dequeue_ptr;
		//Circular buffer
		if (dequeue_ptr - cinfo->cmd_buffer == cinfo->cmd_buf_size)
			dequeue_ptr = cinfo->cmd_buffer;
	}
}

static void xhci_port_command_thread(void* param)
{
	xhci_port_info* pinfo = reinterpret_cast<xhci_port_info*>(param);
	xhci_command** dequeue_ptr = pinfo->cmd_buffer;
	paddr_t command_paddr = pinfo->cmdring.pbase;
	void* cmdtrb_enq = pinfo->cmdring.ringbase;
	while (1)
	{
		wait_semaphore(pinfo->command_available, 1, TIMEOUT_INFINITY);
		xhci_command* curr = *dequeue_ptr;
		//Write command
		PendingCommands[command_paddr] = curr;
		cmdtrb_enq = xhci_write_crb(cmdtrb_enq, curr->lowval, curr->highval, command_paddr);
		//Ring doorbell
		xhci_write_register(pinfo->controller->doorbell, XHCI_DOORBELL(pinfo->slotid), XHCI_DOORBELL_ENPOINT0, 32);

		signal_semaphore(pinfo->command_buffer_lock, 1);
		delete curr;
		++dequeue_ptr;
		//Circular buffer
		if (dequeue_ptr - pinfo->cmd_buffer == pinfo->cmd_buf_size)
			dequeue_ptr = pinfo->cmd_buffer;
	}
}
#endif

static xhci_command* create_address_command(bool bsr, paddr_t context, uint16_t slot)
{
	xhci_command* ret = new xhci_command;
	uint64_t lowval = 0, highval = 0;
	lowval = context;
	highval = XHCI_TRB_ENABLED | XHCI_TRB_TYPE(XHCI_TRB_TYPE_ADDRESS_DEVICE) | XHCI_TRB_SLOTID(slot);
	highval |= bsr ? XHCI_TRB_ADDRESS_BSR : 0;
		
	ret->lowval = lowval;
	ret->highval = highval;
	return ret;
}

static xhci_command* create_evaluate_command(paddr_t context, uint16_t slot)
{
	xhci_command* ret = new xhci_command;
	uint64_t lowval = 0, highval = 0;
	lowval = context;
	highval = XHCI_TRB_ENABLED | XHCI_TRB_TYPE(XHCI_TRB_TYPE_EVALUATE_CONTEXT) | XHCI_TRB_SLOTID(slot);

	ret->lowval = lowval;
	ret->highval = highval;
	return ret;
}

static xhci_command* create_enableslot_command(uint8_t slottype)
{
	xhci_command* ret = new xhci_command;
	ret->lowval = 0;
	ret->highval = XHCI_TRB_ENABLED | XHCI_TRB_TYPE(XHCI_TRB_TYPE_ENABLE_SLOT) | (slottype << 16);
	return ret;
}

static xhci_command* create_setup_stage_trb(uint8_t rType, uint8_t bRequest, uint16_t value, uint16_t wIndex, uint16_t wLength, uint8_t trt)
{
	xhci_command* ret = new xhci_command;
	ret->lowval = rType | (bRequest << 8) | (value << 16) | ((uint64_t)wIndex << 32) | ((uint64_t)wLength << 48);
	ret->highval = 8 | XHCI_TRB_ENABLED | XHCI_TRB_IDT | XHCI_TRB_TRT(3) | XHCI_TRB_TYPE(XHCI_TRB_TYPE_SETUP_STAGE);
	return ret;
}

static xhci_command* create_data_stage_trb(paddr_t buffer, uint16_t size, bool indirection)
{
	xhci_command* ret = new xhci_command;
	ret->lowval = buffer;
	ret->highval = XHCI_TRB_ENABLED | XHCI_TRB_ENT | (indirection ? XHCI_TRB_DIR_IN : 0)  | XHCI_TRB_TYPE(XHCI_TRB_TYPE_DATA_STAGE) | size;
	return ret;
}

static xhci_command* create_status_stage_trb(bool indirection)
{
	xhci_command* ret = new xhci_command;
	ret->lowval = 0;
	ret->highval = XHCI_TRB_ENABLED | XHCI_TRB_ENT | (indirection ? XHCI_TRB_DIR_IN : 0) | XHCI_TRB_TYPE(XHCI_TRB_TYPE_STATUS_STAGE) | XHCI_TRB_IOC;
	return ret;
}

#if USE_COMMAND_THREADS
static void* enqueue_command(xhci_controller_info* cinfo, xhci_command* command)
{
	wait_semaphore(cinfo->command_buffer_lock, 1, TIMEOUT_INFINITY);
	bool success = false;
	size_t cur_val;
	while (!success)
	{
		cur_val = (size_t)(cinfo->enqueue_cmd);
		size_t next_val = cur_val + sizeof(xhci_command**);
		if ((next_val - cur_val) == cinfo->cmd_buf_size * sizeof(xhci_command**))
			next_val = (size_t)cinfo->cmd_buffer;
		success = arch_cas((volatile size_t*)&cinfo->enqueue_cmd, cur_val, next_val);
	}
	//We have our enqueue pointer
	*(xhci_command**)cur_val = command;
	//Enqueued successfully
	signal_semaphore(cinfo->command_available, 1);
	return command;
}

static void* enqueue_command(xhci_port_info* pinfo, xhci_command* command)
{
	wait_semaphore(pinfo->command_buffer_lock, 1, TIMEOUT_INFINITY);
	bool success = false;
	size_t cur_val;
	while (!success)
	{
		cur_val = (size_t)(pinfo->enqueue_cmd);
		size_t next_val = cur_val + sizeof(xhci_command**);
		if ((next_val - cur_val) == pinfo->cmd_buf_size * sizeof(xhci_command**))
			next_val = (size_t)pinfo->cmd_buffer;
		success = arch_cas((volatile size_t*)&pinfo->enqueue_cmd, cur_val, next_val);
	}
	//We have our enqueue pointer
	*(xhci_command**)cur_val = command;
	//Enqueued successfully
	signal_semaphore(pinfo->command_available, 1);
	return command;
}
#else
static void* enqueue_command(xhci_controller_info* cinfo, xhci_command* command)
{
	auto st = acquire_spinlock(cinfo->command_lock);
	xhci_cmdring_info& cmdring = cinfo->cmdring;
	//Write command
	auto st2 = acquire_spinlock(tree_lock);
	PendingCommands[cmdring.penqueue] = command;
	release_spinlock(tree_lock, st2);
	cmdring.enqueue = xhci_write_crb(cmdring.enqueue, command->lowval, command->highval, cmdring.penqueue);
	xhci_write_register(cinfo->doorbell, XHCI_DOORBELL_HOST, XHCI_DOORBELL_HOST, 32);
	release_spinlock(cinfo->command_lock, st);
	delete command;
	return command;
}

static void* enqueue_command(xhci_port_info* pinfo, xhci_command* command)
{
	auto st = acquire_spinlock(pinfo->command_lock);
	xhci_cmdring_info& cmdring = pinfo->cmdring;
	//Write command
	auto st2 = acquire_spinlock(tree_lock);
	PendingCommands[cmdring.penqueue] = command;
	release_spinlock(tree_lock, st2);
	cmdring.enqueue = xhci_write_crb(cmdring.enqueue, command->lowval, command->highval, cmdring.penqueue);
	xhci_write_register(pinfo->controller->doorbell, XHCI_DOORBELL(pinfo->slotid), XHCI_DOORBELL_ENPOINT0, 32);
	release_spinlock(pinfo->command_lock, st);
	delete command;
	return command;
}

static void* enqueue_commands(xhci_port_info* pinfo, xhci_command** commands)
{
	auto st = acquire_spinlock(pinfo->command_lock);
	xhci_cmdring_info& cmdring = pinfo->cmdring;
	//Write command
	size_t n = 0;
	while (commands[n])
	{
		paddr_t lastcmd = cmdring.penqueue;
		cmdring.enqueue = xhci_write_crb(cmdring.enqueue, commands[n]->lowval, commands[n]->highval, cmdring.penqueue);
		auto st2 = acquire_spinlock(tree_lock);
		PendingCommands[lastcmd] = commands;
		release_spinlock(tree_lock, st2);
		delete commands[n];
		++n;
	}
	xhci_write_register(pinfo->controller->doorbell, XHCI_DOORBELL(pinfo->slotid), XHCI_DOORBELL_ENPOINT0, 32);
	release_spinlock(pinfo->command_lock, st);
	return commands;
}
#endif

static void* xhci_wait_complete(void* commandtrb, size_t timeout)
{
	semaphore_t waiter = create_semaphore(0, u"USB-XHCI Command Waiter");
	auto st = acquire_spinlock(tree_lock);
	auto it = CompletedCommands.find(commandtrb);
	if (it != CompletedCommands.end())
	{
		release_spinlock(tree_lock, st);
		return it->second;
	}
	WaitingCommands[commandtrb] = waiter;
	release_spinlock(tree_lock, st);
	//kprintf(u"Waiting for TRB %x\n", commandtrb);
	if (wait_semaphore(waiter, 1, timeout) == 0)
		return nullptr;
	st = acquire_spinlock(tree_lock);
	void* result = CompletedCommands[commandtrb];
	CompletedCommands.remove(commandtrb);
	delete_semaphore(waiter);
	WaitingCommands.remove(commandtrb);
	release_spinlock(tree_lock, st);
	return result;
}

static void* xhci_wait_portsc(size_t port, size_t timeout)
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

static void update_packet_size_fs(xhci_port_info* port)
{
	uint8_t* buffer = new uint8_t[8];
	xhci_command** devcmds = new xhci_command*[4];
	devcmds[0] = create_setup_stage_trb(0x80, 6, 0x100, 0, 8, 3);
	devcmds[1] = create_data_stage_trb(get_physical_address(buffer), 8, true);
	devcmds[2] = create_status_stage_trb(true);
	devcmds[3] = nullptr;
	void* statusevt = enqueue_commands(port, devcmds);
	void* res = xhci_wait_complete(statusevt, 1000);
	if (get_trb_completion_code(res) != XHCI_COMPLETION_SUCCESS)
	{
		kprintf(u"Error getting FullSpeed packet size: %d\n", get_trb_completion_code(res));
		return;
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
		void* lastcmd = enqueue_command(port->controller, create_evaluate_command(incontext, port->slotid));
		void* res = xhci_wait_complete(lastcmd, 1000);
		if (get_trb_completion_code(res) != XHCI_COMPLETION_SUCCESS)
		{
			kprintf(u"Error updating device context\n");
		}
	}
}

static void Stall(uint32_t milliseconds)
{
	uint64_t current = arch_get_system_timer();
	while (arch_get_system_timer() - current < milliseconds);
}

struct xhci_thread_port_startup {
	xhci_controller_info* cinfo;
	size_t port;
};
static void xhci_port_startup(void* pinf)
{
	xhci_controller_info* cinfo = ((xhci_thread_port_startup*)pinf)->cinfo;
	size_t n = ((xhci_thread_port_startup*)pinf)->port;

	uint32_t portsc = xhci_read_register(cinfo->oppbase, XHCI_OPREG_PORTSC(n), 32);
	if ((portsc & 1) == 1)
	{
		//Reset port
		portsc |= (1 << 4);
		portsc &= (SIZE_MAX - 1);
		xhci_write_register(cinfo->oppbase, XHCI_OPREG_PORTSC(n), portsc, 32);
		uint64_t* psctrb = (uint64_t*)xhci_wait_portsc(n, 1000);
		if (!psctrb)
			return kprintf(u"Timeout!\n");
		//Port successfully reset
		uint8_t portspeed = XHCI_PORTSC_SPEED_GET(xhci_read_register(cinfo->oppbase, XHCI_OPREG_PORTSC(n), 32));
		kprintf(u" Device attatched on port %d, %d Kbps\n", n, ReadablePortSpeed(portspeed) / 1000);
		Stall(100);
#if 1
		//Enable slot command
		void* last_command = nullptr;
		last_command = enqueue_command(cinfo, create_enableslot_command(protocol_speeds[portspeed].slottype));
		//Now get a command completion event from the event ring
		uint64_t* curevt = (uint64_t*)xhci_wait_complete(last_command, 1000);
		if (get_trb_completion_code(curevt) != XHCI_COMPLETION_SUCCESS)
		{
			kprintf(u"Error enabling slot!\n");
			return;
		}
		uint8_t slotid = curevt[1] >> 56;
		if (slotid == 0)
			return;
		//kprintf(u"  DSE command complete: %d\n", slotid);
		//Allocate input context
		paddr_t incontext = pmmngr_allocate(1);
		void* mapincontxt = find_free_paging(PAGESIZE);
		paging_map(mapincontxt, incontext, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING);
		memset(mapincontxt, 0, PAGESIZE);
		//Set input control context
		volatile uint32_t* aflags = raw_offset<volatile uint32_t*>(mapincontxt, 4);
		*aflags = 3;
		//Setup slot context
		xhci_write_register(mapincontxt, 0x20, (1 << 27) | (portspeed << 20), 32);
		xhci_write_register(mapincontxt, 0x20 + 4, n << 16, 32);		//Root hub port number
		//Initialise endpoint 0 context, allocate TRB
		paddr_t dtrb = pmmngr_allocate(1);
		void* mapdtrb = find_free_paging(PAGESIZE);
		paging_map(mapdtrb, dtrb, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING);
		memset(mapdtrb, 0, PAGESIZE);
		//Control endpoint, CErr = 3
		xhci_write_register(mapincontxt, 0x40 + 4, (4 << 3) | (max_packet_size(portspeed) << 16) | (3 << 1), 32);
		xhci_write_register(mapincontxt, 0x40 + 8, dtrb & UINT32_MAX | 1, 32);
		xhci_write_register(mapincontxt, 0x40 + 12, dtrb >> 32, 32);
		xhci_write_register(mapincontxt, 0x40 + 0x10, PAGESIZE / 0x10, 32);

		//Allocate device context
		paddr_t dctxt = pmmngr_allocate(1);
		void* mapdctxt = find_free_paging(PAGESIZE);
		paging_map(mapdctxt, dctxt, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING);
		memset(mapdctxt, 0, PAGESIZE);
		//Write to device context array slot
		*raw_offset<volatile uint64_t*>(cinfo->devctxt, slotid * 8) = dctxt;
		//Issue address device command
		last_command = enqueue_command(cinfo, create_address_command(false, incontext, slotid));
		curevt = (uint64_t*)xhci_wait_complete(last_command, 1000);
		if (get_trb_completion_code(curevt) != XHCI_COMPLETION_SUCCESS)
		{
			kprintf(u"Error addressing device\n");
			return;
		}

		//Now create command thread for port
		xhci_port_info* port_info = new xhci_port_info;
		port_info->controller = cinfo;
		port_info->portindex = n;
		port_info->slotid = slotid;
		port_info->cmdring.pbase = dtrb;
		port_info->cmdring.penqueue = dtrb;
		port_info->cmdring.ringbase = mapdtrb;
		port_info->cmdring.enqueue = mapdtrb;
		port_info->cmd_buf_size = 32;
		port_info->device_context = mapdctxt;
#if USE_COMMAND_THREADS
		port_info->enqueue_cmd = port_info->cmd_buffer = new xhci_command*[port_info->cmd_buf_size];
		port_info->command_available = create_semaphore(0, u"USB-XHCI Port Command Available");
		port_info->command_buffer_lock = create_semaphore(1, u"USB-XHCI Port Command Lock");
		port_info->cmdthread = create_thread(&xhci_port_command_thread, port_info);
#else
		port_info->command_lock = create_spinlock();
#endif
		//If the device is full speed, we need to work out the correct packet size
		if (portspeed == FULL_SPEED)
		{
			update_packet_size_fs(port_info);
		}
		//Now we get device descriptor
		xhci_command** devcmds = new xhci_command*[4];
		devcmds[0] = create_setup_stage_trb(0x80, 6, 0x100, 0, 20, 3);
		volatile usb_device_descriptor* devdesc = (usb_device_descriptor*)new uint8_t[64];
		devcmds[1] = create_data_stage_trb(get_physical_address((void*)devdesc), 20, true);
		devcmds[2] = create_status_stage_trb(true);
		devcmds[3] = nullptr;
		void* statusevt = enqueue_commands(port_info, devcmds);
		void* resulttrb = xhci_wait_complete(statusevt, 1000);
		if (get_trb_completion_code(resulttrb) != XHCI_COMPLETION_SUCCESS)
		{
			kprintf(u"Error getting device descriptor (code %d)\n", get_trb_completion_code(resulttrb));
			return;
		}
		if (devdesc->iProduct != 0)
		{
			//Get device string
			devcmds[0] = create_setup_stage_trb(0x80, 6, 0x300 | devdesc->iProduct, 0, 256, 3);
			volatile wchar_t* devstr = new wchar_t[256];
			devcmds[1] = create_data_stage_trb(get_physical_address((void*)devstr), 256, true);
			devcmds[2] = create_status_stage_trb(true);
			devcmds[3] = nullptr;
			statusevt = enqueue_commands(port_info, devcmds);
			resulttrb = xhci_wait_complete(statusevt, 1000);
			kprintf(u"   %s\n", ++devstr);
		}
		else
		{
			kprintf_a("   Device %x:%x class %x:%x:%x USB version %x\n", devdesc->idVendor, devdesc->idProduct, devdesc->bDeviceClass, devdesc->bDeviceSublass, devdesc->bDeviceProtocol, devdesc->bcdUSB);
		}
		//devdesc = raw_offset<volatile usb_device_descriptor*>(devdesc, 64);
#endif
	}
}

static void xhci_protocol_speeds(void* baseaddr)
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
		supportp = xhci_find_extended_cap(baseaddr, XHCI_ECAP_SUPPORT, supportp);
		if (supportp != 0)
		{
			uint32_t supreg = xhci_read_register(baseaddr, supportp, 32);
			union {
				char usb_str[5];
				uint32_t namestr;
			}u;
			u.namestr = xhci_read_register(baseaddr, supportp + 4, 32);
			u.usb_str[4] = 0;
			uint16_t revision = supreg >> 16;
			uint32_t psic = xhci_read_register(baseaddr, supportp + 8, 32) >> 28;
			uint32_t slott = xhci_read_register(baseaddr, supportp + 0xC, 32) & 0x1F;
			kprintf_a("%s%x.%02x, %d descriptors, slot type %d\n", u.usb_str, revision >> 8, revision & 0xFF, psic, slott);
			for (size_t i = 0; i < psic; ++i)
			{
				uint32_t data = xhci_read_register(baseaddr, supportp + 0x10 + i*4, 32);
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
				else if(psitype == 2)		//Rx
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
				kprintf_a(" Protocol %d, speed %d*10^%d\n", psiv, relstruct->spd_mantissa, relstruct->spd_exponent);
			}
		}
	} while (supportp != 0);
	kprintf(u"XHCI supported protocols loaded\n");
}

static void xhci_controller_init(size_t handle)
{
	kprintf_a("Initalizing XHCI controller %x\n", handle);
	xhci_controller_info* cinfo = nullptr;
	void* baseaddr = xhci_pci_baseaddr(pcicontrollers[handle], cinfo);
	//Configure MSI
	void* oppbase = raw_offset<void*>(baseaddr, xhci_read_register(baseaddr, XHCI_CAPREG_CAPLENGTH, 8));
	cinfo->oppbase = oppbase;
	uint32_t usbcmd = xhci_read_register(oppbase, XHCI_OPREG_USBCMD, 32);
	//Request control of host controller, if supported
	uint32_t legacy = xhci_find_extended_cap(baseaddr, XHCI_ECAP_LEGSUP);
	if (legacy != 0)
	{
		uint32_t legreg = xhci_read_register(baseaddr, legacy, 32);
		legreg |= (1 << 24);
		xhci_write_register(baseaddr, legacy, legreg, 32);
		while (1)
		{
			legreg = xhci_read_register(baseaddr, legacy, 32);
			if (((legreg & (1 << 24)) != 0) && ((legreg & (1 << 16)) == 0))
				break;
		}
		kprintf(u"Taken control of XHCI from firmware\n");
	}
	//Stop host controller first
	usbcmd &= (SIZE_MAX - 1);
	xhci_write_register(oppbase, XHCI_OPREG_USBCMD, usbcmd, 32);
	while((xhci_read_register(oppbase, XHCI_OPREG_USBSTS, 32) & 1) == 0);
	//Reset the host controller, now it's halted
	usbcmd |= (1 << 1);
	xhci_write_register(oppbase, XHCI_OPREG_USBCMD, usbcmd, 32);
	while ((xhci_read_register(oppbase, XHCI_OPREG_USBCMD, 32) & (1 << 1)) != 0);
	//Reset is complete
	//Create device context array
	size_t hcsp1 = xhci_read_register(baseaddr, XHCI_CAPREG_HCSPARAMS1, 32);
	size_t max_slots = hcsp1 & 0xFF;
	size_t max_ports = (hcsp1 >> 24) & 0xFF;
	paddr_t cont = pmmngr_allocate(1);
	void* devctxt = find_free_paging(PAGESIZE);
	cinfo->devctxt = devctxt;
	paging_map(devctxt, cont, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING);
	memset(devctxt, 0, PAGESIZE);
	xhci_write_register(oppbase, XHCI_OPREG_DCBAAP, cont & UINT32_MAX, 32);
	xhci_write_register(oppbase, XHCI_OPREG_DCBAAP+4, cont >> 32, 32);
	//Give XHCI a scratchpad
	uint32_t hcsp2 = xhci_read_register(baseaddr, XHCI_CAPREG_HCSPARAMS2, 32);
	uint32_t scratchsize = (hcsp2 >> 27) & 0x1F;
	scratchsize |= ((hcsp2 >> 21) & 0x1F << 5);
	if (scratchsize > 512)
		kprintf(u"Warning: scratchpad too big!\n");
	paddr_t spad = pmmngr_allocate(1);
	xhci_write_register(devctxt, 0, spad, 64);
	xhci_write_register(oppbase, XHCI_OPREG_CONFIG, max_slots, 32);
	void* mappedspa = find_free_paging(PAGESIZE);
	paging_map(mappedspa, spad, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING);
	for (size_t n = 0; n < scratchsize && n < 512; ++n)
	{
		paddr_t* spadi = (paddr_t*)mappedspa;
		spadi[n] = pmmngr_allocate(1);
	}
	//XHCI knows that we're loaded, and has device slots available
	//Look up protocols
	xhci_protocol_speeds(baseaddr);
	//Create command ring
	paddr_t crb = pmmngr_allocate(1);
	void* crpg = find_free_paging(PAGESIZE);
	void* enq = crpg;
	paging_map(crpg, crb, PAGESIZE, PAGE_ATTRIBUTE_WRITABLE | PAGE_ATTRIBUTE_NO_CACHING);
	memset(crpg, 0, PAGESIZE);
	xhci_write_register(oppbase, XHCI_OPREG_CRCR, (crb & UINT32_MAX) | 1, 32);
	xhci_write_register(oppbase, XHCI_OPREG_CRCR+4, crb >> 32, 32);
	//Write to info
	cinfo->cmdring.penqueue = cinfo->cmdring.pbase = crb;
	cinfo->cmdring.enqueue = cinfo->cmdring.ringbase = crpg;
	//Set up event rings and primary interrupter
	void* runtbase = raw_offset<void*>(baseaddr, xhci_read_register(baseaddr, XHCI_CAPREG_RTSOFF, 32));
	cinfo->runbase = runtbase;
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
	xhci_write_register(evtringseg, 0, evt, 64);
	xhci_write_register(evtringseg, 8, PAGESIZE / 0x10, 64);
	//Write the event ring info
	cinfo->primaryevt.dequeueptr = evtdp;
	cinfo->primaryevt.ringbase = evtring;
	//Set segment table size
	xhci_write_register(runtbase, XHCI_RUNREG_ERSTSZ(0), 1, 32);
	//Set dequeue pointer
	xhci_write_register(runtbase, XHCI_RUNREG_ERDP(0), evt, 64);
	//Enable event ring
	xhci_write_register(runtbase, XHCI_RUNREG_ERSTBA(0), evtseg, 64);
	xhci_write_register(runtbase, XHCI_RUNREG_IMAN(0), xhci_read_register(runtbase, XHCI_RUNREG_IMAN(0), 32), 32);
	//Set up synchronisation
	tree_lock = create_spinlock();
	port_tree_lock = create_spinlock();
	event_available = create_semaphore(0, u"USB-XHCI Interrupt Semaphore");
	//Create command buffer
	cinfo->cmd_buf_size = 512;
#if USE_COMMAND_THREADS
	cinfo->cmd_buffer = new xhci_command*[cinfo->cmd_buf_size];
	cinfo->enqueue_cmd = cinfo->cmd_buffer;
	cinfo->command_buffer_lock = create_semaphore(1, u"USB-XHCI Command Buffer Lock");
	cinfo->command_available = create_semaphore(0, u"USB-XHCI Command Available");
#else
	cinfo->command_lock = create_spinlock();
#endif
	//Start driver threads
	cinfo->evtthread = create_thread(&xhci_event_thread, cinfo);
#if USE_COMMAND_THREADS
	cinfo->cmdthread = create_thread(&xhci_command_thread, cinfo);
#endif

	//TODO - interrupts go here
	//Start USB!
	xhci_write_register(oppbase, XHCI_OPREG_USBCMD, 1 | (1<<2), 32);		//Start USB!
	//Enable Interrupter 0
	xhci_write_register(runtbase, XHCI_RUNREG_IMAN(0), xhci_read_register(runtbase, XHCI_RUNREG_IMAN(0), 32) | (1 << 1), 32);
	//Now find doorbell registers
	void* doorbell = raw_offset<void*>(baseaddr, xhci_read_register(baseaddr, XHCI_CAPREG_DBOFF, 32));
	cinfo->doorbell = doorbell;
	//Work on ports
	kprintf(u"XHCI controller enabled, %d ports\n", max_ports);
	for (size_t n = 1; n <= max_ports; ++n)
	{
		xhci_thread_port_startup* sup = new xhci_thread_port_startup;
		sup->cinfo = cinfo;
		sup->port = n;
		//create_thread(&xhci_port_startup, sup);
		xhci_port_startup(sup);
	}
}

void xhci_init()
{
	pci_bus_scan(&xhci_pci_scan);
	//Now init controllers
	for (auto it = pcicontrollers.begin(); it != pcicontrollers.end(); ++it)
	{
		xhci_controller_init(it->first);
	}
}