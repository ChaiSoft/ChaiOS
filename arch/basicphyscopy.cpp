/********************************************************** 
ChaiOS 0.05 Copyright (C) 2012-2014 Nathaniel Cleland
Licensed under GPLv2
See License for full details

Project: ChaiOS
File: basicphyscopy.cpp
Path: C:\Users\Nathaniel\Documents\Visual Studio 2013\Projects\ChaiOS\ChaiOS\arch\basicphyscopy.cpp
Created by Nathaniel on 2/11/2014 at 18:13

Description: physical address copying
**********************************************************/
#define TERAOS_EARLY

#include "stdafx.h"
#include "basicpaging.h"

void basic_physical_to_virtual_copy(void* dest, void* physsrc, size_t len)
{
	void* virtphy = basic_paging_create_mapping(physsrc);
	size_t offset = 0;
	size_t bytesread = (PAGESIZE - ((size_t)virtphy - ((size_t)virtphy & (PAGESIZE - 1))));
	do	{
		for (int n = 0; ((size_t)&((uint8_t*)virtphy)[n] & 0xFFF); n++)
		{
			((uint8_t*)dest)[offset + n] = ((uint8_t*)virtphy)[n];
		}
		if (bytesread < len)
		{
			offset = bytesread;
			bytesread += PAGESIZE;
			virtphy = basic_paging_create_mapping((void*)((uint8_t*)physsrc)[offset]);
		}
	} while (bytesread < len);
}