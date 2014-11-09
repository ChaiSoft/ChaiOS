/********************************************************** 
ChaiOS 0.05 Copyright (C) 2012-2014 Nathaniel Cleland
Licensed under GPLv2
See License for full details

Project: ChaiOS
File: compiler.h
Path: C:\Users\Nathaniel\Documents\Visual Studio 2013\Projects\ChaiOS\Include\compiler.h
Created by Nathaniel on 31/10/2014 at 10:36

Description: Includes standard headers and compiler specific stuff
			 That includes assembly functions
**********************************************************/
#ifndef CHAIOS_COMPIILER_H
#define CHAIOS_COMPIILER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <iso646.h>
#include <limits.h>
#include <stdarg.h>

#define UNUSED(x) \
	(void)x

//Non standard includes
#ifdef _MSC_VER
#include "msvc.h"
#elif defined __GNUC__
#include "gnuc.h"
//TODO: solve
#error "GNUC needs implementing"
#else
//TODO: solve
#error "Unsupported Compiler"
#endif	//COMPILER_SELECT

#include "registers.h"

#endif	//CHAIOS_COMPIILER_H
