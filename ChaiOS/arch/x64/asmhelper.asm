BITS 64

section .text

inportb:
xor rax, rax
mov dx, cx
in al, dx
ret