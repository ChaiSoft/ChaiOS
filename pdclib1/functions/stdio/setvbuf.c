/* setvbuf( FILE *, char *, int, size_t )

   This file is part of the Public Domain C Library (PDCLib).
   Permission is granted to use, modify, and / or redistribute at will.
*/

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#ifndef REGTEST

#ifndef __STDC_NO_THREADS__
#include <threads.h>
#endif

int setvbuf( struct _PDCLIB_file_t * _PDCLIB_restrict stream, char * _PDCLIB_restrict buf, int mode, size_t size )
{
    switch ( mode )
    {
        case _IONBF:
            /* When unbuffered I/O is requested, we keep the buffer anyway, as
               we don't want to e.g. flush the stream for every character of a
               stream being printed.
            */
            _PDCLIB_LOCK( stream->mtx );
            break;

        case _IOFBF:
        case _IOLBF:
            if ( size > INT_MAX || size == 0 )
            {
                /* PDCLib only supports buffers up to INT_MAX in size. A size
                   of zero doesn't make sense.
                */
                return -1;
            }

            if ( buf != NULL )
            {
                /* User provided buffer. Deallocate existing buffer, and mark
                   the stream so that fclose() does not try to deallocate the
                   user's buffer.
                */
                if ( stream->status & _PDCLIB_FREEBUFFER )
                {
                    free( stream->buffer );
                }

                stream->status &= ~_PDCLIB_FREEBUFFER;
            }
            else
            {
                /* User requested buffer size, but leaves it to library to
                   allocate the buffer.
                */
                /* If current buffer is big enough for requested size, but not
                   over twice as big (and wasting memory space), we use the
                   current buffer (i.e., do nothing), to save the malloc() /
                   free() overhead.
                */
                _PDCLIB_LOCK( stream->mtx );

                if ( ( stream->bufsize < size ) || ( stream->bufsize > ( size << 1 ) ) )
                {
                    /* Buffer too small, or much too large - allocate. */
                    if ( ( buf = ( char * ) malloc( size ) ) == NULL )
                    {
                        /* Out of memory error. */
                        _PDCLIB_UNLOCK( stream->mtx );
                        return -1;
                    }

                    if ( stream->status & _PDCLIB_FREEBUFFER )
                    {
                        free( stream->buffer );
                    }

                    /* This buffer must be free()d on fclose() */
                    stream->status |= _PDCLIB_FREEBUFFER;
                }
            }

            stream->buffer = buf;
            stream->bufsize = size;
            break;

        default:
            /* If mode is something else than _IOFBF, _IOLBF or _IONBF -> exit */
            return -1;
    }

    /* Deleting current buffer mode */
    stream->status &= ~( _IOFBF | _IOLBF | _IONBF );
    /* Set user-defined mode */
    stream->status |= mode;
    _PDCLIB_UNLOCK( stream->mtx );
    return 0;
}

#endif

#ifdef TEST

#include "_PDCLIB_test.h"

#include <errno.h>

#define BUFFERSIZE 500

int main( void )
{
#ifndef REGTEST
    char buffer[ BUFFERSIZE ];
    FILE * fh;
    /* full buffered, user-supplied buffer */
    TESTCASE( ( fh = tmpfile() ) != NULL );
    TESTCASE( setvbuf( fh, buffer, _IOFBF, BUFFERSIZE ) == 0 );
    TESTCASE( fh->buffer == buffer );
    TESTCASE( fh->bufsize == BUFFERSIZE );
    TESTCASE( ( fh->status & ( _IOFBF | _IONBF | _IOLBF ) ) == _IOFBF );
    TESTCASE( fclose( fh ) == 0 );
    /* line buffered, lib-supplied buffer */
    TESTCASE( ( fh = tmpfile() ) != NULL );
    TESTCASE( setvbuf( fh, NULL, _IOLBF, BUFFERSIZE ) == 0 );
    TESTCASE( fh->buffer != NULL );
    TESTCASE( fh->bufsize == BUFFERSIZE );
    TESTCASE( ( fh->status & ( _IOFBF | _IONBF | _IOLBF ) ) == _IOLBF );
    TESTCASE( fclose( fh ) == 0 );
    /* not buffered, user-supplied buffer */
    TESTCASE( ( fh = tmpfile() ) != NULL );
    TESTCASE( setvbuf( fh, buffer, _IONBF, BUFFERSIZE ) == 0 );
    TESTCASE( ( fh->status & ( _IOFBF | _IONBF | _IOLBF ) ) == _IONBF );
    TESTCASE( fclose( fh ) == 0 );
#else
    puts( " NOTEST setvbuf() test driver is PDCLib-specific." );
#endif
    return TEST_RESULTS;
}

#endif
