;/********************************************************** 
;ChaiOS 0.05 Copyright (C) 2012-2014 Nathaniel Cleland
;Licensed under GPLv2
;See License for full details
;
;Project: ChaiOS
;File: stack.asm
;Path: C:\Users\Nathaniel\Documents\Visual Studio 2013\Projects\ChaiOS\ChaiOS\arch\i386\stack.asm
;Created by Nathaniel on 31/10/2014 at 15:49
;
;Description: Stack management functions
;**********************************************************/
BITS 32
segment .text

global _setStack
_setStack:
mov edx, [esp]
mov esp, [esp+4]
jmp edx

global _getStack
_getStack:
mov eax, esp
ret


global _getBase
_getBase:
mov eax, ebp
ret

global _storeMBinfo
_storeMBinfo:
mov [ebp+0x8], ebx
ret
