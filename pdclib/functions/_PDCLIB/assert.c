/* _PDCLIB_assert( const char * )

   This file is part of the Public Domain C Library (PDCLib).
   Permission is granted to use, modify, and / or redistribute at will.
*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#ifndef REGTEST

void _PDCLIB_assert99( const char * const message1, const char * const function, const char * const message2 )
{
    fputs( message1, stderr );
    fputs( function, stderr );
    fputs( message2, stderr );
    abort();
}

void _PDCLIB_assert89( const char * const message )
{
    fputs( message, stderr );
    abort();
}

#endif

#ifdef TEST

#include "_PDCLIB_test.h"

#include <signal.h>

static int EXPECTED_ABORT = 0;
static int UNEXPECTED_ABORT = 1;

static void aborthandler( int sig )
{
    TESTCASE( ! EXPECTED_ABORT );
    exit( ( signed int )TEST_RESULTS );
}

#ifdef NDEBUG
#error Compiling test drivers with NDEBUG set is a questionable choice...
#endif

#define NDEBUG

#include <assert.h>

static int disabled_test( void )
{
    int i = 0;
    assert( i == 0 ); /* NDEBUG set, condition met */
    assert( i == 1 ); /* NDEBUG set, condition fails */
    return i;
}

#undef NDEBUG

#include <assert.h>

int main( void )
{
    TESTCASE( signal( SIGABRT, &aborthandler ) != SIG_ERR );
    TESTCASE( disabled_test() == 0 );
    assert( UNEXPECTED_ABORT ); /* NDEBUG not set, condition met */
    assert( EXPECTED_ABORT ); /* NDEBUG not set, condition fails - should abort */
    return TEST_RESULTS;
}

#endif
