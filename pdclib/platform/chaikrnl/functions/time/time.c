/* time( time_t * )

   This file is part of the Public Domain C Library (PDCLib).
   Permission is granted to use, modify, and / or redistribute at will.
*/

#include <time.h>

#ifndef REGTEST

#include "pdclib/_PDCLIB_defguard.h"

/* See comments in _PDCLIB_config.h on the semantics of time_t. */

time_t time( time_t * timer )
{
    return -1;
}

#endif

#ifdef TEST

#include "_PDCLIB_test.h"

int main( void )
{
    time_t t = time( NULL );
    printf( "%d\n", (int)t );
    TESTCASE( NO_TESTDRIVER );
    return TEST_RESULTS;
}

#endif
