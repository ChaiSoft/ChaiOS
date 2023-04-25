/* _PDCLIB_flushbuffer( struct _PDCLIB_file_t * )

   This file is part of the Public Domain C Library (PDCLib).
   Permission is granted to use, modify, and / or redistribute at will.
*/

/* This is an example implementation of _PDCLIB_flushbuffer() fit for
   use with POSIX kernels.
*/

#include <stdio.h>
#include <string.h>

#ifndef REGTEST

#include "pdclib/_PDCLIB_glue.h"

int _PDCLIB_flushbuffer( struct _PDCLIB_file_t * stream )
{
    return EOF;
}

#endif

#ifdef TEST

#include "_PDCLIB_test.h"

int main( void )
{
    /* Testing covered by ftell.c */
    return TEST_RESULTS;
}

#endif
