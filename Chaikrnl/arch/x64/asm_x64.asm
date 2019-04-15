section .text
BITS 64

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