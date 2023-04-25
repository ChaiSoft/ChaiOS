/* Alignment <stdalign.h>

   This file is part of the Public Domain C Library (PDCLib).
   Permission is granted to use, modify, and / or redistribute at will.
*/

#ifndef _PDCLIB_STDALIGN_H
#define _PDCLIB_ALIGN_H _PDCLIB_ALIGN_H

#ifndef __cplusplus
#define alignas _Alignas
#define alignof _Alignof
#endif

#define __alignas_is_defined 1
#define __alignof_is_defined 1

/* Extension hook for downstream projects that want to have non-standard
   extensions to standard headers.
*/
#ifdef _PDCLIB_EXTEND_STDALIGN_H
#include _PDCLIB_EXTEND_STDALIGN_H
#endif

#endif

