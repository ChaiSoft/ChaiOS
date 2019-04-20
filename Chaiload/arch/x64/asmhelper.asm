BITS 64

section .text

global inportb
inportb:
xor rax, rax
mov dx, cx
in al, dx
ret

global inportw
inportw:
xor rax, rax
mov dx, cx
in ax, dx
ret

global inportd
inportd:
xor rax, rax
mov dx, cx
in eax, dx
ret

global outportb
outportb:
mov al, dl
mov dx, cx
out dx, al
ret

global outportw
outportw:
mov ax, dx
mov dx, cx
out dx, ax
ret

global outportd
outportd:
mov eax, edx
mov dx, cx
out dx, eax
ret

global cpuid
;Stack
;[RBP]: Backlink
;[RBP+8]: Return address
;[RBP+16]: RCX home, page
;[RBP+24]: RDX home, a*
;[RBP+32]: R8 home, b*
;[RBP+40]: R9 home, c*
;[RBP+48]: d*
;[RBP+56]: subpage

cpuid:
push rbp
mov rbp, rsp
push rbx
mov rax, rcx
mov r10, rdx
mov rcx, [rbp+56]
cpuid
mov [r10], rax
mov [r8], rbx
mov [r9], rcx
mov r11, [rbp+48]
mov [r11], rdx
pop rbx
pop rbp
ret

global cacheflush
cacheflush:
wbinvd
ret

global tlbflush
tlbflush:
invlpg [rcx]
ret

global x64_locked_cas
x64_locked_cas:
mov rax, rdx
lock cmpxchg [rcx], r8
jz .true
mov rax, 0
ret
.true:
mov rax, 1
ret

global disable
disable:
pushfq
cli
pop rax
ret

global restore
restore:
push rcx
popfq
ret

global memory_barrier
memory_barrier:
mfence
ret

global get_paging_root
get_paging_root:
mov rax, cr3
ret

global set_paging_root
set_paging_root:
mov cr3, rcx
ret

global cpu_startup
cpu_startup:
ret
;Enable SSE
mov rax, cr0
and ax, 0xFFFB
or ax, 0x2
mov cr0, rax	;Clear CR0.EM, set CR0.MP
mov rax, cr4
or ax, (3<<9)
mov cr4, rax
;SSE is enabled. Now enable AVX, if AVX is supported

push rax
push rcx
xor rcx, rcx
xgetbv		;Load XCR0
or al, 7	;AVX, SSE, X87
xsetbv
pop rcx
pop rax
ret

global pause
pause:
pause
ret

global save_gdt
save_gdt:
sgdt [rcx]
ret

global get_segment_register
get_segment_register:
and rcx, 0x7
mov rdx, .jmp_tab
add rdx, rcx
add rdx, rcx
jmp rdx
.jmp_tab:
jmp short .cs
jmp short .ds
jmp short .es
jmp short .fs
jmp short .gs
jmp short .ss
jmp short .invalid
jmp short .invalid
.cs:
mov ax, cs
ret
.ds:
mov ax, ds
ret
.es:
mov ax, es
ret
.fs:
mov ax, fs
ret
.gs:
mov ax, gs
ret
.ss:
mov ax, ss
ret
.invalid:
xor ax, ax
ret

global rdmsr
rdmsr:
rdmsr
shl rdx, 32
or rax, rdx
ret

global wrmsr
wrmsr:
mov rax, rdx
shr rdx, 32
wrmsr
ret

global read_cr0
read_cr0:
mov rax, cr0
ret

global write_cr0
write_cr0:
mov cr0, rcx
ret

global call_kernel
call_kernel:
;RCX: param (just pass this directly)
;RDX: kernel entry
;R8: kernel stack
;R9: stack size
add r8, r9
sub r8, 0x28
mov rsp, r8
push rbp
mov rbp, rsp
sub rsp, 32
call rdx
jmp $
ret