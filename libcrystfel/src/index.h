/*
 * index.h
 *
 * Perform indexing (somehow)
 *
 * Copyright © 2012-2013 Deutsches Elektronen-Synchrotron DESY,
 *                       a research centre of the Helmholtz Association.
 * Copyright © 2012 Richard Kirian
 * Copyright © 2012 Lorenzo Galli
 *
 * Authors:
 *   2010-2013 Thomas White <taw@physics.org>
 *   2010      Richard Kirian
 *   2012      Lorenzo Galli
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

#ifndef INDEX_H
#define INDEX_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include "cell.h"
#include "image.h"
#include "detector.h"


/**
 * IndexingMethod:
 * @INDEXING_NONE: No indexing to be performed
 * @INDEXING_DIRAX: Invoke DirAx
 * @INDEXING_MOSFLM: Invoke MOSFLM
 * @INDEXING_REAX: DPS algorithm using known cell parameters
 *
 * An enumeration of all the available indexing methods.
 **/
typedef enum {

	INDEXING_NONE   = 0,

	/* The core indexing methods themselves */
	INDEXING_DIRAX  = 1,
	INDEXING_MOSFLM = 2,
	INDEXING_REAX   = 3,

	/* Bits at the top of the IndexingMethod are flags which modify the
	 * behaviour of the indexer, at the moment just by adding checks. */
	INDEXING_CHECK_CELL_COMBINATIONS = 256,
	INDEXING_CHECK_CELL_AXES         = 512,
	INDEXING_CHECK_PEAKS             = 1024,

} IndexingMethod;

/* This defines the bits in "IndexingMethod" which are used to represent the
 * core of the indexing method */
#define INDEXING_METHOD_MASK (0xff)


/**
 * IndexingPrivate:
 *
 * This is an opaque data structure containing information needed by the
 * indexing method.
 **/
typedef void *IndexingPrivate;

extern IndexingMethod *build_indexer_list(const char *str);

extern IndexingPrivate **prepare_indexing(IndexingMethod *indm, UnitCell *cell,
                                          const char *filename,
                                          struct detector *det,
                                          struct beam_params *beam, float *ltl);

extern void index_pattern(struct image *image,
                          IndexingMethod *indms, IndexingPrivate **iprivs);

extern void cleanup_indexing(IndexingMethod *indms, IndexingPrivate **privs);

#endif	/* INDEX_H */
