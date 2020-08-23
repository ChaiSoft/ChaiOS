BITS 64

section .text

save_interrupt_registers:
pop rax
push rcx
push rdx
push r8
push r9
push r10
push r11
jmp rax

restore_interrupt_registers:
pop rax
pop r11
pop r10
pop r9
pop r8
pop rdx
pop rcx
jmp rax

swap_gs_ifpriv:
mov rax, QWORD[rbp+8+0x10]
and rax, 0x3	;CPL
cmp rax, 0
je .next
swapgs
.next:
ret


%macro SAVE_VOLATILE_REGISTERS 0
push rax
call save_interrupt_registers
%endmacro

%macro RESTORE_VOLATILE_REGISTERS 0
call restore_interrupt_registers
pop rax
%endmacro

extern x64_interrupt_dispatcher
extern x64_save_fpu
extern x64_restore_fpu
extern xsavearea_size
extern memset
extern kprintf

;Stack layout:
;Old stack
;____________________
;PADDING
;____________________
;XSAVE
;____________________
;Old RSP
;____________________
save_fpu_interrupt:
pop r9			;Return address
mov rax, xsavearea_size
mov rdx, [rax]	;Size of stack area
mov r8, rdx		;Length parameter for memset
mov rcx, rsp
sub rcx, rdx
and cl, 0xC0	;Align stack
mov rdx, rsp	;Old stack pointer
mov rsp, rcx	;Load new stack pointer
push rdx		;Save old RSP on new stack
push r9			;R9 is volatile, save our return address
;Set to 0
sub rsp, 32
push r8
mov rdx, 0
push rdx
push rcx
call memset
add rsp, 32+24
;Now save the FPU state
mov rax, [qword x64_save_fpu]
call rax
pop r9
jmp r9

restore_fpu_interrupt:
pop r8		;Return address
pop r9		;Old stack pointer
mov rcx, rsp	;XSAVE area
push r8		;R8 is volatile
push r9		;R9 is volatile
mov rax, [qword x64_restore_fpu]
call rax	;Save the state!
pop r9
pop r8		;Restore R8
mov rsp, r9	;Load old stack
jmp r8


extern x64_syscall_handler

;SYSCALL
global x64_syscall_entry
x64_syscall_entry:
;RCX: return address
;R11: FLAGS
;Save user stack
mov rdx, rsp
;Load kernel stack
mov rsp, qword[fs:0x20]
swapgs

;Save Syscall Return stuff
push rcx
push rdx
push r11
push rbp
mov rbp, rsp

;align the stack
and sp, 0xFFF0

;KERNEL
sti

sub rsp, 32
call x64_syscall_handler
add rsp, 32

;Return
cli

;restore stack
mov rsp, rbp
pop rbp
pop r11
pop rdx
pop rcx

swapgs

;USER STACK
mov rsp, rdx
o64 sysret



x64_compat_common:
sti		;Preemptable

jmp $

cli
ret

global x64_syscall_entry_compat
x64_syscall_entry_compat:
;RCX: return address
;R11: FLAGS
;Save user stack
mov rdx, rsp
;Load kernel stack
mov rsp, [fs:0x20]
swapgs
;KERNEL
call x64_compat_common
;Return
swapgs
mov rsp, rdx
sysret

global x64_sysenter_entry_compat
x64_sysenter_entry_compat:
cli
;RDX: return address
;RCX: user stack
swapgs
xchg rdx, rcx

call x64_compat_common

xchg rdx, rcx
;Return
swapgs
sti
sysexit


;First parameter: error code passed
;Second parameter: 
%macro INTERRUPT_HANDLER 2
global x64_interrupt_handler_%2
x64_interrupt_handler_%2:
cld
%if %1 == 0
push 0xDEADBEEF			;Dummy error code
%endif
;Stack frame
push rbp
mov rbp, rsp
SAVE_VOLATILE_REGISTERS
;Per CPU information
call swap_gs_ifpriv
call save_fpu_interrupt

;Now we pass the stack interrupt stack and vector
mov rcx, %2
mov rdx, rbp
;add rdx, 8

sub rsp, 32
call x64_interrupt_dispatcher
add rsp, 32

call restore_fpu_interrupt
call swap_gs_ifpriv


RESTORE_VOLATILE_REGISTERS
pop rbp
add rsp, 8		;Get rid of error code
iretq
%endmacro

%macro INTERRUPT_HANDLER_BLOCK 3
%assign i %2
%rep %3-%2
INTERRUPT_HANDLER %1, i
%assign i i+1
%endrep
%endmacro

INTERRUPT_HANDLER 0, 0
INTERRUPT_HANDLER 0, 1
INTERRUPT_HANDLER 0, 2
INTERRUPT_HANDLER 0, 3
INTERRUPT_HANDLER 0, 4
INTERRUPT_HANDLER 0, 5
INTERRUPT_HANDLER 0, 6
INTERRUPT_HANDLER 0, 7
INTERRUPT_HANDLER 1, 8
INTERRUPT_HANDLER 0, 9
INTERRUPT_HANDLER 1, 10
INTERRUPT_HANDLER 1, 11
INTERRUPT_HANDLER 1, 12
INTERRUPT_HANDLER 1, 13
INTERRUPT_HANDLER 1, 14
INTERRUPT_HANDLER 0, 15
INTERRUPT_HANDLER 0, 16
INTERRUPT_HANDLER 1, 17
INTERRUPT_HANDLER 0, 18
INTERRUPT_HANDLER 0, 19
INTERRUPT_HANDLER 0, 20
INTERRUPT_HANDLER_BLOCK 0, 21, 30
INTERRUPT_HANDLER 1, 30
INTERRUPT_HANDLER 0, 31
INTERRUPT_HANDLER_BLOCK 0, 32, 256

%macro dq_concat 2
dq %1%2
%endmacro

section .data
global default_irq_handlers
default_irq_handlers:
%assign i 0
%rep 256
dq_concat x64_interrupt_handler_,i
%assign i i+1
%endrep
stack_str: dw __utf16__(`Interrupt frame:\n Error code %x\n Return RIP: %x\n Return CS: %x\n Return RFLAGS: %x\n Return RSP: %x\n Return SS: %x\n`), 0