#ifndef CHAIOS_CHAIKRNL_API_H
#define CHAIOS_CHAIKRNL_API_H

#include <compiler.h>

#ifdef CHAIKRNL_DLL
#define CHAIOS_API_FUNC(x)	\
	EXPORT_FUNC(x)
#define CHAIOS_API_CLASS(name)	\
	EXPORT_CLASS(name)
#else
#define CHAIOS_API_FUNC(x)	\
	IMPORT_FUNC(x)
#define CHAIOS_API_CLASS(name)	\
	IMPORT_CLASS(name)
#endif	//CHAIKRNL_DLL

#endif	//CHAIOS_CHAIKRNL_API_H
