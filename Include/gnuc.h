/********************************************************** 
ChaiOS 0.05 Copyright (C) 2012-2014 Nathaniel Cleland
Licensed under GPLv2
See License for full details

Project: ChaiOS
File: gnuc.h
Path: C:\Users\Nathaniel\Documents\Visual Studio 2013\Projects\ChaiOS\Include\gnuc.h
Created by Nathaniel on 31/10/2014 at 10:52

Description: GNU C compiler specific definitions
**********************************************************/
#ifndef CHAIOS_COMPILER_GNUC_H
#define CHAIOS_COMPILER_GNUC_H

#ifdef __GNUC__

#define READ_CR0()

#define WRITE_CR0(x) \
	__writecr0(x);

#define RETURN_ADDRESS() \
	__builtin_return_address(0)

#endif	//__GNUC__

#endif	//CHAIOS_COMPILER_GNUC_H
