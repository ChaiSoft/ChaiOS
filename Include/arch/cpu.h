#ifndef CHAIOS_ARCH_CPU_H
#define CHAIOS_ARCH_CPU_H

#include <stdheaders.h>
#include <chaikrnl.h>

#ifdef __cplusplus
extern "C" {
#endif

void arch_cpu_init();

size_t arch_read_port(size_t port, uint8_t width);
void arch_write_port(size_t port, size_t value, uint8_t width);

#ifdef __cplusplus
CHAIKRNL_FUNC bool arch_cas(volatile size_t* loc, size_t oldv, size_t newv);
#else
CHAIKRNL_FUNC int arch_cas(volatile size_t* loc, size_t oldv, size_t newv);
#endif

CHAIKRNL_FUNC void arch_pause();		//Hyperthreading hint

typedef size_t cpu_status_t;

cpu_status_t arch_disable_interrupts();
cpu_status_t arch_enable_interrupts();
void arch_restore_state(cpu_status_t val);

#define BREAKPOINT_CODE 0
#define BREAKPOINT_WRITE 1
#define BREAKPOINT_READ_WRITE 3
CHAIKRNL_FUNC void arch_set_breakpoint(void* addr, size_t length, size_t type);
CHAIKRNL_FUNC void arch_enable_breakpoint(size_t enabled);

void arch_setup_interrupts();

#define INTERRUPT_SUBSYSTEM_NATIVE 0
#define INTERRUPT_SUBSYSTEM_DISPATCH 1
#define INTERRUPT_SUBSYSTEM_IRQ 2

#define IRQL_TIMER 0xFFFFFFFF
#define IRQL_INTERRUPT 1
#define IRQL_KERNEL 0

typedef void(*arch_register_irq_func)(size_t vector, uint32_t processor, void* fn, void* param);
typedef void(*arch_register_irq_postevt)(size_t vector, uint32_t processor, void(*evt)());

typedef struct _arch_interrupt_subsystem {
	arch_register_irq_func register_irq;
	arch_register_irq_postevt post_evt;
}arch_interrupt_subsystem;

uint64_t arch_read_per_cpu_data(uint32_t offset, uint8_t width);
void arch_write_per_cpu_data(uint32_t offset, uint8_t width, uint64_t value);

void arch_write_tls_base(void* tls, uint8_t user);
uint64_t arch_read_tls(uint32_t offset, uint8_t user, uint8_t width);
void arch_write_tls(uint32_t offset, uint8_t user, uint64_t value, uint8_t width);

typedef struct _per_cpu_data {
	struct _per_cpu_data* cpu_data;
	void* running_thread;
	uint64_t cpu_ticks;
	uint32_t cpu_id;
}per_cpu_data;

#ifdef __cplusplus
static class _cpu_data {
	static const uint32_t offset_ptr = 0;
	static const uint32_t offset_thread = 0x8;
	static const uint32_t offset_ticks = 0x10;
	static const uint32_t offset_id = 0x18;
	static const uint32_t offset_irql = 0x1C;
	static const uint32_t offset_kstack = 0x20;
	static const uint32_t offset_max = 0x28;
public:
	static const size_t data_size = 0x38;
	class cpu_id {
	public:
		uint32_t operator = (uint32_t i) { arch_write_per_cpu_data(offset_id, 32, i); return i; }
		operator uint32_t() const { return arch_read_per_cpu_data(offset_id, 32); }
	}cpuid;
	class cpu_data {
	public:
		operator per_cpu_data*() const { return (per_cpu_data*)arch_read_per_cpu_data(offset_ptr, 64); }
	}cpudata;
	class running_thread {
	public:
		void* operator = (void* i) { arch_write_per_cpu_data(offset_thread, 64, (size_t)i); return i; }
		operator void*() const { return (void*)arch_read_per_cpu_data(offset_thread, 64); }
	}runningthread;
	class cpu_ticks {
	public:
		uint64_t operator = (uint64_t i) { arch_write_per_cpu_data(offset_ticks, 64, i); return i; }
		operator uint64_t() const { return arch_read_per_cpu_data(offset_ticks, 64); }
	}cputicks;

	class cpu_irql {
	public:
		uint32_t operator = (uint32_t i) { arch_write_per_cpu_data(offset_irql, 32, i); return i; }
		operator uint32_t() const { return arch_read_per_cpu_data(offset_irql, 32); }
	}irql;

	class cpu_kstack {
	public:
		void* operator = (void* i) { arch_write_per_cpu_data(offset_kstack, 64, (size_t)i); return i; }
		operator void*() const { return (void*)arch_read_per_cpu_data(offset_kstack, 64); }
	}kstack;
}pcpu_data;
uint64_t arch_msi_address(uint64_t* data, size_t vector, uint32_t processor, uint8_t edgetrigger = 1, uint8_t deassert = 0);
#endif

CHAIKRNL_FUNC void arch_register_interrupt_subsystem(uint32_t subsystem, arch_interrupt_subsystem* system);

typedef uint8_t(*dispatch_interrupt_handler)(size_t vector, void* param);
#define INTERRUPT_ALLCPUS (-1)
#define INTERRUPT_CURRENTCPU (-2)
CHAIKRNL_FUNC void arch_register_interrupt_handler(uint32_t subsystem, size_t vector, uint32_t processor, void* fn, void* param);
CHAIKRNL_FUNC void arch_install_interrupt_post_event(uint32_t subsystem, size_t vector, uint32_t processor, void(*evt)());

CHAIKRNL_FUNC uint32_t arch_allocate_interrupt_vector();
CHAIKRNL_FUNC void arch_reserve_interrupt_range(uint32_t start, uint32_t end);

void arch_set_paging_root(size_t root);

uint32_t arch_current_processor_id();
uint8_t arch_startup_cpu(uint32_t processor, void* address, volatile size_t* rendezvous, size_t rendezvousval);
uint8_t arch_is_bsp();
void arch_halt();
void arch_local_eoi();

typedef void* context_t;
context_t context_factory();
void context_destroy(context_t ctx);
int save_context(context_t ctxt);
void jump_context(context_t ctxt, int value);

typedef void* stack_t;
stack_t arch_create_stack(size_t length, uint8_t user);
void arch_destroy_stack(stack_t stack, size_t length);
void* arch_init_stackptr(stack_t stack, size_t length);
void arch_new_thread(context_t ctxt, stack_t stack, void* entrypt);

void arch_go_usermode(void* userstack, void (*ufunc)(void*), size_t bitness);
void arch_write_kstack(stack_t stack);

CHAIKRNL_FUNC void arch_flush_tlb(void*);
CHAIKRNL_FUNC void arch_flush_cache();
void arch_memory_barrier();

CHAIKRNL_FUNC uint16_t arch_swap_endian16(uint16_t);
CHAIKRNL_FUNC uint32_t arch_swap_endian32(uint32_t);
CHAIKRNL_FUNC uint64_t arch_swap_endian64(uint64_t);

#ifdef __cplusplus
enum ARCH_CACHE_TYPE {
	CACHE_TYPE_UNKNOWN,
	CACHE_TYPE_DATA,
	CACHE_TYPE_INSTRUCTION,
	CACHE_TYPE_UNIFIED
};

#define CACHE_FULLY_ASSOCIATIVE SIZE_MAX

size_t cpu_get_cache_size(uint8_t cache_level, ARCH_CACHE_TYPE type);
size_t cpu_get_cache_associativity(uint8_t cache_level, ARCH_CACHE_TYPE type);
size_t cpu_get_cache_linesize(uint8_t cache_level, ARCH_CACHE_TYPE type);

typedef void(*cpu_cache_callback)(uint8_t, ARCH_CACHE_TYPE);
size_t iterate_cpu_caches(cpu_cache_callback callback);
#endif

void cpu_print_information();
CHAIKRNL_FUNC uint64_t arch_get_system_timer();

#ifdef __cplusplus
}
#endif

#endif
