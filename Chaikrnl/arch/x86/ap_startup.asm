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
xor rcx, rcx
xor rax, rax
mov rcx, 1
.waits:
mov rax, 0
lock cmpxchg [0x1000+0x8], rcx
pause
jnz .waits
;Now wait for our data area
mov rcx, 1
.infoloop:
mov rax, 1
lock cmpxchg [0x1000+0x8], rcx
pause
je .infoloop
mov r12, rax
;Now wait for a stack
xor rcx, rcx
xor rax, rax
mov [0x1000+0x8], rax
.waitstack:
lock cmpxchg [r12+8], rcx
pause
je .waitstack
mov rsp, rax
;Wait for routine and param to be written
xor rcx, rcx
xor rax, rax
.funcl:
lock cmpxchg [r12+0x10], rcx
pause
jz .funcl
xor rax, rax
.paraml:
lock cmpxchg [r12+0x18], rcx
pause
jz .paraml
;Now we have the entry routine!
;Load the final GDT
;mov ax, [r12+0x22]		;DS
;lgdt [r12+0x30]
;mov ds, ax
sgdt [0x1800]
;mov ax, [r12+0x24]		;ES
;mov es, ax
;mov ax, [r12+0x26]		;FS
;mov fs, ax
;mov ax, [r12+0x28]		;GS
;mov gs, ax
;mov ax, [r12+0x2A]		;SS
;mov ss, ax
;jmp $
;mov ax, [r12+0x20]		;CS
;mov cs, ax
;jmp .kernel
.kernel:
mov rdx, [r12+0x10]
mov rcx, [r12+0x18]
;Align the stack
and sp, 0xFFF0
mov rbp, rsp
sub rsp, 32	;shadow space for paramters, used by compiler
invlpg [rsp]
jmp rdx


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
dw 0xFFFF
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
db 0xCF
db 0

gdtr64:
dw $-gdt64-1
dd gdt64
