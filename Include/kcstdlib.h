#ifndef CHAIOS_KCSTDLIB_H
#define CHAIOS_KCSTDLIB_H

#ifdef __cplusplus
#define EXTERN extern "C"
#else
#define EXTERN
#endif

#ifndef DLLEXPORT
#define DLLEXPORT __declspec(dllexport)
#define DLLIMPORT __declspec(dllimport)
#endif

#ifndef CHAIOS_KCSTDLIB
#define KCSTDLIB_FUNC DLLIMPORT
#else
#define KCSTDLIB_FUNC DLLEXPORT
#endif

#endif
