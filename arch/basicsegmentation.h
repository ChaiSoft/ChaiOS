/********************************************************** 
ChaiOS 0.05 Copyright (C) 2012-2014 Nathaniel Cleland
Licensed under GPLv2
See License for full details

Project: ChaiOS
File: basicsegmentation.h
Path: C:\Users\Nathaniel\Documents\Visual Studio 2013\Projects\ChaiOS\ChaiOS\arch\basicsegmentation.h
Created by Nathaniel on 2/11/2014 at 12:33

Description: Sets upa flat segmented memory model on segmented arch
**********************************************************/
#ifndef CHAIOS_BASIC_SEGMENTATION_H
#define CHAIOS_BASIC_SEGMENTATION_H

#ifdef TERAOS_EARLY

#define SEGMENTED_ARCH (X86 || X64)

#if !(SEGMENTED_ARCH)		//architectures with segmentation
//void for architectures without segmentation
#define basic_segmentaion_setup()
#else
//architectures with segmentation
void basic_segmentaion_setup();
#endif	//SEGMENTED_ARCH

#endif	//TERAOS_EARLY

#endif	//CHAIOS_BASIC_SEGMENTATION_H
