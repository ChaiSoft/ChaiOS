/* _PDCLIB_open( const char * const, int )

   This file is part of the Public Domain C Library (PDCLib).
   Permission is granted to use, modify, and / or redistribute at will.
*/

/* This is an example implementation of _PDCLIB_open() fit for use with POSIX
   kernels.
*/

#include <stdio.h>

#ifndef REGTEST

#include "pdclib/_PDCLIB_glue.h"
#include "pdclib/_PDCLIB_defguard.h"

_PDCLIB_fd_t _PDCLIB_open( const char * const filename, unsigned int mode )
{
    return _PDCLIB_NOHANDLE;
}

#endif

#ifdef TEST

#include "_PDCLIB_test.h"

#include <stdlib.h>
#include <string.h>

int main( void )
{
#ifndef REGTEST
    /* This testdriver assumes POSIX, i.e. _PDCLIB_fd_t being int and being
       incremented by one on each successful open.
    */
    _PDCLIB_fd_t fh;
    char buffer[ 10 ];
    remove( testfile );
    /* Trying to read non-existent file. */
    TESTCASE( _PDCLIB_open( testfile, _PDCLIB_FREAD ) == _PDCLIB_NOHANDLE );
    /* Writing to file, trying to read from it. */
    TESTCASE( ( fh = _PDCLIB_open( testfile, _PDCLIB_FWRITE ) ) != _PDCLIB_NOHANDLE );
    puts( strerror( errno ) );
    TESTCASE( write( fh, "test", 4 ) == 4 );
    TESTCASE( lseek( fh, 0, SEEK_SET ) == 0 );
    TESTCASE( read( fh, buffer, 4 ) == -1 );
    TESTCASE( _PDCLIB_close( fh ) == 0 );
    /* Reading from file, trying to write to it. */
    TESTCASE( ( fh = _PDCLIB_open( testfile, _PDCLIB_FREAD ) ) != _PDCLIB_NOHANDLE );
    TESTCASE( write( fh, "test", 4 ) == -1 );
    TESTCASE( _PDCLIB_close( fh ) == 0 );
    /* Appending to file, trying to read from it. */
    TESTCASE( ( fh = _PDCLIB_open( testfile, _PDCLIB_FAPPEND ) ) != _PDCLIB_NOHANDLE );
    TESTCASE( write( fh, "app", 3 ) == 3 );
    TESTCASE( lseek( fh, 0, SEEK_SET ) == 0 );
    TESTCASE( read( fh, buffer, 10 ) == -1 );
    TESTCASE( write( fh, "end", 3 ) == 3 );
    TESTCASE( _PDCLIB_close( fh ) == 0 );
    /* Reading and writing from file ("r+"). */
    TESTCASE( ( fh = _PDCLIB_open( testfile, _PDCLIB_FREAD | _PDCLIB_FRW ) ) != _PDCLIB_NOHANDLE );
    TESTCASE( read( fh, buffer, 10 ) == 10 );
    TESTCASE( memcmp( buffer, "testappend", 10 ) == 0 );
    TESTCASE( lseek( fh, 0, SEEK_SET ) == 0 );
    TESTCASE( write( fh, "wedo", 4 ) == 4 );
    TESTCASE( lseek( fh, 0, SEEK_SET ) == 0 );
    TESTCASE( read( fh, buffer, 10 ) == 10 );
    TESTCASE( memcmp( buffer, "wedoappend", 10 ) == 0 );
    TESTCASE( _PDCLIB_close( fh ) == 0 );
    /* Writing and reading from file ("w+"). */
    TESTCASE( ( fh = _PDCLIB_open( testfile, _PDCLIB_FWRITE | _PDCLIB_FRW ) ) != _PDCLIB_NOHANDLE );
    TESTCASE( write( fh, "test", 4 ) == 4 );
    TESTCASE( lseek( fh, 1, SEEK_SET ) == 1 );
    TESTCASE( read( fh, buffer, 2 ) == 2 );
    TESTCASE( memcmp( buffer, "es", 2 ) == 0 );
    TESTCASE( write( fh, "sie", 3 ) == 3 );
    TESTCASE( lseek( fh, 0, SEEK_SET ) == 0 );
    TESTCASE( read( fh, buffer, 6 ) == 6 );
    TESTCASE( memcmp( buffer, "tessie", 6 ) == 0 );
    TESTCASE( _PDCLIB_close( fh ) == 0 );
    /* Appending and reading from file ("a+"). */
    TESTCASE( ( fh = _PDCLIB_open( testfile, _PDCLIB_FAPPEND | _PDCLIB_FRW ) ) != _PDCLIB_NOHANDLE );
    TESTCASE( write( fh, "baby", 4 ) == 4 );
    TESTCASE( lseek( fh, 0, SEEK_SET ) == 0 );
    TESTCASE( read( fh, buffer, 10 ) == 10 );
    TESTCASE( memcmp( buffer, "tessiebaby", 10 ) == 0 );
    TESTCASE( _PDCLIB_close( fh ) == 0 );
    /* Cleaning up. */
    TESTCASE( remove( testfile ) == 0 );
#endif
    return TEST_RESULTS;
}

#endif
