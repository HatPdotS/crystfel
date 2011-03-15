/*
 * stream.c
 *
 * Stream tools
 *
 * (c) 2006-2011 Thomas White <taw@physics.org>
 *
 * Part of CrystFEL - crystallography with a FEL
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cell.h"
#include "utils.h"
#include "image.h"
#include "stream.h"
#include "reflist.h"
#include "reflist-utils.h"


#define CHUNK_START_MARKER "----- Begin chunk -----"
#define CHUNK_END_MARKER "----- End chunk -----"
#define PEAK_LIST_START_MARKER "Peaks from peak search"
#define PEAK_LIST_END_MARKER "End of peak list"
#define REFLECTION_START_MARKER "Reflections measured after indexing"
#define REFLECTION_END_MARKER "End of reflections"

static void exclusive(const char *a, const char *b)
{
	ERROR("The stream options '%s' and '%s' are mutually exclusive.\n",
	      a, b);
}


int parse_stream_flags(const char *a)
{
	int n, i;
	int ret = STREAM_NONE;
	char **flags;

	n = assplode(a, ",", &flags, ASSPLODE_NONE);

	for ( i=0; i<n; i++ ) {

		if ( strcmp(flags[i], "pixels") == 0) {
			if ( ret & STREAM_INTEGRATED ) {
				exclusive("pixels", "integrated");
				return -1;
			}
			ret |= STREAM_PIXELS;

		} else if ( strcmp(flags[i], "integrated") == 0) {
			if ( ret & STREAM_PIXELS ) {
				exclusive("pixels", "integrated");
				return -1;
			}
			ret |= STREAM_INTEGRATED;

		} else if ( strcmp(flags[i], "peaks") == 0) {
			if ( ret & STREAM_PEAKS_IF_INDEXED ) {
				exclusive("peaks", "peaksifindexed");
				return -1;
			}
			ret |= STREAM_PEAKS;

		} else if ( strcmp(flags[i], "peaksifindexed") == 0) {
			if ( ret & STREAM_PEAKS ) {
				exclusive("peaks", "peaksifindexed");
				return -1;
			}
			ret |= STREAM_PEAKS_IF_INDEXED;

		} else {
			ERROR("Unrecognised stream flag '%s'\n", flags[i]);
			return 0;
		}

		free(flags[i]);

	}
	free(flags);

	return ret;

}


int count_patterns(FILE *fh)
{
	char *rval;

	int n_total_patterns = 0;
	do {
		char line[1024];

		rval = fgets(line, 1023, fh);
		if ( rval == NULL ) continue;
		chomp(line);
		if ( strcmp(line, CHUNK_END_MARKER) == 0 ) n_total_patterns++;

	} while ( rval != NULL );

	return n_total_patterns;
}


static UnitCell *read_orientation_matrix(FILE *fh)
{
	float u, v, w;
	struct rvec as, bs, cs;
	UnitCell *cell;
	char line[1024];

	if ( fgets(line, 1023, fh) == NULL ) return NULL;
	if ( sscanf(line, "astar = %f %f %f", &u, &v, &w) != 3 ) {
		ERROR("Couldn't read a-star\n");
		return NULL;
	}
	as.u = u*1e9;  as.v = v*1e9;  as.w = w*1e9;
	if ( fgets(line, 1023, fh) == NULL ) return NULL;
	if ( sscanf(line, "bstar = %f %f %f", &u, &v, &w) != 3 ) {
		ERROR("Couldn't read b-star\n");
		return NULL;
	}
	bs.u = u*1e9;  bs.v = v*1e9;  bs.w = w*1e9;
	if ( fgets(line, 1023, fh) == NULL ) return NULL;
	if ( sscanf(line, "cstar = %f %f %f", &u, &v, &w) != 3 ) {
		ERROR("Couldn't read c-star\n");
		return NULL;
	}
	cs.u = u*1e9;  cs.v = v*1e9;  cs.w = w*1e9;
	cell = cell_new_from_axes(as, bs, cs);

	return cell;
}


static int read_reflections(FILE *fh, struct image *image)
{
	char *rval = NULL;
	int first = 1;

	image->reflections = reflist_new();

	do {

		char line[1024];
		signed int h, k, l;
		float intensity, sigma, res, fs, ss;
		char phs[1024];
		int cts;
		int r;
		Reflection *refl;

		rval = fgets(line, 1023, fh);
		if ( rval == NULL ) continue;
		chomp(line);

		if ( strcmp(line, REFLECTION_END_MARKER) == 0 ) return 0;

		r = sscanf(line, "%i %i %i %f %s %f %f %i %f %f",
		           &h, &k, &l, &intensity, phs, &sigma, &res, &cts,
		           &fs, &ss);
		if ( (r != 10) && (!first) ) return 1;

		first = 0;
		if ( r == 10 ) {
			refl = add_refl(image->reflections, h, k, l);
			set_int(refl, intensity);
			set_detector_pos(refl, fs, ss, 0.0);
			set_esd_intensity(refl, sigma);
		}

	} while ( rval != NULL );

	/* Got read error of some kind before finding PEAK_LIST_END_MARKER */
	return 1;

}


static int read_peaks(FILE *fh, struct image *image)
{
	char *rval = NULL;
	int first = 1;

	image->features = image_feature_list_new();

	do {

		char line[1024];
		float x, y, d, intensity;
		int r;

		rval = fgets(line, 1023, fh);
		if ( rval == NULL ) continue;
		chomp(line);

		if ( strcmp(line, PEAK_LIST_END_MARKER) == 0 ) return 0;

		r = sscanf(line, "%f %f %f %f", &x, &y, &d, &intensity);
		if ( (r != 4) && (!first) ) {
			ERROR("Failed to parse peak list line.\n");
			ERROR("The failed line was: '%s'\n", line);
			return 1;
		}

		first = 0;
		if ( r == 4 ) {
			image_add_feature(image->features, x, y,
			                  image, 1.0, NULL);
		}

	} while ( rval != NULL );

	/* Got read error of some kind before finding PEAK_LIST_END_MARKER */
	return 1;
}


static void write_peaks(struct image *image, FILE *ofh)
{
	int i;

	fprintf(ofh, PEAK_LIST_START_MARKER"\n");
	fprintf(ofh, " fs/px  ss/px  (1/d)/nm^-1   Intensity\n");

	for ( i=0; i<image_feature_count(image->features); i++ ) {

		struct imagefeature *f;
		struct rvec r;
		double q;

		f = image_get_feature(image->features, i);
		if ( f == NULL ) continue;

		r = get_q(image, f->fs, f->ss, NULL, 1.0/image->lambda);
		q = modulus(r.u, r.v, r.w);

		fprintf(ofh, "%6.1f %6.1f   %10.2f  %10.2f\n",
		       f->fs, f->ss, q/1.0e9, f->intensity);

	}

	fprintf(ofh, PEAK_LIST_END_MARKER"\n");
}


void write_chunk(FILE *ofh, struct image *i, int f)
{
	double asx, asy, asz;
	double bsx, bsy, bsz;
	double csx, csy, csz;
	double a, b, c, al, be, ga;

	fprintf(ofh, CHUNK_START_MARKER"\n");

	fprintf(ofh, "Image filename: %s\n", i->filename);

	if ( i->indexed_cell != NULL ) {

		cell_get_parameters(i->indexed_cell, &a, &b, &c,
		                                         &al, &be, &ga);
		fprintf(ofh, "Cell parameters %7.5f %7.5f %7.5f nm,"
			     " %7.5f %7.5f %7.5f deg\n",
			     a*1.0e9, b*1.0e9, c*1.0e9,
			     rad2deg(al), rad2deg(be), rad2deg(ga));

		cell_get_reciprocal(i->indexed_cell, &asx, &asy, &asz,
			                  &bsx, &bsy, &bsz,
			                  &csx, &csy, &csz);
		fprintf(ofh, "astar = %+9.7f %+9.7f %+9.7f nm^-1\n",
			asx/1e9, asy/1e9, asz/1e9);
		fprintf(ofh, "bstar = %+9.7f %+9.7f %+9.7f nm^-1\n",
			bsx/1e9, bsy/1e9, bsz/1e9);
		fprintf(ofh, "cstar = %+9.7f %+9.7f %+9.7f nm^-1\n",
		       csx/1e9, csy/1e9, csz/1e9);

	} else {

		fprintf(ofh, "No unit cell from indexing.\n");

	}

	if ( i->i0_available ) {
		fprintf(ofh, "I0 = %7.5f (arbitrary units)\n", i->i0);
	} else {
		fprintf(ofh, "I0 = invalid\n");
	}

	fprintf(ofh, "photon_energy_eV = %f\n",
	        J_to_eV(ph_lambda_to_en(i->lambda)));

	if ( (f & STREAM_PEAKS)
	  || ((f & STREAM_PEAKS_IF_INDEXED) && (i->indexed_cell != NULL)) ) {
		fprintf(ofh, "\n");
		write_peaks(i, ofh);
	}

	if ( (f & STREAM_PIXELS) || (f & STREAM_INTEGRATED) ) {

		fprintf(ofh, "\n");
		fprintf(ofh, REFLECTION_START_MARKER"\n");
		write_reflections_to_file(ofh, i->reflections, i->indexed_cell);
		fprintf(ofh, REFLECTION_END_MARKER"\n");

	}

	fprintf(ofh, CHUNK_END_MARKER"\n\n");
}


static int find_start_of_chunk(FILE *fh)
{
	char *rval = NULL;
	char line[1024];

	do {

		rval = fgets(line, 1023, fh);

		/* Trouble? */
		if ( rval == NULL ) return 1;

		chomp(line);

	} while ( strcmp(line, CHUNK_START_MARKER) != 0 );

	return 0;
}


/* Read the next chunk from a stream and fill in 'image' */
int read_chunk(FILE *fh, struct image *image)
{
	char line[1024];
	char *rval = NULL;
	struct rvec as, bs, cs;
	int have_as = 0;
	int have_bs = 0;
	int have_cs = 0;
	int have_filename = 0;
	int have_cell = 0;
	int have_ev = 0;

	if ( find_start_of_chunk(fh) ) return 1;

	image->i0_available = 0;
	image->i0 = 1.0;
	image->lambda = -1.0;
	image->features = NULL;
	image->reflections = NULL;
	image->indexed_cell = NULL;

	do {

		float u, v, w;

		rval = fgets(line, 1023, fh);

		/* Trouble? */
		if ( rval == NULL ) return 1;

		chomp(line);

		if ( strncmp(line, "Image filename: ", 16) == 0 ) {
			image->filename = strdup(line+16);
			have_filename = 1;
		}

		if ( strncmp(line, "I0 = ", 5) == 0 ) {
			image->i0 = atof(line+5);
			image->i0_available = 1;
		}

		if ( sscanf(line, "astar = %f %f %f", &u, &v, &w) == 3 ) {
			as.u = u*1e9;  as.v = v*1e9;  as.w = w*1e9;
			have_as = 1;
		}

		if ( sscanf(line, "bstar = %f %f %f", &u, &v, &w) == 3 ) {
			bs.u = u*1e9;  bs.v = v*1e9;  bs.w = w*1e9;
			have_bs = 1;
		}

		if ( sscanf(line, "cstar = %f %f %f", &u, &v, &w) == 3 ) {
			cs.u = u*1e9;  cs.v = v*1e9;  cs.w = w*1e9;
			have_cs = 1;
		}

		if ( have_as && have_bs && have_cs ) {
			if ( image->indexed_cell != NULL ) {
				ERROR("Duplicate cell found in stream!\n");
				cell_free(image->indexed_cell);
			}
			image->indexed_cell = cell_new_from_axes(as, bs, cs);
			have_cell = 1;
			have_as = 0;  have_bs = 0;  have_cs = 0;
		}

		if ( strncmp(line, "photon_energy_eV = ", 19) == 0 ) {
			image->lambda = ph_en_to_lambda(eV_to_J(atof(line+19)));
			have_ev = 1;
		}

		if ( strcmp(line, PEAK_LIST_START_MARKER) == 0 ) {
			if ( read_peaks(fh, image) ) {
				ERROR("Failed while reading peaks\n");
				return 1;
			}
		}

		if ( strcmp(line, REFLECTION_START_MARKER) == 0 ) {
			if ( read_reflections(fh, image) ) {
				ERROR("Failed while reading reflections\n");
				return 1;
			}
		}

	} while ( strcmp(line, CHUNK_END_MARKER) != 0 );

	if ( have_filename && have_ev ) return 0;

	ERROR("Incomplete chunk found in input file.\n");
	return 1;
}


int find_chunk(FILE *fh, UnitCell **cell, char **filename, double *ev)
{
	char line[1024];
	char *rval = NULL;
	int have_ev = 0;
	int have_cell = 0;
	int have_filename = 0;
	long start_of_chunk = 0;

	do {

		const long start_of_line = ftell(fh);

		rval = fgets(line, 1023, fh);
		if ( rval == NULL ) continue;

		chomp(line);

		if ( strncmp(line, "Reflections from indexing", 25) == 0 ) {

			*filename = strdup(line+29);
			*cell = NULL;
			*ev = 0.0;
			have_cell = 0;
			have_ev = 0;
			have_filename = 1;
			start_of_chunk = ftell(fh);

		}

		if ( !have_filename ) continue;

		if ( strncmp(line, "astar = ", 8) == 0 ) {
			fseek(fh, start_of_line, 0);
			*cell = read_orientation_matrix(fh);
			have_cell = 1;
		}

		if ( strncmp(line, "photon_energy_eV = ", 19) == 0 ) {
			*ev = atof(line+19);
			have_ev = 1;
		}

		if ( strlen(line) == 0 ) {
			if ( have_filename && have_cell && have_ev ) {
				fseek(fh, start_of_chunk, 0);
				return 0;
			}
		}

	} while ( rval != NULL );

	return 1;
}


int skip_some_files(FILE *fh, int n)
{
	char *rval = NULL;
	int n_patterns = 0;

	do {

		char line[1024];

		if ( n_patterns == n ) return 0;

		rval = fgets(line, 1023, fh);
		if ( rval == NULL ) continue;
		if ( strcmp(line, CHUNK_END_MARKER) == 0 ) n_patterns++;

	} while ( rval != NULL );

	return 1;
}
