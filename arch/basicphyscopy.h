/********************************************************** 
ChaiOS 0.05 Copyright (C) 2012-2014 Nathaniel Cleland
Licensed under GPLv2
See License for full details

Project: ChaiOS
File: basicphyscopy.h
Path: C:\Users\Nathaniel\Documents\Visual Studio 2013\Projects\ChaiOS\ChaiOS\arch\basicphyscopy.h
Created by Nathaniel on 2/11/2014 at 18:18

Description: Physical address copying
**********************************************************/
#ifndef CHAIOS_BASIC_PHYSICAL_COPY_H
#define CHAIOS_BASIC_PHYSICAL_COPY_H

#ifdef TERAOS_EARLY		//Only early functions get basic stuff

void basic_physical_to_virtual_copy(void* dest, void* physsrc, size_t len);

#endif	//TERAOS_EARLY

#endif	//CHAIOS_BASIC_PHYSICAL_COPY_H
