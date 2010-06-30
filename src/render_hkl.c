/*
 * render_hkl.c
 *
 * Draw pretty renderings of reflection lists
 *
 * (c) 2006-2010 Thomas White <taw@physics.org>
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
#include <cairo.h>
#include <cairo-pdf.h>

#include "utils.h"
#include "reflections.h"
#include "povray.h"
#include "symmetry.h"


static void show_help(const char *s)
{
	printf("Syntax: %s [options] <file.hkl>\n\n", s);
	printf(
"Render intensity lists in various ways.\n"
"\n"
"  -h, --help       Display this help message.\n"
"      --povray     Render a 3D animation using POV-ray.\n"
"      --zone-axis  Render a 2D zone axis pattern.\n"
"  -j <n>           Run <n> instances of POV-ray in parallel.\n"
"  -p, --pdb=<file> PDB file from which to get the unit cell.\n"
);
}


static void render_za(UnitCell *cell, double *ref, unsigned int *c)
{
	cairo_surface_t *surface;
	cairo_t *dctx;
	double max_u, max_v, max_res, max_intensity, scale;
	double sep_u, sep_v, max_r;
	double as, bs, theta;
	double asx, asy, asz;
	double bsx, bsy, bsz;
	double csx, csy, csz;
	signed int h, k;
	float wh, ht;
	const char *sym = "6/mmm";

	wh = 1024;
	ht = 1024;

	surface = cairo_pdf_surface_create("za.pdf", wh, ht);

	if ( cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS ) {
		fprintf(stderr, "Couldn't create Cairo surface\n");
		cairo_surface_destroy(surface);
		return;
	}

	dctx = cairo_create(surface);

	/* Black background */
	cairo_rectangle(dctx, 0.0, 0.0, wh, ht);
	cairo_set_source_rgb(dctx, 0.0, 0.0, 0.0);
	cairo_fill(dctx);

	max_u = 0.0;  max_v = 0.0;  max_intensity = 0.0;
	max_res = 0.0;

	/* Work out reciprocal lattice spacings and angles for this cut */
	if ( cell_get_reciprocal(cell, &asx, &asy, &asz,
	                          &bsx, &bsy, &bsz,
	                          &csx, &csy, &csz) ) {
		ERROR("Couldn't get reciprocal parameters\n");
		return;
	}
	theta = angle_between(asx, asy, asz, bsx, bsy, bsz);
	as = modulus(asx, asy, asz) / 1e9;
	bs = modulus(bsx, bsy, bsz) / 1e9;
	STATUS("theta=%f\n", rad2deg(theta));

	for ( h=-INDMAX; h<INDMAX; h++ ) {
	for ( k=-INDMAX; k<INDMAX; k++ ) {

		double u, v, intensity, res;
		int ct;
		int nequiv, p;

		ct = lookup_count(c, h, k, 0);
		if ( ct < 1 ) continue;

		intensity = lookup_intensity(ref, h, k, 0) / (float)ct;

		if ( intensity != 0 ) {

			nequiv = num_equivs(h, k, 0, sym);
			for ( p=0; p<nequiv; p++ ) {

				signed int he, ke, le;
				get_equiv(h, k, 0, &he, &ke, &le, sym, p);

				u =  (double)he*as*sin(theta);
				v =  (double)he*as*cos(theta) + ke*bs;
				if ( fabs(u) > fabs(max_u) ) max_u = fabs(u);
				if ( fabs(v) > fabs(max_v) ) max_v = fabs(v);
				if ( fabs(intensity) > fabs(max_intensity) ) {
					max_intensity = fabs(intensity);
				}
				res = resolution(cell, he, ke, 0);
				if ( res > max_res ) max_res = res;



			}
		}

	}
	}

	max_res /= 1e9;
	max_u /= 0.5;
	max_v /= 0.5;
	printf("Maximum resolution is 1/d = %5.3f nm^-1, d = %5.3f nm\n",
	       max_res*2.0, 1.0/(max_res*2.0));

	if ( max_intensity <= 0.0 ) {
		max_r = 4.0;
		goto out;
	}

	/* Choose whichever scaling factor gives the smallest value */
	scale = ((double)wh-50.0) / (2*max_u);
	if ( ((double)ht-50.0) / (2*max_v) < scale ) {
		scale = ((double)ht-50.0) / (2*max_v);
	}

	sep_u = as * scale * cos(theta);
	sep_v = bs * scale;
	max_r = ((sep_u < sep_v)?sep_u:sep_v);

	for ( h=-INDMAX; h<INDMAX; h++ ) {
	for ( k=-INDMAX; k<INDMAX; k++ ) {

		double u, v, intensity, val;
		int ct;
		int nequiv, p;

		ct = lookup_count(c, h, k, 0);
		if ( ct < 1 ) continue;

		intensity = lookup_intensity(ref, h, k, 0) / (float)ct;
		val = 3.0*intensity/max_intensity;

		nequiv = num_equivs(h, k, 0, sym);
		for ( p=0; p<nequiv; p++ ) {

			signed int he, ke, le;
			get_equiv(h, k, 0, &he, &ke, &le, sym, p);

			u = (double)he*as*sin(theta);
			v = (double)he*as*cos(theta) + ke*bs;

			cairo_arc(dctx, ((double)wh/2)+u*scale*2,
					((double)ht/2)+v*scale*2, max_r,
					0, 2*M_PI);

			cairo_set_source_rgb(dctx, val, val, val);
			cairo_fill(dctx);

		}

	}
	}

out:
	/* Centre marker */
	cairo_arc(dctx, (double)wh/2,
			(double)ht/2, max_r, 0, 2*M_PI);
	cairo_set_source_rgb(dctx, 1.0, 0.0, 0.0);
	cairo_fill(dctx);

	cairo_surface_finish(surface);
	cairo_destroy(dctx);
}


int main(int argc, char *argv[])
{
	int c;
	UnitCell *cell;
	char *infile;
	double *ref;
	unsigned int *cts;
	int config_povray = 0;
	int config_zoneaxis = 0;
	unsigned int nproc = 1;
	char *pdb = NULL;
	int r = 0;

	/* Long options */
	const struct option longopts[] = {
		{"help",               0, NULL,               'h'},
		{"povray",             0, &config_povray,      1},
		{"zone-axis",          0, &config_zoneaxis,    1},
		{"pdb",                1, NULL,               'p'},
		{0, 0, NULL, 0}
	};

	/* Short options */
	while ((c = getopt_long(argc, argv, "hj:p:", longopts, NULL)) != -1) {

		switch (c) {
		case 'h' :
			show_help(argv[0]);
			return 0;

		case 'j' :
			nproc = atoi(optarg);
			break;

		case 'p' :
			pdb = strdup(optarg);
			break;

		case 0 :
			break;

		default :
			return 1;
		}

	}

	if ( pdb == NULL ) {
		pdb = strdup("molecule.pdb");
	}

	infile = argv[optind];

	cell = load_cell_from_pdb(pdb);
	if ( cell == NULL ) {
		ERROR("Couldn't load unit cell from %s\n", pdb);
		return 1;
	}
	cts = new_list_count();
	ref = read_reflections(infile, cts, NULL);
	if ( ref == NULL ) {
		ERROR("Couldn't open file '%s'\n", infile);
		return 1;
	}

	if ( config_povray ) {
		r = povray_render_animation(cell, ref, cts, nproc);
	} else if ( config_zoneaxis ) {
		render_za(cell, ref, cts);
	} else {
		ERROR("Try again with either --povray or --zone-axis.\n");
	}

	free(pdb);

	return r;
}
