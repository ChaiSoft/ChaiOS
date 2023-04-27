[BITS 64]
section .text
;void * memset( void * s, int c, size_t n );
;RCX - s
;RDX - c
; R8 - n
global memset
export memset
memset:
mov r10, rcx
;Prepare rdx value
and rdx, 0xFF
mov rax, rdx
mov r9, has_erpmsb
cmp DWORD[r9], 1
je erepmemset
mov rdx, 0x0101010101010101
mul rdx
cmp r8, 16
;Very short, this will use simple writes
jl .mainLoop
;First things first, align
mov QWORD[rcx], rax
mov QWORD[rcx+8], rax
mov rdx, 0x0F
mov r9, rcx
and r9, rdx
;Unaligned count -> R9
not rdx
and rcx, rdx
add rcx, 16
add r8, r9
sub r8, 16
;Ready for aligned SSE section
movq xmm0, rax
unpcklpd xmm0, xmm0		;XMM0 now contains what we need
.mainLoop:
bsr r9, r8
;R9 now contains log2(r8) (length remaining)
jz .done
mov rdx, memset_jumptab
jmp [rdx + r9*8]
;Unrolled SSE loop
.Sse16:
movntpd [rcx], xmm0
movntpd [rcx+0x10], xmm0
movntpd [rcx+0x20], xmm0
movntpd [rcx+0x30], xmm0
movntpd [rcx+0x40], xmm0
movntpd [rcx+0x50], xmm0
movntpd [rcx+0x60], xmm0
movntpd [rcx+0x70], xmm0
mov r9, 0x80
add rcx, r9
sub r8, r9
.Sse8:
movntpd [rcx], xmm0
movntpd [rcx+0x10], xmm0
movntpd [rcx+0x20], xmm0
movntpd [rcx+0x30], xmm0
mov r9, 0x40
add rcx, r9
sub r8, r9
.Sse4:
movntpd [rcx], xmm0
movntpd [rcx+0x10], xmm0
mov r9, 0x20
add rcx, r9
sub r8, r9
.Sse2:
movntpd [rcx], xmm0
mov r9, 0x10
add rcx, r9
sub r8, r9
.Sse1:
movntpd [rcx], xmm0
mov r9, 0x10
add rcx, r9
sub r8, r9
jmp .mainLoop

;Finish the job
.Move16:
mov QWORD[rcx], rax
mov r9, 0x8
add rcx, r9
sub r8, r9
.Move8:
mov DWORD[rcx], eax
mov r9, 4
add rcx, r9
sub r8, r9
.Move4:
mov WORD[rcx], ax
mov r9, 2
add rcx, r9
sub r8, r9
.Move2:
mov BYTE[rcx], al
inc rcx
dec r8
.Move1:
mov BYTE[rcx], al
inc rcx
dec r8
jmp .mainLoop
.done:
mov rax, r10
ret

erepmemset:
xchg rdi, rcx
xchg rcx, r8
rep stosb
mov rdi, r8
mov rax, r10
ret


section .rdata
memset_jumptab:		;log2(length)
.zero: dq memset.Move1
.one: dq memset.Move2
.two: dq memset.Move4
.three: dq memset.Move8
.four: dq memset.Sse1
.five: dq memset.Sse2
.six: dq memset.Sse4
.seven: dq memset.Sse8
.eight: dq memset.Sse16
;Now the enhanced rep performance takes hold
TIMES (64-8) dq memset.Sse16

section .data
has_erpmsb: dq 1