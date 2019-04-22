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
bool arch_cas(volatile size_t* loc, size_t oldv, size_t newv);
#else
int arch_cas(volatile size_t* loc, size_t oldv, size_t newv);
#endif

void arch_pause();		//Hyperthreading hint

typedef size_t cpu_status_t;

cpu_status_t arch_disable_interrupts();
void arch_restore_state(cpu_status_t val);

void arch_setup_interrupts();

#define INTERRUPT_SUBSYSTEM_NATIVE 0
#define INTERRUPT_SUBSYSTEM_DISPATCH 1
#define INTERRUPT_SUBSYSTEM_IRQ 2

typedef void(*arch_register_irq_func)(size_t vector, void* fn, void* param);
typedef void(*arch_register_irq_postevt)(size_t vector, void(*evt)());

typedef struct _arch_interrupt_subsystem {
	arch_register_irq_func register_irq;
	arch_register_irq_postevt post_evt;
}arch_interrupt_subsystem;

uint64_t arch_read_per_cpu_data(uint32_t offset, uint8_t width);
void arch_write_per_cpu_data(uint32_t offset, uint8_t width, uint64_t value);

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
	static const size_t data_size = sizeof(per_cpu_data);
public:
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
}pcpu_data;
#endif

CHAIKRNL_FUNC void arch_register_interrupt_subsystem(uint32_t subsystem, arch_interrupt_subsystem* system);

typedef uint8_t(*dispatch_interrupt_handler)(size_t vector, void* param);
CHAIKRNL_FUNC void arch_register_interrupt_handler(uint32_t subsystem, size_t vector, void* fn, void* param);
CHAIKRNL_FUNC void arch_install_interrupt_post_event(uint32_t subsystem, size_t vector, void(*evt)());

void arch_set_paging_root(size_t root);

uint32_t arch_current_processor_id();
uint8_t arch_startup_cpu(uint32_t processor, void* address, volatile size_t* rendezvous, size_t rendezvousval);
uint8_t arch_is_bsp();
void arch_halt();

typedef void* context_t;
context_t context_factory();
void context_destroy(context_t ctx);
int save_context(context_t ctxt);
void jump_context(context_t ctxt, int value);

typedef void* kstack_t;
kstack_t arch_create_kernel_stack();
void arch_destroy_kernel_stack(kstack_t stack);
void* arch_init_stackptr(kstack_t stack);
void arch_new_thread(context_t ctxt, kstack_t stack, void* entrypt);

void arch_flush_tlb(void*);
CHAIKRNL_FUNC void arch_flush_cache();
void arch_memory_barrier();

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
uint64_t arch_get_system_timer();

#ifdef __cplusplus
}
#endif

#endif
