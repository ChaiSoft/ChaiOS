/********************************************************** 
ChaiOS 0.05 Copyright (C) 2012-2014 Nathaniel Cleland
Licensed under GPLv2
See License for full details

Project: ChaiOS
File: msvc.h
Path: C:\Users\Nathaniel\Documents\Visual Studio 2013\Projects\ChaiOS\Include\msvc.h
Created by Nathaniel on 31/10/2014 at 10:50

Description: Microsoft Visual C++ compiler specific stuff
**********************************************************/
#ifndef CHAIOS_COMPILER_MSVC_H
#define CHAIOS_COMPILER_MSVC_H

#include <intrin.h>
#include <address.h>

#if defined X86 || defined X64

#define READ_CR0() \
	__readcr0()

#define WRITE_CR0(x) \
	__writecr0(x)

#define READ_CR2() \
	__readcr2()

#define READ_CR3() \
	__readcr3()

#define WRITE_CR3(x) \
	__writecr3(x)

#define READ_CR4() \
	__readcr4()

#define WRITE_CR4(x) \
	__writecr4(x)

#define READ_MSR(x) \
	__readmsr(x)

#define WRITE_MSR(x,v) \
	__writemsr(x, v)

#define READ_DR(x) \
	__readdr(x)

#define WRITE_DR(x,v) \
	__writedr(x,v)

#define INPORTB(x) \
	__inbyte(x)

#define INPORTBS(x,p,len) \
	__inbytestring(x,p,len)

#define OUTPORTB(x,v) \
	__outbyte(x,v)

#define OUTPORTBS(x,p,len) \
	__outbytestring(x, p, len)

#define INPORTW(x) \
	__inword(x)

#define INPORTWS(x,p,len) \
	__inwordstring(x, p, len)

#define OUTPORTW(x,v) \
	__outword(x, v)

#define OUTPORTWS(x,p,len) \
	__outwordstring(x, p, len)

#define INPORTD(x) \
	__indword(x)

#define INPORTDS(x,p,len) \
	__indwordstring(x, p, len)

#define OUTPORTD(x,v) \
	__outdword(x, v)

#define OUTPORTDS(x,p,len) \
	__outdwordstring(x, p, len)

#define DISABLE_INTERRUPTS() \
	_disable();

#define ENABLE_INTERRUPTS() \
	_enable();

#define CPU_HALT() \
	__halt();

#define INVLPG(x) \
	__invlpg(x)

#define COMPILER_MEMORY_BARRIER() \
	_ReadWriteBarrier()

#define LGDT(x)	\
	_lgdt(x)

#define LIDT(x)	\
	_lidt(x)

#ifdef X64

#define READ_EFER() \
	READ_MSR(0xC0000080)

#define WRITE_EFER(x) \
	WRITE_MSR(0xC0000080,x)

#define READ_CR8() \
	__readcr8()

#define WRITE_CR8(x) \
	__writecr8(x)

#endif	//X64

#endif	//X86||X64

#define ALIGN(a, x) \
	__declspec(align(a)) x

#define RETURN_ADDRESS() \
	_ReturnAddress()

#define NO_INLINE(x) \
	__declspec(noinline) x

#define EXPORT_FUNC(x) \
	__declspec(dllexport) x

#define EXPORT_CLASS(name) \
	class EXPORT_FUNC(name)

#define IMPORT_FUNC(x) \
	__declspec(dllimport) x

#define IMPORT_CLASS(name) \
	class IMPORT_FUNC(name)

#endif	//CHAIOS_COMPILER_MSVC_H
