/********************************************************** 
ChaiOS 0.05 Copyright (C) 2012-2014 Nathaniel Cleland
Licensed under GPLv2
See License for full details

Project: ChaiOS
File: address.h
Path: C:\Users\Nathaniel\Documents\Visual Studio 2013\Projects\ChaiOS\Include\address.h
Created by Nathaniel on 31/10/2014 at 13:08

Description: Address definitions
**********************************************************/
#ifndef CHAIOS_ADDRESS_H
#define CHAIOS_ADDRESS_H
//Load address and kernel base address, and similar stuff
#define LOADBASE 0x100000

#ifdef X86
#define PAGESIZE 4096
#define KERNEL_BASE 0xC0000000
#define KERNEL_STACK_BASE 0xF5000000
#define LOADBASE_EARLY LOADBASE
#elif defined X64
#define PAGESIZE 4096
#define KERNEL_BASE 0xFFFFC00000000000
#define KERNEL_STACK_BASE 0xFFFFF50000000000
#define LOADBASE_EARLY 0xFFFFC00000000000		//our bootstrap sets up paging
#endif	//ARCH_SELECT

#define MAKE_PHYSICAL_EARLY(x) \
	((void*)((size_t)x - (size_t)KERNEL_BASE + (size_t)LOADBASE_EARLY))

#define MAKE_VIRTUAL_EARLY(x) \
	((void*)((size_t)x + (size_t)KERNEL_BASE - (size_t)LOADBASE_EARLY))

#endif	//CHAIOS_ADDRESS_H