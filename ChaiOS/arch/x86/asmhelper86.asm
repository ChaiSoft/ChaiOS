BITS 32

section .text

global _inportb
_inportb:
xor eax, eax
mov dx, [ebp+4]
in al, dx
ret

global _inportw
_inportw:
xor rax, rax
mov dx, [ebp+4]
in ax, dx
ret

global _inportd
_inportd:
xor rax, rax
mov dx, [ebp+4]
in eax, dx
ret

global _outportb
_outportb:
mov al, [ebp+8]
mov dx, [ebp+4]
out dx, al
ret

global outportw
outportw:
mov ax, [ebp+8]
mov dx, [ebp+4]
out dx, ax
ret

global outportd
outportd:
mov eax, [ebp+8]
mov dx, [ebp+4]
out dx, eax
ret

global _cpuid
;Stack
;[EBP-4]: EBX
;[EBP]: Backlink
;[EBP+4]: Return address
;[EBP+8]: page
;[EBP+12]: a*
;[EBP+16]: b*
;[EBP+20]: c*
;[EBP+24]: d*
;[EBP+28]: subpage

_cpuid:
push ebp
mov ebp, rsp
push ebx
mov eax, [ebp+8]
mov ecx, [ebp+28]
cpuid
push ebx
;[EBP-8]: EBX from CPUID
mov ebx, [ebp+12]
mov [ebx], eax
mov eax, [ebp+16]
pop ebx
mov [eax], ebx
mov eax, [ebp+20]
mov [eax], ecx
mov eax, [ebp+24]
mov [eax], edx
pop ebx
pop ebp
ret

global _cacheflush
_cacheflush:
wbinvd
ret