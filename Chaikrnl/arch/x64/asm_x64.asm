BITS 64
section .text


global x64_read_cr0
x64_read_cr0:
mov rax, cr0
ret

global x64_read_cr2
x64_read_cr2:
mov rax, cr2
ret

global x64_read_cr3
x64_read_cr3:
mov rax, cr3
ret

global x64_read_cr4
x64_read_cr4:
mov rax, cr4
ret

global x64_read_xcr0
x64_read_xcr0:
xor rcx, rcx
xgetbv
;RDX:RAX
shl rdx, 32
or rax, rdx
ret

global x64_write_cr0
x64_write_cr0:
mov cr0, rcx
ret

global x64_write_cr3
x64_write_cr3:
mov cr3, rcx
ret

global x64_write_cr4
x64_write_cr4:
mov cr4, rcx
ret

global x64_write_xcr0
x64_write_xcr0:
mov rax, rcx
mov rdx, rcx
xor rcx, rcx
shr rdx, 32
xsetbv
ret

global x64_xsave
x64_xsave:
xor rax, rax
xor rdx, rdx
dec rax
dec rdx
xsave [rcx]
ret

global x64_xrstor
x64_xrstor:
xor rax, rax
xor rdx, rdx
dec rax
dec rdx
xrstor [rcx]
ret

global x64_fxsave
x64_fxsave:
fxsave [rcx]
ret

global x64_fxrstor
x64_fxrstor:
fxrstor [rcx]
ret

global x64_rdmsr
x64_rdmsr:
rdmsr
shl rdx, 32
or rax, rdx
ret

global x64_wrmsr
x64_wrmsr:
mov rax, rdx
shr rdx, 32
wrmsr
ret

global x64_sgdt
x64_sgdt:
sgdt [rcx]
ret

global x64_lgdt
x64_lgdt:
lgdt [rcx]
ret

global x64_sidt
x64_sidt:
sidt [rcx]
ret

global x64_lidt
x64_lidt:
lidt [rcx]
ret

global x64_ltr
x64_ltr:
ltr cx
ret

global x64_cpuid
;Stack
;[RBP]: Backlink
;[RBP+8]: Return address
;[RBP+16]: RCX home, page
;[RBP+24]: RDX home, a*
;[RBP+32]: R8 home, b*
;[RBP+40]: R9 home, c*
;[RBP+48]: d*
;[RBP+56]: subpage

x64_cpuid:
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

global x64_get_segment_register
x64_get_segment_register:
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

global x64_set_segment_register
x64_set_segment_register:
and rcx, 0x7
mov r8, .jmp_tab
add r8, rcx
add r8, rcx
jmp r8
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
;mov cs, dx	//that's too easy, isn't it?
pop rax
push rdx
push rax
db 0x48		;REX.W
retf
.ds:
mov ds, dx
ret
.es:
mov es, dx
ret
.fs:
mov fs, dx
ret
.gs:
mov gs, dx
ret
.ss:
mov ss, dx
ret
.invalid:
xor ax, ax
ret

global x64_disable_interrupts
x64_disable_interrupts:
pushfq
cli
pop rax
ret

global x64_restore_flags
x64_restore_flags:
push rcx
popfq
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

global x64_inportb
x64_inportb:
mov dx, cx
in al, dx
ret

global x64_inportw
x64_inportw:
mov dx, cx
in ax, dx
ret

global x64_inportd
x64_inportd:
mov dx, cx
in eax, dx
ret

global x64_outportb
x64_outportb:
mov al, dl
mov dx, cx
out dx, al
ret

global x64_outportw
x64_outportw:
mov ax, dx
mov dx, cx
out dx, ax
ret

global x64_outportd
x64_outportd:
mov eax, edx
mov dx, cx
out dx, eax
ret

global x64_pause
x64_pause:
pause
ret

global x64_invlpg
x64_invlpg:
invlpg [rcx]
ret

global x64_mfence
x64_mfence:
mfence
ret

global x64_cacheflush
x64_cacheflush:
wbinvd
ret

global x64_fs_readb
x64_fs_readb:
xor rax, rax
mov al, [fs:rcx]
ret

global x64_fs_readw
x64_fs_readw:
xor rax, rax
mov ax, [fs:rcx]
ret

global x64_fs_readd
x64_fs_readd:
xor rax, rax
mov eax, [fs:rcx]
ret

global x64_fs_readq
x64_fs_readq:
mov rax, [fs:rcx]
ret

global x64_fs_writeb
x64_fs_writeb:
mov [fs:rcx], dl
ret

global x64_fs_writew
x64_fs_writew:
mov [fs:rcx], dx
ret

global x64_fs_writed
x64_fs_writed:
mov [fs:rcx], edx
ret

global x64_fs_writeq
x64_fs_writeq:
mov [fs:rcx], rdx
ret

global x64_gs_readb
x64_gs_readb:
xor rax, rax
mov al, [gs:rcx]
ret

global x64_gs_readw
x64_gs_readw:
xor rax, rax
mov ax, [gs:rcx]
ret

global x64_gs_readd
x64_gs_readd:
xor rax, rax
mov eax, [gs:rcx]
ret

global x64_gs_readq
x64_gs_readq:
mov rax, [gs:rcx]
ret

global x64_gs_writeb
x64_gs_writeb:
mov [gs:rcx], dl
ret

global x64_gs_writew
x64_gs_writew:
mov [gs:rcx], dx
ret

global x64_gs_writed
x64_gs_writed:
mov [gs:rcx], edx
ret

global x64_gs_writeq
x64_gs_writeq:
mov [gs:rcx], rdx
ret

global x64_bswapw
x64_bswapw:
xchg cl, ch
movzx eax, cx
ret

global x64_bswapd
x64_bswapd:
bswap ecx
mov eax, ecx
ret

global x64_bswapq
x64_bswapq:
bswap rcx
mov rax, rcx
ret

struc CONTEXT
.rip: resq 1
.rbx: resq 1
.rsi: resq 1
.rdi: resq 1
.rsp: resq 1
.rbp: resq 1
.r12: resq 1
.r13: resq 1
.r14: resq 1
.r15: resq 1
.rflags: resq 1
.floats: resy 10+1
.end:
endstruc

extern x64_avx_level

global x64_save_context
x64_save_context:
mov [rcx + CONTEXT.rbx], rbx
mov [rcx + CONTEXT.rsi], rsi
mov [rcx + CONTEXT.rdi], rdi
mov [rcx + CONTEXT.rbp], rbp
mov [rcx + CONTEXT.r12], r12
mov [rcx + CONTEXT.r13], r13
mov [rcx + CONTEXT.r14], r14
mov [rcx + CONTEXT.r15], r15
;RFLAGS
pushfq
pop rax
mov [rcx + CONTEXT.rflags], rax
mov rax, [qword x64_avx_level]
mov rdx, CONTEXT.floats
add rdx, rcx
cmp rax, 1
jge .avx_save
;Align to 16 byte boundary
and dl, 0xF0
add rdx, 0x10
movaps [rdx], xmm6
movaps [rdx+0x10], xmm7
movaps [rdx+0x20], xmm8
movaps [rdx+0x30], xmm9
movaps [rdx+0x40], xmm10
movaps [rdx+0x50], xmm11
movaps [rdx+0x60], xmm12
movaps [rdx+0x70], xmm13
movaps [rdx+0x80], xmm14
movaps [rdx+0x90], xmm15
jmp .finish
.avx_save:
;Align to 32 byte boundary
and dl, 0xE0
add rdx, 0x20
vmovaps [rdx], ymm6
vmovaps [rdx+0x20], ymm7
vmovaps [rdx+0x40], ymm8
vmovaps [rdx+0x60], ymm9
vmovaps [rdx+0x80], ymm10
vmovaps [rdx+0x100], ymm11
vmovaps [rdx+0x120], ymm12
vmovaps [rdx+0x140], ymm13
vmovaps [rdx+0x160], ymm14
vmovaps [rdx+0x180], ymm15
.finish:
;Now sort out the returning
pop rdx		;Return address
mov [rcx + CONTEXT.rip], rdx
mov [rcx + CONTEXT.rsp], rsp
xor rax, rax
jmp rdx

global x64_load_context
x64_load_context:
mov r8, rdx		;Return value, if any
mov rbx, [rcx + CONTEXT.rbx]
mov rsi, [rcx + CONTEXT.rsi]
mov rdi, [rcx + CONTEXT.rdi]
mov rbp, [rcx + CONTEXT.rbp]
mov r12, [rcx + CONTEXT.r12]
mov r13, [rcx + CONTEXT.r13]
mov r14, [rcx + CONTEXT.r14]
mov r15, [rcx + CONTEXT.r15]
mov rax, [qword x64_avx_level]
mov rdx, CONTEXT.floats
add rdx, rcx
cmp rax, 1
jge .avx_save
;Align to 16 byte boundary
and dl, 0xF0
add rdx, 0x10
movaps xmm6, [rdx]
movaps xmm7, [rdx+0x10]
movaps xmm8, [rdx+0x20]
movaps xmm9, [rdx+0x30]
movaps xmm10, [rdx+0x40]
movaps xmm11, [rdx+0x50]
movaps xmm12, [rdx+0x60]
movaps xmm13, [rdx+0x70]
movaps xmm14, [rdx+0x80]
movaps xmm15, [rdx+0x90]
jmp .finish
.avx_save:
;Align to 32 byte boundary
and dl, 0xE0
add rdx, 0x20
vmovaps ymm6, [rdx]
vmovaps ymm7, [rdx+0x20]
vmovaps ymm8, [rdx+0x40]
vmovaps ymm9, [rdx+0x60]
vmovaps ymm10, [rdx+0x80]
vmovaps ymm11, [rdx+0x100]
vmovaps ymm12, [rdx+0x120]
vmovaps ymm13, [rdx+0x140]
vmovaps ymm14, [rdx+0x160]
vmovaps ymm15, [rdx+0x180]
.finish:
; Now returning
mov r9, 1
cmp r8, 0
cmove r8, r9
mov r9, [rcx + CONTEXT.rflags]
mov rsp, [rcx + CONTEXT.rsp]
mov rdx, [rcx + CONTEXT.rip]
mov rax, r8
push r9
popfq
jmp rdx

global x64_new_context
x64_new_context:
;RCX is new context, RDX is new stack, R8 is entry point
mov [rcx + CONTEXT.rsp], rdx
mov [rcx + CONTEXT.rip], r8
pushfq
pop rax
or rax, 0x200		;Interrupts enabled
mov [rcx + CONTEXT.rflags], rax
ret

global x64_go_usermode
x64_go_usermode:
;RCX: stackptr RDX: RIP R8: CS R9: DS [stack] - TSS
mov r10, [rsp+40]
mov rax, [rsp+48]

;Save stack to TSS
mov [r10 + 0x4], rsp	;Execute on current kernel stack
;Flush TSS
;jmp $
;ltr ax
;Prepare for IRETQ
pushfq
pop rax

or ax, (1<<9)	;Interrupt enable
push r9		;SS
push rcx		;RSP
push rax		;RFLAGS
push r8		;CS
push rdx		;RIP

;mov ds, r9w		;Load segment registers
;mov es, r9w
;mov fs, r9w	;FS is kernel per-cpu data
;mov gs, r9w
cli
swapgs			;Load User TLS

iretq			;Go to user mode!
.end:
ret

global x64_hlt
x64_hlt:
hlt
ret

global x64_rdtsc
x64_rdtsc:
rdtsc
shl rdx, 32
or rax, rdx
ret

global x64_set_breakpoint
x64_set_breakpoint:
mov dr0, rcx
dec rdx
xor rax, rax
mov al, 0b10	;Global breakpoint enable
shl rdx, 18
shl r8, 16
or rdx, r8
and rdx, 0xF0000
or rax, rdx
mov dr7, rax
ret

global x64_enable_breakpoint
x64_enable_breakpoint:
mov rax, dr7
mov rdx, 0b10
or rdx, rax
and al, 0b11111101
cmp rcx, 1
cmove rax, rdx
mov dr7, rax
ret

global user_function
user_function:
syscall
jmp short user_function

section .data
global x64_context_size
x64_context_size: dq CONTEXT.end

global _fltused
_fltused:
ret
