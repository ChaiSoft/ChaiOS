/********************************************************** 
ChaiOS 0.05 Copyright (C) 2012-2014 Nathaniel Cleland
Licensed under GPLv2
See License for full details

Project: ChaiOS
File: basicsegmentationx86.cpp
Path: C:\Users\Nathaniel\Documents\Visual Studio 2013\Projects\ChaiOS\ChaiOS\arch\i386\basicsegmentationx86.cpp
Created by Nathaniel on 2/11/2014 at 12:45

Description: Sets up flat segmentation
**********************************************************/
#define TERAOS_EARLY

#include "stdafx.h"
#include "basicsegmentation.h"

ALIGN(4, uint64_t gdt[])
{
	0,
	0x00209A0000000000,
	0x0000920000000000
};
#pragma pack(push,1)
struct gdtdesc {
	uint16_t length;
	size_t addr;
};
#pragma pack(pop,1)
ALIGN(4, gdtdesc thegdt)
{
	sizeof(gdt)-1,
	(size_t)gdt
};

void basic_segmentaion_setup()
{
	LGDT(&thegdt);
}
