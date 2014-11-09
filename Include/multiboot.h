/********************************************************** 
ChaiOS 0.05 Copyright (C) 2012-2014 Nathaniel Cleland
Licensed under GPLv2
See License for full details

Project: ChaiOS
File: multiboot.h
Path: C:\Users\Nathaniel\Documents\Visual Studio 2013\Projects\ChaiOS\Include\multiboot.h
Created by Nathaniel on 31/10/2014 at 12:34

Description: Multiboot structures
**********************************************************/
#ifndef CHAIOS_MULTIBOOT_H
#define CHAIOS_MULTIBOOT_H
#include <stdint.h>
#include <address.h>


//! header offset will always be this
#define   MB_ALIGN                           0x400
#define   HEADER_ADDRESS         LOADBASE+MB_ALIGN

#define MULTIBOOT_HEADER_MAGIC        0x1BADB002
#define MULTIBOOT_HEADER_FLAGS        0x00010003
#define STACK_SIZE                    0x4000    
#define CHECKSUM                      -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)

#pragma pack (push, 1)

/**
*   Multiboot structure
*/
typedef struct _MULTIBOOT_STRUCT {

	uint32_t magic;
	uint32_t flags;
	uint32_t checksum;
	uint32_t headerAddr;
	uint32_t loadAddr;
	uint32_t loadEndAddr;
	uint32_t bssEndAddr;
	uint32_t entryPoint;
	uint32_t modType;
	uint32_t width;
	uint32_t height;
	uint32_t depth;

}MULTIBOOT_BOOT_STRUCT, *PMULTIBOOT_BOOT_STRUCT;

typedef struct _MULTIBOOT_INFO {

	uint32_t flags;			//required
	uint32_t memLower;		//if bit 0 in flags are set
	uint32_t memUpper;		//if bit 0 in flags are set
	uint32_t bootDevice;		//if bit 1 in flags are set
	uint32_t commandLine;		//if bit 2 in flags are set
	uint32_t moduleCount;		//if bit 3 in flags are set
	uint32_t moduleAddress;		//if bit 3 in flags are set
	uint32_t syms[4];		//if bits 4 or 5 in flags are set
	uint32_t memMapLength;		//if bit 6 in flags is set
	uint32_t memMapAddress;		//if bit 6 in flags is set
	uint32_t drivesLength;		//if bit 7 in flags is set
	uint32_t drivesAddress;		//if bit 7 in flags is set
	uint32_t configTable;		//if bit 8 in flags is set
	uint32_t apmTable;		//if bit 9 in flags is set
	uint32_t vbeControlInfo;	//if bit 10 in flags is set
	uint32_t vbeModeInfo;		//if bit 11 in flags is set
	uint32_t vbeMode;		// all vbe_* set if bit 12 in flags are set
	uint32_t vbeInterfaceSeg;
	uint32_t vbeInterfaceOff;
	uint32_t vbeInterfaceLength;

}MULTIBOOT_INFO, *PMULTIBOOT_INFO;

#pragma pack(pop,1)

#endif	//CHAIOS_MULTIBOOT_H