/* perror( const char * )

   This file is part of the Public Domain C Library (PDCLib).
   Permission is granted to use, modify, and / or redistribute at will.
*/

#include <stdio.h>
#include <errno.h>
#include <locale.h>

#ifndef REGTEST

#ifndef __STDC_NO_THREADS__
#include <threads.h>
#endif

/* TODO: Doing this via a static array is not the way to do it. */
void perror( const char * s )
{
    _PDCLIB_LOCK( stderr->mtx );

    if ( ( s != NULL ) && ( s[0] != '\n' ) )
    {
        fprintf( stderr, "%s: ", s );
    }

    if ( errno >= _PDCLIB_ERRNO_MAX || errno < 0 )
    {
        fprintf( stderr, "%s\n", _PDCLIB_EUNKNOWN_TEXT );
    }
    else
    {
        fprintf( stderr, "%s\n", _PDCLIB_lc_messages->errno_texts[errno] );
    }

    _PDCLIB_UNLOCK( stderr->mtx );
    return;
}

#endif

#ifdef TEST

#include "_PDCLIB_test.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>

int main( void )
{
    FILE * fh;
    unsigned long long max = ULLONG_MAX;
    char buffer[100];
    sprintf( buffer, "%llu", max );
    TESTCASE( ( fh = freopen( testfile, "wb+", stderr ) ) != NULL );
    TESTCASE( strtol( buffer, NULL, 10 ) == LONG_MAX );
    perror( "Test" );
    rewind( fh );
    TESTCASE( fread( buffer, 1, 7, fh ) == 7 );
    TESTCASE( memcmp( buffer, "Test: ", 6 ) == 0 );
    TESTCASE( fclose( fh ) == 0 );
    TESTCASE( remove( testfile ) == 0 );
    return TEST_RESULTS;
}

#endif
