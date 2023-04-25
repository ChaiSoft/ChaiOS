/* fprintf( FILE *, const char *, ... )

   This file is part of the Public Domain C Library (PDCLib).
   Permission is granted to use, modify, and / or redistribute at will.
*/

#include <stdarg.h>
#include <stdio.h>

#ifndef REGTEST

int fprintf( struct _PDCLIB_file_t * _PDCLIB_restrict stream, const char * _PDCLIB_restrict format, ... )
{
    int rc;
    va_list ap;
    va_start( ap, format );
    rc = vfprintf( stream, format, ap );
    va_end( ap );
    return rc;
}

#endif

#ifdef TEST

#include <stddef.h>
#include <stdint.h>
#include <float.h>

#define _PDCLIB_FILEID "stdio/fprintf.c"
#define _PDCLIB_FILEIO

#include "_PDCLIB_test.h"

#define testprintf( stream, ... ) fprintf( stream, __VA_ARGS__ )

int main( void )
{
    FILE * target;
    TESTCASE( ( target = tmpfile() ) != NULL );
#include "printf_testcases.h"
    TESTCASE( fclose( target ) == 0 );
    return TEST_RESULTS;
}

#endif
