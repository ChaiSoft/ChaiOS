#ifndef CHAIOS_CHAIKRNL_H
#define CHAIOS_CHAIKRNL_H

#ifndef EXTERN
#define EXTERN extern "C"
#endif

#ifndef DLLEXPORT
#define DLLEXPORT __declspec(dllexport)
#define DLLIMPORT __declspec(dllimport)
#endif

#ifndef CHAIOS_KERNEL
#define CHAIKRNL_FUNC DLLIMPORT
#else
#define CHAIKRNL_FUNC DLLEXPORT
#endif

#endif