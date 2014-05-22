/*
 * stream.h
 *
 * Stream tools
 *
 * Copyright © 2013-2014 Deutsches Elektronen-Synchrotron DESY,
 *                       a research centre of the Helmholtz Association.
 *
 * Authors:
 *   2010-2014 Thomas White <taw@physics.org>
 *   2011      Andrew Aquila
 *
 * This file is part of CrystFEL.
 *
 * CrystFEL is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CrystFEL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CrystFEL.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef STREAM_H
#define STREAM_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


struct image;
struct hdfile;

#define CHUNK_START_MARKER "----- Begin chunk -----"
#define CHUNK_END_MARKER "----- End chunk -----"
#define PEAK_LIST_START_MARKER "Peaks from peak search"
#define PEAK_LIST_END_MARKER "End of peak list"
#define CRYSTAL_START_MARKER "--- Begin crystal"
#define CRYSTAL_END_MARKER "--- End crystal"
#define REFLECTION_START_MARKER "Reflections measured after indexing"
/* REFLECTION_END_MARKER is over in reflist-utils.h because it is also
 * used to terminate a standalone list of reflections */

typedef struct _stream Stream;

/**
 * StreamReadFlags:
 * @STREAM_READ_UNITCELL: Read the unit cell
 * @STREAM_READ_REFLECTIONS: Read the integrated reflections
 * @STREAM_READ_PEAKS: Read the peak search results
 * @STREAM_READ_CRYSTALS: Read the general information about crystals
 *
 * A bitfield of things that can be read from a stream.  Use this (and
 * read_chunk_2()) to read the stream faster if you don't need the entire
 * contents of the stream.
 *
 * Using either or both of @STREAM_READ_REFLECTIONS and @STREAM_READ_UNITCELL
 * implies @STREAM_READ_CRYSTALS.
 **/
typedef enum {

	STREAM_READ_UNITCELL = 1,
	STREAM_READ_REFLECTIONS = 2,
	STREAM_READ_PEAKS = 4,
	STREAM_READ_CRYSTALS = 8,

} StreamReadFlags;


extern Stream *open_stream_for_read(const char *filename);
extern Stream *open_stream_for_write(const char *filename);
extern Stream *open_stream_fd_for_write(int fd);
extern int get_stream_fd(Stream *st);
extern void close_stream(Stream *st);

extern int read_chunk(Stream *st, struct image *image);
extern int read_chunk_2(Stream *st, struct image *image, StreamReadFlags srf);
extern void write_chunk(Stream *st, struct image *image, struct hdfile *hdfile,
                        int include_peaks, int include_reflections);

extern void write_command(Stream *st, int argc, char *argv[]);

extern int rewind_stream(Stream *st);
extern int is_stream(const char *filename);

#endif	/* STREAM_H */
