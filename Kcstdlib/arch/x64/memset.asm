section .text
BITS 64

%ifdef USE_AVX2
global memset_avx2
;rcx: ptr
;rdx: value
;r8: num
memset_avx2:
;Broadcast char to all of rdx
mov rax, 0x0101010101010101
mul rdx
;Check it's a long one
cmp r8, 64
jl short_memset
;RAX now contains 8 bytes of what we want to write. We now align the memory address
mov [rcx], rax
mov [rcx+8], rax
mov [rcx+16], rax
mov [rcx+24], rax
push rax
vbroadcastss ymm0, [rsp]
add rsp, 8
;Align pointer upwards, count towards count
mov rax, rcx
add rax, 31
and ax, 0xE0
mov rdx, rax
sub rdx, rcx
sub r8, rdx
vmovaps [rax], ymm0
vmovaps [rax+32], ymm0
ret
%endif

short_memset:
;RAX already contains nicely prepared data for us
.loop:
cmp r8, 0
je .end
cmp r8, 8
jge .large
;Less than 8 bytes left
mov [rcx], al
add rcx, 1
sub r8, 1
jmp .loop
.large:
mov [rcx], rax
add rcx, 8
sub r8, 8
jmp .loop
.end:
ret

export memset
global memset
memset:
push rdi
mov rdi, rcx
mov rax, rdx
mov rcx, r8
rep stosb
pop rdi
ret

export memcmp
global memcmp
memcmp:
push rsi
push rdi
push rcx
mov rsi, rcx
mov rdi, rdx
mov rcx, r8
repz cmpsb
mov rax, -1
mov rcx, 1
mov rdx, 0
;cmovl rax, rax
cmovg rax, rcx
cmove rax, rdx
pop rax
pop rdi
pop rsi
ret

mone: dq -1
one: dq 1
zero: dq 0