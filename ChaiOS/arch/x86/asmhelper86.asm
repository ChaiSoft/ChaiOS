BITS 32

section .text

global _inportb
_inportb:
xor eax, eax
mov dx, [esp+4]
in al, dx
ret

global _inportw
_inportw:
xor eax, eax
mov dx, [esp+4]
in ax, dx
ret

global _inportd
_inportd:
xor eax, eax
mov dx, [esp+4]
in eax, dx
ret

global _outportb
_outportb:
mov al, [esp+8]
mov dx, [esp+4]
out dx, al
ret

global _outportw
_outportw:
mov ax, [esp+8]
mov dx, [esp+4]
out dx, ax
ret

global _outportd
_outportd:
mov eax, [esp+8]
mov dx, [esp+4]
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
mov ebp, esp
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

global _compareswap
_compareswap:
mov eax, [esp+8]
mov ecx, [esp+4]
mov edx, [esp+12]
lock cmpxchg [ecx], edx
jz .true
mov eax, 0
ret
.true:
mov eax, 1
ret

global _disable:
_disable:
pushfd
cli
pop eax
ret

global _restore:
_restore:
mov ecx, [esp+4]
push ecx
popfd
ret

global _memory_barrier
_memory_barrier:
mfence
ret

global _get_paging_root
_get_paging_root:
mov eax, cr3
ret

global _set_paging_root
_set_paging_root:
mov eax, [esp+4]
mov cr3, eax
ret