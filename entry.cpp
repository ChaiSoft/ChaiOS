/********************************************************** 
ChaiOS 0.05 Copyright (C) 2012-2014 Nathaniel Cleland
Licensed under GPLv2
See License for full details

Project: ChaiOS
File: entry.cpp
Path: C:\Users\Nathaniel\Documents\Visual Studio 2013\Projects\ChaiOS\ChaiOS\entry.cpp
Created by Nathaniel on 31/10/2014 at 10:25

Description: ChaiOS kernel entry point
**********************************************************/
#define TERAOS_EARLY

#include "stdafx.h"
#include "multiboot.h"
#include "basicpaging.h"
#include "basicsegmentation.h"
#include "basicphyscopy.h"
#include "stack.h"

void displayWelcome()
{
	unsigned short* vidmem = (unsigned short*)0xB8000;
	char* welcome = "ChaiOS 0.05 Loaded. Copyright (C) 2014 Nathaniel Cleland";
	welcome = (char*)MAKE_PHYSICAL_EARLY(welcome);
	for (int n = 0; welcome[n]; n++)
		vidmem[n] = welcome[n] | (0x3F << 8);
}

PMULTIBOOT_INFO multiboot = 0;

MULTIBOOT_INFO mbinfo = { 0 };

extern "C" void entry(PMULTIBOOT_INFO info)
{
	//Get MBINFO as parameter is expected, instead of in ebx
#ifndef storeMBinfo		//Funny inversion, but it works
	((void(*)())MAKE_PHYSICAL_EARLY(storeMBinfo))();
#endif
	//Store MBINFO for future reference
	*(PMULTIBOOT_INFO*)MAKE_PHYSICAL_EARLY(&multiboot) = info;
	//Display a welcome message ?????????? TODO: Please Deprecate when ready
	((void(*)())MAKE_PHYSICAL_EARLY(displayWelcome))();
	//Now setup basic paging
	//We need to call this at LOADBASE offset
	((void(*)())MAKE_PHYSICAL_EARLY(basic_paging_setup))();
	//We are now officially paged. We can now start more advanced stuff
	basic_segmentaion_setup();
	//Now we have segmentation on segmented architectures in the kernel. Flat memory model
	//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!MEMORY SORTED!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	//We are now self reliant in terms of memory
	//We now need to get the multiboot info
	basic_physical_to_virtual_copy(&mbinfo, multiboot, sizeof(MULTIBOOT_INFO));
	//Just sorting this out
	multiboot = &mbinfo;
	info = multiboot;
	//Now we have the basics
	//We can now start getting ambitious
	//Now we need the full memory manager

	DISABLE_INTERRUPTS();
	CPU_HALT();
}

#if defined X86		//Solution is different on x64
#pragma code_seg(".code$0")
__declspec(allocate(".code$0"))
MULTIBOOT_BOOT_STRUCT mbstruct{
	MULTIBOOT_HEADER_MAGIC,
	MULTIBOOT_HEADER_FLAGS,
	(uint32_t)CHECKSUM,
	(uint32_t)MAKE_PHYSICAL_EARLY(&mbstruct),
	LOADBASE,
	0, //load end address
	0, //bss end address
	(uint32_t)MAKE_PHYSICAL_EARLY(&entry)
};

#pragma comment (linker,"/merge:.text=.code")

#endif
