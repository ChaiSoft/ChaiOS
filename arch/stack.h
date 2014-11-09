/********************************************************** 
ChaiOS 0.05 Copyright (C) 2012-2014 Nathaniel Cleland
Licensed under GPLv2
See License for full details

Project: ChaiOS
File: stack.h
Path: C:\Users\Nathaniel\Documents\Visual Studio 2013\Projects\ChaiOS\ChaiOS\arch\stack.h
Created by Nathaniel on 31/10/2014 at 15:57

Description: Stack management (asm) functions
**********************************************************/
#ifndef CHAIOS_STACK_H
#define CHAIOS_STACK_H

#ifdef __cplusplus
extern "C" {
#endif	//__cplusplus

void __cdecl setStack(void* newstack);
void* __cdecl getStack();
void* __cdecl getBase();

#ifdef X86
//X86 specific, x64bootstrap sort it out otherwise
void __cdecl storeMBinfo();
#else
//Void, this is not X86
#define storeMBinfo()
#endif	//X86

#ifdef __cplusplus
}
#endif	//__cplusplus

#endif	//CHAIOS_STACK_H
