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