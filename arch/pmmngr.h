/********************************************************** 
ChaiOS 0.05 Copyright (C) 2012-2014 Nathaniel Cleland
Licensed under GPLv2
See License for full details

Project: ChaiOS
File: pmmngr.cpp
Path: C:\Users\Nathaniel\Documents\Visual Studio 2013\Projects\ChaiOS\ChaiOS\arch\pmmngr.cpp
Created by Nathaniel on 8/11/2014 at 21:01

Description: Physical Memory Manager
**********************************************************/
#ifndef CHAIOS_PHYSICAL_MEMMNGR_H
#define CHAIOS_PHYSICAL_MEMMNGR_H

#ifdef __cplusplus
//C++ is the native ChaiOS interface (COM virtual calling style, C++ classes & Class Factory)
#include "stdafx.h"

EXPORT_CLASS(CPMemMngr)
{
protected:
	~CPMemMngr();
public:
	virtual void CALLING_CONVENTION destroy() = 0;
	virtual void CALLING_CONVENTION create() = 0;
private:
public:
	void operator delete(void* p)
	{
		CPMemMngr* mngr = static_cast<CPMemMngr*>(p);
		mngr->destroy();
	}

};

EXTERN EXPORT_FUNC(CPMemMngr* DefaultPMemMngrFactory());

#else
//C wrapper API

#endif	//__cplusplus

#endif	//CHAIOS_PHYSICAL_MEMMNGR_H
