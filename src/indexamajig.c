/*
 * indexamajig.c
 *
 * Find hits, index patterns, output hkl+intensity etc.
 *
 * (c) 2006-2009 Thomas White <taw@physics.org>
 *
 * Part of CrystFEL - crystallography with a FEL
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#include "utils.h"
#include "hdf5-file.h"
#include "dirax.h"
#include "intensities.h"
#include "ewald.h"


static void show_help(const char *s)
{
	printf("Syntax: %s [options]\n\n", s);
	printf(
"Process and index FEL diffraction images.\n"
"\n"
"  -h, --help              Display this help message.\n"
"\n"
"  -i, --input=<filename>  Specify file containing list of images to process.\n"
"                           '-' means stdin, which is the default.\n"
"      --no-index          Do everything else (including fine peak search),\n"
"                           but don't invoke the indexing program.\n"
"\n");
}


struct peak {
	int x;
	int y;
	int i;
	int invalid;
};


static int image_fom(struct image *image)
{
	int x, y;
	int integr, n;
	float fintegr, mean, sd, th;
	struct peak peaks[1024];
	int i, n_peaks, n_nearby, n_valid;

	/* Measure mean */
	integr = 0;
	n = 0;
	for ( x=0; x<1024; x++ ) {
	for ( y=600; y<1024; y++ ) {

		int val;
		if ( (x>400) && (x<600) ) continue;
		val = image->data[x+image->height*y];
		if ( val < 0 ) continue;
		integr += val;
		n++;

	}
	}
	mean = (float)integr / n;  /* As integer to keep maths fast */

	/* Standard deviation */
	fintegr = 0;
	for ( x=0; x<1024; x++ ) {
	for ( y=600; y<1024; y++ ) {

		float val;

		if ( (x>400) && (x<600) ) continue;
		val = (float)image->data[x+image->height*y];
		if ( val < 0 ) continue;

		val -= mean;
		val = powf(val, 2.0);
		fintegr += val;

	}
	}
	sd = sqrtf(fintegr / n);

	/* Threshold */
	th = mean + 5*sd;
	STATUS("mean=%f ,sd=%f, th=%f\n", mean, sd, th);

	/* Find pixels above threshold */
	n_peaks = 0;
	for ( x=0; x<1024; x++ ) {
	for ( y=600; y<1024; y++ ) {

		int val;

		/* Chop out streaky region */
		if ( (x>400) && (x<600) ) continue;

		val = image->data[x+image->height*y];

		if ( val > th ) {
			peaks[n_peaks].x = x;
			peaks[n_peaks].y = y;
			peaks[n_peaks].i = val;
			peaks[n_peaks].invalid = 0;
			n_peaks++;
		}

	}
	}

	do {

		int max, max_i = -1;
		int adjacent;

		n_nearby = 0;

		/* Find brightest peak */
		max = 0;
		for ( i=0; i<n_peaks; i++ ) {
			if ( peaks[i].i > max ) {
				max = peaks[i].i;
				max_i = i;
			}
		}

		/* Must be at least one adjacent bright pixel */
		adjacent = 0;
		for ( i=0; i<n_peaks; i++ ) {

			int td;

			td = abs(peaks[i].x - peaks[max_i].x) +
			     abs(peaks[i].y - peaks[max_i].y);
			if ( td == 1 ) {
				adjacent++;
				break;  /* One is enough */
			}
		}
		if ( adjacent < 1 ) {
			peaks[i].invalid = 1;
			continue;
		}

		/* Remove nearby (non-invalidated) peaks from list */
		n_nearby = 0;
		for ( i=0; i<n_peaks; i++ ) {

			int dx, dy, ds;

			if ( peaks[i].invalid ) continue;

			dx = abs(peaks[i].x - peaks[max_i].x);
			dy = abs(peaks[i].y - peaks[max_i].y);
			ds = dx*dx + dy*dy;
			if ( ds < 36 ) {
				n_nearby++;
				peaks[i].invalid = 1;
			}

		}

	} while ( n_nearby );

	n_valid = 0;
	for ( i=0; i<n_peaks; i++ ) {
		if ( peaks[i].invalid ) continue;
		//printf("%i %i\n", peaks[i].x, peaks[i].y);
		n_valid++;
	}

	return n_valid;
}


int main(int argc, char *argv[])
{
	int c;
	char *filename = NULL;
	FILE *fh;
	char *rval;
	int n_images;
	int n_hits;
	int config_noindex = 0;
	int config_dumpfound = 0;

	/* Long options */
	const struct option longopts[] = {
		{"help",               0, NULL,               'h'},
		{"input",              1, NULL,               'i'},
		{"no-index",           0, &config_noindex,     1},
		{"dump-found-peaks",   0, &config_dumpfound,   1},
		{0, 0, NULL, 0}
	};

	/* Short options */
	while ((c = getopt_long(argc, argv, "hi:w", longopts, NULL)) != -1) {

		switch (c) {
		case 'h' : {
			show_help(argv[0]);
			return 0;
		}

		case 'i' : {
			filename = strdup(optarg);
			break;
		}

		case 0 : {
			break;
		}

		default : {
			return 1;
		}
		}

	}

	if ( filename == NULL ) {
		filename = strdup("-");
	}
	if ( strcmp(filename, "-") == 0 ) {
		fh = stdin;
	} else {
		fh = fopen(filename, "r");
	}
	free(filename);
	if ( fh == NULL ) {
		ERROR("Failed to open input file\n");
		return 1;
	}

	n_images = 0;
	n_hits = 0;
	do {

		char line[1024];
		struct hdfile *hdfile;
		struct image image;
		int fom;

		rval = fgets(line, 1023, fh);
		if ( rval == NULL ) continue;
		chomp(line);

		image.features = NULL;
		image.molecule = NULL;

		STATUS("Processing '%s'\n", line);

		hdfile = hdfile_open(line);
		if ( hdfile == NULL ) {
			ERROR("Couldn't open file '%s'\n", filename);
		} else if ( hdfile_set_first_image(hdfile, "/") ) {
			ERROR("Couldn't select path\n");
		}

		hdf5_read(hdfile, &image);

		fom = image_fom(&image);
		if ( fom > 0 ) {

			/* Calculate orientation matrix (by magic) */
			index_pattern(&image, config_noindex, config_dumpfound);

			/* View head-on (unit cell is tilted) */
			image.orientation.x = 0.0;
			image.orientation.y = 0.0;
			image.orientation.z = 0.0;
			image.orientation.w = 1.0;
			get_ewald(&image);

			/* Read h,k,l,I */
			output_intensities(&image);

			n_hits++;

		}

		n_images++;

	} while ( rval != NULL );

	fclose(fh);

	STATUS("There were %i images.\n", n_images);
	STATUS("%i hits were found.\n", n_hits);

	return 0;
}
