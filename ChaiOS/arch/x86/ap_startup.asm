BITS 16
org 0xA000
section .text
cli
cld
jmp 0:next
next:
xor ax, ax
mov ds, ax
lgdt [gdtr]
mov eax, cr0
or eax, 1
mov cr0, eax
jmp 0x8:pmode
BITS 32
pmode:
mov ax, 0x10
mov ds, ax
mov es, ax
mov ss, ax
cmp DWORD[0x1000], 64
je enterlongmode
mov eax, [0x1000+8]
mov cr3, eax
mov eax, cr0
or eax, (1<<31)
mov cr0, eax
;We're now in the kernel address space
jmp $

enterlongmode:
mov edi, 0x2000		;Temporary paging
mov cr3, edi
xor eax, eax
mov ecx, 4*1024 	;Enough to identity map low memory
rep stosd
mov edi, 0x2000
mov DWORD[edi], 0x3003
mov DWORD[edi+4], 0
add di, 0x1000
mov DWORD[edi], 0x4003
mov DWORD[edi+4], 0
add di, 0x1000
mov DWORD[edi], 0x5003
mov DWORD[edi+4], 0
add di, 0x1000
;This is the page table
mov eax, 0x3
mov ecx, 512
.setentry:
mov DWORD[edi], eax
add eax, 0x1000
add edi, 0x8
loop .setentry
;Enable PAE
mov eax, cr4
or eax, (1<<5)
mov cr4, eax
;Enable long mode
mov ecx, 0xC0000080
rdmsr
or eax, 1<<8
wrmsr
;Actually enable paging
mov eax, cr0
or eax, (1<<31)
mov cr0, eax
;We're in compatibility mode, load 64 bit GDT
lgdt [gdtr64]
jmp 0x8:longmode
BITS 64
longmode:
mov ax, 0x10
mov ds, ax
mov es, ax
mov ss, ax
mov rax, [0x1000+0x10]
mov cr3, rax
;We're now in the kernel address space
mov rcx, 1
.waits:
mov rax, 0
lock cmpxchg [0x1000+0x8], rcx
jnz .waits
;Now wait for our data area
.infoloop:
cmp [0x1000+0x8], rcx
je .infoloop
mov rdx, [0x1000+0x8]
mov QWORD[0x1000+0x8], 0
;Now wait for a stack
mov rcx, 0
mov rax, 0
.waitstack:
lock cmpxchg [rdx+8], rcx
je .waitstack
mov rsp, rax
mov rcx, [rdx]
mov rbx, [0x1000+0x20]	;acquire_spinlock
mov r12, rdx
mov r13, [0x1000+0x28]	;release_spinlock
mov r14, rcx
.get_lock:
call rbx
;Spinlock acquired
cmp QWORD[r12+0x10], 0
jne .entry_written
mov rdx, rax
mov rcx, r14
call r13
pause
jmp .get_lock
.entry_written:
mov rdx, rax
mov rcx, r14
call r13
;Now we have the entry routine!
mov rdx, [r12+0x10]
mov rcx, [r12+0x18]
call rdx

section .data
align 8
gdt:
;0 selector
dq 0
;0x8
dw 0xFFFF
dw 0
db 0
db 0x9A
db 0xCF
db 0
;0x10
dw 0xFFFF
dw 0
db 0
db 0x92
db 0xCF
db 0
gdtr:
dw $-gdt-1
dd gdt

align 8
gdt64:
;NULL
dq 0
; 0x8
dw 0
dw 0
db 0
db 0x9A
db 0xAF
db 0
; 0x10
dw 0
dw 0
db 0
db 0x92
db 0
db 0

gdtr64:
dw $-gdt64-1
dd gdt64