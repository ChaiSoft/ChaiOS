/********************************************************** 
ChaiOS 0.05 Copyright (C) 2012-2014 Nathaniel Cleland
Licensed under GPLv2
See License for full details

Project: ChaiOS
File: pagingdefs.h
Path: C:\Users\Nathaniel\Documents\Visual Studio 2013\Projects\ChaiOS\ChaiOS\arch\i386\pagingdefs.h
Created by Nathaniel on 31/10/2014 at 15:24

Description: x86 Paging definitions
**********************************************************/
#ifndef CHAIOS_X86_PAGING_DEFINES_H
#define CHAIOS_X86_PAGING_DEFINES_H

#ifdef X86

#define PAGING_PRESENT 1
#define PAGING_WRITE 2
#define PAGING_USER 4

#define PAGE_TABLE_ENTRIES 1024

#define PAGE_TABLE_LOW_ENTRY 0
#define PAGE_TABLE_KERNEL_ENTRY (KERNEL_BASE>>22)
#define PAGE_TABLE_STACK_ENTRY (KERNEL_STACK_BASE>>22)

#define PAGE_TABLE_FRACTAL_ENTRY (PAGE_TABLE_ENTRIES-1)

#define STACK_PHY_BASE 0x10000
#define STACK_LENGTH 0x4000

#define MMU_RECURSIVE_SLOT      (PAGE_TABLE_ENTRIES-1)
#define MMU_PD_INDEX(addr)      ((((uintptr_t)(addr))>>22) & 1023)
#define MMU_PT_INDEX(addr)      ((((uintptr_t)(addr))>>12) & 1023)

// Base address for paging structures
#define KADDR_MMU_PT            (MMU_RECURSIVE_SLOT<<22)
#define KADDR_MMU_PD            (KADDR_MMU_PT + (MMU_RECURSIVE_SLOT<<12))

#define MMU_PD(addr)            ((uint32_t*)( KADDR_MMU_PD ))
#define MMU_PT(addr)            ((uint32_t*)( KADDR_MMU_PT + (size_t)addr/1024 &(~(size_t)0xFFF) ))

#endif	//X86

#endif	//CHAIOS_X86_PAGING_DEFINES_H
