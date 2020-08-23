section .text
BITS 64

export memcpy
global memcpy
memcpy:
mov rax, rcx
push rsi
push rdi
mov rdi, rcx
mov rsi, rdx
mov rcx, r8
rep movsb
pop rdi
pop rsi
ret

export _purecall
global _purecall
_purecall:
ret