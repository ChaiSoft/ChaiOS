

global arch_cpu_init
arch_cpu_init:
;Setup control registers
mov rax, cr0
or rax, (1<<1)
and ax, (-1) - ((1<<2)|(1<<3))
ret