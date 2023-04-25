/* tmpnam( char * )

   This file is part of the Public Domain C Library (PDCLib).
   Permission is granted to use, modify, and / or redistribute at will.
*/

#include <stdio.h>

#ifndef REGTEST

#include "pdclib/_PDCLIB_glue.h"

#include <string.h>

char * tmpnam( char * s )
{
    static char filename[ L_tmpnam ];
    FILE * file = tmpfile();

    if ( s == NULL )
    {
        s = filename;
    }

    strcpy( s, file->filename );
    fclose( file );
    return s;
}

#endif

#ifdef TEST

#include "_PDCLIB_test.h"

#include <string.h>

int main( void )
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    TESTCASE( strlen( tmpnam( NULL ) ) < L_tmpnam );
#pragma GCC diagnostic pop
    return TEST_RESULTS;
}

#endif
