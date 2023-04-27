[BITS 64]
section .text
;void * memcpy( void * _PDCLIB_restrict s1, const void * _PDCLIB_restrict s2, size_t n )
;RCX - s1
;RDX - s2
; R8 - n
export memcpy
global memcpy
memcpy:
jmp erepMemcpy
cmp r8, 0
;Zero length
je .end
;At least one byte to copy
bsr r9, r8
;R9 now contains floor(log2(n))
mov rax, memcpy_jumptab
jmp [rax + r9*8]
.end:
ret

byteMemcpy:
xchg r8, rcx
.copy:
mov r9b, BYTE[rdx]
mov BYTE[r8], r9b
loop .copy
ret

erepMemcpy:
xchg rsi, rdx
xchg rdi, rcx
xchg r8, rcx
rep movsb
mov rdi, r8
xchg rsi, rdx
ret

section .rdata
memcpy_jumptab:		;log2(length)
.zero: dq byteMemcpy
.one: dq byteMemcpy
.two: dq byteMemcpy
.three: dq byteMemcpy
.four: dq byteMemcpy
.five: dq byteMemcpy
.six: dq byteMemcpy
.seven: dq byteMemcpy
;Now the enhanced rep performance takes hold
TIMES (64-8) dq erepMemcpy