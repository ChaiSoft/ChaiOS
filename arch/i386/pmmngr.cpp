/********************************************************** 
ChaiOS 0.05 Copyright (C) 2012-2014 Nathaniel Cleland
Licensed under GPLv2
See License for full details

Project: CHAIKRNL
File: pmmngr.cpp
Path: C:\Users\Nathaniel\Documents\Visual Studio 2013\Projects\ChaiOS\ChaiOS\arch\i386\pmmngr.cpp
Created by Nathaniel on 9/11/2014 at 15:44

Description: Physical Memory Management
**********************************************************/
#include "stdafx.h"
#include "pmmngr.h"

void CALLING_CONVENTION CPMemMngr::destroy()
{
	
}

EXTERN EXPORT_FUNC(CPMemMngr* DefaultPMemMngrFactory())
{
	return 0;
}
