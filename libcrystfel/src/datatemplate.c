/*
 * datatemplate.c
 *
 * Data template structure
 *
 * Copyright © 2019-2020 Deutsches Elektronen-Synchrotron DESY,
 *                       a research centre of the Helmholtz Association.
 *
 * Authors:
 *   2019-2020 Thomas White <taw@physics.org>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

#include "utils.h"
#include "datatemplate.h"
#include "events.h"

#include "datatemplate_priv.h"


/**
 * \file datatemplate.h
 */

struct rg_definition {
	char *name;
	char *pns;
};


struct rgc_definition {
	char *name;
	char *rgs;
};


static struct panel_template *new_panel(DataTemplate *det, const char *name)
{
	struct panel_template *new;

	det->n_panels++;
	det->panels = realloc(det->panels,
	                      det->n_panels*sizeof(struct panel_template));

	new = &det->panels[det->n_panels-1];
	memcpy(new, &det->defaults, sizeof(struct panel_template));

	new->name = strdup(name);

	/* Copy strings */
	if ( new->cnz_from != NULL ) new->cnz_from = strdup(new->cnz_from);
	if ( new->data != NULL ) new->data = strdup(new->data);
	if ( new->mask != NULL ) new->mask = strdup(new->mask);

	/* Create a new copy of the dim_structure if needed */
	if ( new->dim_structure != NULL ) {

		struct dim_structure *dim_copy;
		int di;

		dim_copy = initialize_dim_structure();
		dim_copy->num_dims = new->dim_structure->num_dims;
		dim_copy->dims = malloc(dim_copy->num_dims*sizeof(int));
		for ( di=0; di<dim_copy->num_dims; di++ ) {
			dim_copy->dims[di] = new->dim_structure->dims[di];
		}

		new->dim_structure = dim_copy;
	}

	return new;
}


static struct dt_badregion *new_bad_region(DataTemplate *det, const char *name)
{
	struct dt_badregion *new;

	det->n_bad++;
	det->bad = realloc(det->bad, det->n_bad*sizeof(struct dt_badregion));

	new = &det->bad[det->n_bad-1];
	new->min_x = NAN;
	new->max_x = NAN;
	new->min_y = NAN;
	new->max_y = NAN;
	new->min_fs = 0;
	new->max_fs = 0;
	new->min_ss = 0;
	new->max_ss = 0;
	new->is_fsss = 99; /* Slightly nasty: means "unassigned" */
	new->panel = NULL;
	strcpy(new->name, name);

	return new;
}


static struct panel_template *find_panel_by_name(DataTemplate *det, const char *name)
{
	int i;

	for ( i=0; i<det->n_panels; i++ ) {
		if ( strcmp(det->panels[i].name, name) == 0 ) {
			return &det->panels[i];
		}
	}

	return NULL;
}


static struct dt_badregion *find_bad_region_by_name(DataTemplate *det,
                                                 const char *name)
{
	int i;

	for ( i=0; i<det->n_bad; i++ ) {
		if ( strcmp(det->bad[i].name, name) == 0 ) {
			return &det->bad[i];
		}
	}

	return NULL;
}


static struct dt_rigid_group *find_or_add_rg(DataTemplate *det,
                                          const char *name)
{
	int i;
	struct dt_rigid_group **new;
	struct dt_rigid_group *rg;

	for ( i=0; i<det->n_rigid_groups; i++ ) {

		if ( strcmp(det->rigid_groups[i]->name, name) == 0 ) {
			return det->rigid_groups[i];
		}

	}

	new = realloc(det->rigid_groups,
	              (1+det->n_rigid_groups)*sizeof(struct dt_rigid_group *));
	if ( new == NULL ) return NULL;

	det->rigid_groups = new;

	rg = malloc(sizeof(struct dt_rigid_group));
	if ( rg == NULL ) return NULL;

	det->rigid_groups[det->n_rigid_groups++] = rg;

	rg->name = strdup(name);
	rg->panels = NULL;
	rg->n_panels = 0;

	return rg;
}


static struct dt_rg_collection *find_or_add_rg_coll(DataTemplate *det,
                                                    const char *name)
{
	int i;
	struct dt_rg_collection **new;
	struct dt_rg_collection *rgc;

	for ( i=0; i<det->n_rg_collections; i++ ) {
		if ( strcmp(det->rigid_group_collections[i]->name, name) == 0 )
		{
			return det->rigid_group_collections[i];
		}
	}

	new = realloc(det->rigid_group_collections,
	              (1+det->n_rg_collections)*sizeof(struct dt_rg_collection *));
	if ( new == NULL ) return NULL;

	det->rigid_group_collections = new;

	rgc = malloc(sizeof(struct dt_rg_collection));
	if ( rgc == NULL ) return NULL;

	det->rigid_group_collections[det->n_rg_collections++] = rgc;

	rgc->name = strdup(name);
	rgc->rigid_groups = NULL;
	rgc->n_rigid_groups = 0;

	return rgc;
}


static void add_to_rigid_group(struct dt_rigid_group *rg, struct panel_template *p)
{
	struct panel_template **pn;

	pn = realloc(rg->panels, (1+rg->n_panels)*sizeof(struct panel_template *));
	if ( pn == NULL ) {
		ERROR("Couldn't add panel to rigid group.\n");
		return;
	}

	rg->panels = pn;
	rg->panels[rg->n_panels++] = p;
}


static void add_to_rigid_group_coll(struct dt_rg_collection *rgc,
                                    struct dt_rigid_group *rg)
{
	struct dt_rigid_group **r;

	r = realloc(rgc->rigid_groups, (1+rgc->n_rigid_groups)*
	            sizeof(struct dt_rigid_group *));
	if ( r == NULL ) {
		ERROR("Couldn't add rigid group to collection.\n");
		return;
	}

	rgc->rigid_groups = r;
	rgc->rigid_groups[rgc->n_rigid_groups++] = rg;
}


/* Free all rigid groups in detector */
static void free_all_rigid_groups(DataTemplate *det)
{
	int i;

	if ( det->rigid_groups == NULL ) return;
	for ( i=0; i<det->n_rigid_groups; i++ ) {
		free(det->rigid_groups[i]->name);
		free(det->rigid_groups[i]->panels);
		free(det->rigid_groups[i]);
	}
	free(det->rigid_groups);
}


/* Free all rigid groups in detector */
static void free_all_rigid_group_collections(DataTemplate *det)
{
	int i;

	if ( det->rigid_group_collections == NULL ) return;
	for ( i=0; i<det->n_rg_collections; i++ ) {
		free(det->rigid_group_collections[i]->name);
		free(det->rigid_group_collections[i]->rigid_groups);
		free(det->rigid_group_collections[i]);
	}
	free(det->rigid_group_collections);
}


static struct dt_rigid_group *find_rigid_group_by_name(DataTemplate *det,
                                                       char *name)
{
	int i;

	for ( i=0; i<det->n_rigid_groups; i++ ) {
		if ( strcmp(det->rigid_groups[i]->name, name) == 0 ) {
			return det->rigid_groups[i];
		}
	}

	return NULL;
}


static int atob(const char *a)
{
	if ( strcasecmp(a, "true") == 0 ) return 1;
	if ( strcasecmp(a, "false") == 0 ) return 0;
	return atoi(a);
}


static int assplode_algebraic(const char *a_orig, char ***pbits)
{
	int len, i;
	int nexp;
	char **bits;
	char *a;
	int idx, istr;

	len = strlen(a_orig);

	/* Add plus at start if no sign there already */
	if ( (a_orig[0] != '+') && (a_orig[0] != '-') ) {
		len += 1;
		a = malloc(len+1);
		snprintf(a, len+1, "+%s", a_orig);
		a[len] = '\0';

	} else {
		a = strdup(a_orig);
	}

	/* Count the expressions */
	nexp = 0;
	for ( i=0; i<len; i++ ) {
		if ( (a[i] == '+') || (a[i] == '-') ) nexp++;
	}

	bits = calloc(nexp, sizeof(char *));

	/* Break the string up */
	idx = -1;
	istr = 0;
	assert((a[0] == '+') || (a[0] == '-'));
	for ( i=0; i<len; i++ ) {

		char ch;

		ch = a[i];

		if ( ch == ' ' ) continue;

		if ( (ch == '+') || (ch == '-') ) {
			if ( idx >= 0 ) bits[idx][istr] = '\0';
			idx++;
			bits[idx] = malloc(len+1);
			istr = 0;
		}

		if ( !isdigit(ch) && (ch != '.') && (ch != '+') && (ch != '-')
		  && (ch != 'x') && (ch != 'y') && (ch != 'z') )
		{
			ERROR("Invalid character '%c' found.\n", ch);
			return 0;
		}

		assert(idx >= 0);
		bits[idx][istr++] = ch;

	}
	if ( idx >= 0 ) bits[idx][istr] = '\0';

	*pbits = bits;
	free(a);

	return nexp;
}


/* Parses the scan directions (accounting for possible rotation)
 * Assumes all white spaces have been already removed */
static int dir_conv(const char *a, double *sx, double *sy, double *sz)
{
	int n;
	char **bits;
	int i;

	*sx = 0.0;  *sy = 0.0;  *sz = 0.0;

	n = assplode_algebraic(a, &bits);

	if ( n == 0 ) {
		ERROR("Invalid direction '%s'\n", a);
		return 1;
	}

	for ( i=0; i<n; i++ ) {

		int len;
		double val;
		char axis;
		int j;

		len = strlen(bits[i]);
		assert(len != 0);
		axis = bits[i][len-1];
		if ( (axis != 'x') && (axis != 'y') && (axis != 'z') ) {
			ERROR("Invalid symbol '%c' - must be x, y or z.\n",
			      axis);
			return 1;
		}

		/* Chop off the symbol now it's dealt with */
		bits[i][len-1] = '\0';

		/* Check for anything that isn't part of a number */
		for ( j=0; j<strlen(bits[i]); j++ ) {
			if ( isdigit(bits[i][j]) ) continue;
			if ( bits[i][j] == '+' ) continue;
			if ( bits[i][j] == '-' ) continue;
			if ( bits[i][j] == '.' ) continue;
			ERROR("Invalid coefficient '%s'\n", bits[i]);
		}

		if ( strlen(bits[i]) == 0 ) {
			val = 1.0;
		} else {
			val = atof(bits[i]);
		}
		if ( strlen(bits[i]) == 1 ) {
			if ( bits[i][0] == '+' ) val = 1.0;
			if ( bits[i][0] == '-' ) val = -1.0;
		}
		switch ( axis ) {

			case 'x' :
			*sx += val;
			break;

			case 'y' :
			*sy += val;
			break;

			case 'z' :
			*sz += val;
			break;
		}

		free(bits[i]);

	}
	free(bits);

	return 0;
}


static int parse_field_for_panel(struct panel_template *panel, const char *key,
                                 const char *val, DataTemplate *det)
{
	int reject = 0;

	if ( strcmp(key, "min_fs") == 0 ) {
		panel->orig_min_fs = atof(val);
	} else if ( strcmp(key, "max_fs") == 0 ) {
		panel->orig_max_fs = atof(val);
	} else if ( strcmp(key, "min_ss") == 0 ) {
		panel->orig_min_ss = atof(val);
	} else if ( strcmp(key, "max_ss") == 0 ) {
		panel->orig_max_ss = atof(val);
	} else if ( strcmp(key, "corner_x") == 0 ) {
		panel->cnx = atof(val);
	} else if ( strcmp(key, "corner_y") == 0 ) {
		panel->cny = atof(val);
	} else if ( strcmp(key, "rail_direction") == 0 ) {
		if ( dir_conv(val, &panel->rail_x,
		                   &panel->rail_y,
		                   &panel->rail_z) )
		{
			ERROR("Invalid rail direction '%s'\n", val);
			reject = 1;
		}
	} else if ( strcmp(key, "clen_for_centering") == 0 ) {
		panel->clen_for_centering = atof(val);
	} else if ( strcmp(key, "adu_per_eV") == 0 ) {
		panel->adu_per_eV = atof(val);
	} else if ( strcmp(key, "adu_per_photon") == 0 ) {
		panel->adu_per_photon = atof(val);
	} else if ( strcmp(key, "rigid_group") == 0 ) {
		add_to_rigid_group(find_or_add_rg(det, val), panel);
	} else if ( strcmp(key, "clen") == 0 ) {
		/* Gets expanded when image is loaded */
		panel->cnz_from = strdup(val);

	} else if ( strcmp(key, "data") == 0 ) {
		if ( strncmp(val,"/",1) != 0 ) {
			ERROR("Invalid data location '%s'\n", val);
			reject = -1;
		}
		panel->data = strdup(val);

	} else if ( strcmp(key, "mask") == 0 ) {
		if ( strncmp(val,"/",1) != 0 ) {
			ERROR("Invalid mask location '%s'\n", val);
			reject = -1;
		}
		panel->mask = strdup(val);

	} else if ( strcmp(key, "mask_file") == 0 ) {
		panel->mask_file = strdup(val);

	} else if ( strcmp(key, "saturation_map") == 0 ) {
		panel->satmap = strdup(val);
	} else if ( strcmp(key, "saturation_map_file") == 0 ) {
		panel->satmap_file = strdup(val);

	} else if ( strcmp(key, "coffset") == 0) {
		panel->cnz_offset = atof(val);
	} else if ( strcmp(key, "res") == 0 ) {
		panel->pixel_pitch = 1.0/atof(val);
	} else if ( strcmp(key, "max_adu") == 0 ) {
		panel->max_adu = atof(val);
	} else if ( strcmp(key, "badrow_direction") == 0 ) {
		ERROR("WARNING 'badrow_direction' is ignored in this version.\n");
	} else if ( strcmp(key, "no_index") == 0 ) {
		panel->bad = atob(val);
	} else if ( strcmp(key, "fs") == 0 ) {
		if ( dir_conv(val, &panel->fsx, &panel->fsy, &panel->fsz) != 0 )
		{
			ERROR("Invalid fast scan direction '%s'\n", val);
			reject = 1;
		}
	} else if ( strcmp(key, "ss") == 0 ) {
		if ( dir_conv(val, &panel->ssx, &panel->ssy, &panel->ssz) != 0 )
		{
			ERROR("Invalid slow scan direction '%s'\n", val);
			reject = 1;
		}
	} else if ( strncmp(key, "dim", 3) == 0) {
		int dim_entry;
		char *endptr;
		if ( key[3] != '\0' ) {
			if  ( panel->dim_structure == NULL ) {
				panel->dim_structure = initialize_dim_structure();
			}
			dim_entry = strtoul(key+3, &endptr, 10);
			if ( endptr[0] != '\0' ) {
				ERROR("Invalid dimension number %s\n", key+3);
			} else {
				if ( set_dim_structure_entry(panel->dim_structure,
				                        dim_entry, val) )
				{
					ERROR("Failed to set dim structure entry\n");
				}
			}
		} else {
			ERROR("'dim' must be followed by a number, e.g. 'dim0'\n");
		}
	} else {
		ERROR("Unrecognised field '%s'\n", key);
	}

	return reject;
}


static int check_badr_fsss(struct dt_badregion *badr, int is_fsss)
{
	/* First assignment? */
	if ( badr->is_fsss == 99 ) {
		badr->is_fsss = is_fsss;
		return 0;
	}

	if ( is_fsss != badr->is_fsss ) {
		ERROR("You can't mix x/y and fs/ss in a bad region.\n");
		return 1;
	}

	return 0;
}


static int parse_field_bad(struct dt_badregion *badr, const char *key,
                           const char *val)
{
	int reject = 0;

	if ( strcmp(key, "min_x") == 0 ) {
		badr->min_x = atof(val);
		reject = check_badr_fsss(badr, 0);
	} else if ( strcmp(key, "max_x") == 0 ) {
		badr->max_x = atof(val);
		reject = check_badr_fsss(badr, 0);
	} else if ( strcmp(key, "min_y") == 0 ) {
		badr->min_y = atof(val);
		reject = check_badr_fsss(badr, 0);
	} else if ( strcmp(key, "max_y") == 0 ) {
		badr->max_y = atof(val);
		reject = check_badr_fsss(badr, 0);
	} else if ( strcmp(key, "min_fs") == 0 ) {
		badr->min_fs = atof(val);
		reject = check_badr_fsss(badr, 1);
	} else if ( strcmp(key, "max_fs") == 0 ) {
		badr->max_fs = atof(val);
		reject = check_badr_fsss(badr, 1);
	} else if ( strcmp(key, "min_ss") == 0 ) {
		badr->min_ss = atof(val);
		reject = check_badr_fsss(badr, 1);
	} else if ( strcmp(key, "max_ss") == 0 ) {
		badr->max_ss = atof(val);
		reject = check_badr_fsss(badr, 1);
	} else if ( strcmp(key, "panel") == 0 ) {
		badr->panel = strdup(val);
	} else {
		ERROR("Unrecognised field '%s'\n", key);
	}

	return reject;
}

static void parse_toplevel(DataTemplate *dt,
                           const char *key, const char *val,
                           struct rg_definition ***rg_defl,
                           struct rgc_definition ***rgc_defl, int *n_rg_defs,
                           int *n_rgc_defs)
{

	if ( strcmp(key, "mask_bad") == 0 ) {

		char *end;
		double v = strtod(val, &end);

		if ( end != val ) {
			dt->mask_bad = v;
		}

	} else if ( strcmp(key, "mask_good") == 0 ) {

		char *end;
		double v = strtod(val, &end);

		if ( end != val ) {
			dt->mask_good = v;
		}

	} else if ( strcmp(key, "coffset") == 0 ) {
		dt->defaults.cnz_offset = atof(val);

	} else if ( strcmp(key, "photon_energy") == 0 ) {
		/* Will be expanded when image is loaded */
		dt->wavelength_from = strdup(val);

	} else if ( strcmp(key, "peak_list") == 0 ) {
		dt->peak_list = strdup(val);

	} else if ( strcmp(key, "photon_energy_bandwidth") == 0 ) {
		double v;
		char *end;
		v = strtod(val, &end);
		if ( (val[0] != '\0') && (end[0] == '\0') ) {
			dt->photon_energy_bandwidth = v;
		} else {
			ERROR("Invalid value for photon_energy_bandwidth\n");
		}

	} else if ( strcmp(key, "photon_energy_scale") == 0 ) {
		dt->photon_energy_scale = atof(val);

	} else if (strncmp(key, "rigid_group", 11) == 0
	        && strncmp(key, "rigid_group_collection", 22) != 0 ) {

		struct rg_definition **new;

		new = realloc(*rg_defl,
		             ((*n_rg_defs)+1)*sizeof(struct rg_definition*));
		*rg_defl = new;

		(*rg_defl)[*n_rg_defs] = malloc(sizeof(struct rg_definition));
		(*rg_defl)[*n_rg_defs]->name = strdup(key+12);
		(*rg_defl)[*n_rg_defs]->pns = strdup(val);
		*n_rg_defs = *n_rg_defs+1;

	} else if ( strncmp(key, "rigid_group_collection", 22) == 0 ) {

		struct rgc_definition **new;

		new = realloc(*rgc_defl, ((*n_rgc_defs)+1)*
		      sizeof(struct rgc_definition*));
		*rgc_defl = new;

		(*rgc_defl)[*n_rgc_defs] =
		                   malloc(sizeof(struct rgc_definition));
		(*rgc_defl)[*n_rgc_defs]->name = strdup(key+23);
		(*rgc_defl)[*n_rgc_defs]->rgs = strdup(val);
		*n_rgc_defs = *n_rgc_defs+1;

	} else if ( parse_field_for_panel(&dt->defaults, key, val, dt) ) {
		ERROR("Unrecognised top level field '%s'\n", key);
	}
}


DataTemplate *data_template_new_from_string(const char *string_in)
{
	DataTemplate *dt;
	char **bits;
	int done = 0;
	int i;
	int rgi, rgci;
	int reject = 0;
	int path_dim, mask_path_dim;
	int dim_dim;
	int dim_reject = 0;
	int dim_dim_reject = 0;
	struct rg_definition **rg_defl = NULL;
	struct rgc_definition **rgc_defl = NULL;
	int n_rg_definitions = 0;
	int n_rgc_definitions = 0;
	char *string;
	char *string_orig;
	size_t len;

	dt = calloc(1, sizeof(DataTemplate));
	if ( dt == NULL ) return NULL;

	dt->n_panels = 0;
	dt->panels = NULL;
	dt->n_bad = 0;
	dt->bad = NULL;
	dt->mask_good = 0;
	dt->mask_bad = 0;
	dt->n_rigid_groups = 0;
	dt->rigid_groups = NULL;
	dt->path_dim = 0;
	dt->dim_dim = 0;
	dt->n_rg_collections = 0;
	dt->rigid_group_collections = NULL;
	dt->photon_energy_bandwidth = -1.0;
	dt->photon_energy_scale = -1.0;
	dt->peak_info_location = NULL;

	/* The default defaults... */
	dt->defaults.orig_min_fs = -1;
	dt->defaults.orig_min_ss = -1;
	dt->defaults.orig_max_fs = -1;
	dt->defaults.orig_max_ss = -1;
	dt->defaults.cnx = NAN;
	dt->defaults.cny = NAN;
	dt->defaults.cnz_from = NULL;
	dt->defaults.cnz_offset = 0.0;
	dt->defaults.pixel_pitch = -1.0;
	dt->defaults.bad = 0;
	dt->defaults.fsx = 1.0;
	dt->defaults.fsy = 0.0;
	dt->defaults.fsz = 0.0;
	dt->defaults.ssx = 0.0;
	dt->defaults.ssy = 1.0;
	dt->defaults.ssz = 0.0;
        dt->defaults.rail_x = NAN;  /* The actual default rail direction */
        dt->defaults.rail_y = NAN;  /*  is below */
        dt->defaults.rail_z = NAN;
        dt->defaults.clen_for_centering = NAN;
	dt->defaults.adu_scale = NAN;
	dt->defaults.adu_scale_unit = ADU_PER_PHOTON;
	dt->defaults.max_adu = +INFINITY;
	dt->defaults.mask = NULL;
	dt->defaults.mask_file = NULL;
	dt->defaults.satmap = NULL;
	dt->defaults.satmap_file = NULL;
	dt->defaults.data = NULL;
	dt->defaults.dim_structure = NULL;
	dt->defaults.name = NULL;

	string = strdup(string_in);
	if ( string == NULL ) return NULL;
	len = strlen(string);
	for ( i=0; i<len; i++ ) {
		if ( string_in[i] == '\r' ) string[i] = '\n';
	}

	/* Becaue 'string' will get modified */
	string_orig = string;

	do {

		char *line;
		struct dt_badregion *badregion = NULL;
		struct panel_template *panel = NULL;

		/* Copy the next line from the big string */
		const char *nl = strchr(string, '\n');
		if ( nl != NULL ) {
			size_t len = nl - string;
			line = strndup(string, nl-string);
			line[len] = '\0';
			string += len+1;
		} else {
			line = strdup(string);
			done = 1;
		}

		/* Trim leading spaces */
		i = 0;
		char *line_orig = line;
		while ( (line_orig[i] == ' ')
		     || (line_orig[i] == '\t') ) i++;
		line = strdup(line+i);
		free(line_orig);

		/* Stop at comment symbol */
		char *comm = strchr(line, ';');
		if ( comm != NULL ) comm[0] = '\0';

		/* Nothing left? Entire line was commented out,
		 * and can be silently ignored */
		if ( line[0] == '\0' ) {
			free(line);
			continue;
		}

		/* Find the equals sign */
		char *eq = strchr(line, '=');
		if ( eq == NULL ) {
			ERROR("Bad line in geometry file: '%s'\n", line);
			free(line);
			continue;
		}

		/* Split into two strings */
		eq[0] = '\0';
		char *val = eq+1;

		/* Trim leading and trailing spaces in value */
		while ( (val[0] == ' ') || (val[0] == '\t') ) val++;
	        notrail(val);

	        /* Trim trailing spaces in key
	         * (leading spaces already done above) */
	        notrail(line);

	        /* Find slash after panel name */
	        char *slash = strchr(line, '/');
	        if ( slash == NULL ) {

			/* Top-level option */
			parse_toplevel(dt, line, val,
			               &rg_defl,
			               &rgc_defl,
			               &n_rg_definitions,
				       &n_rgc_definitions);
			free(line);
			continue;
		}

	        slash[0] = '\0';
	        char *key = slash+1;
	        /* No space trimming this time - must be "panel/key" */

	        /* Find either panel or bad region */
		if ( strncmp(line, "bad", 3) == 0 ) {
			badregion = find_bad_region_by_name(dt, line);
			if ( badregion == NULL ) {
				badregion = new_bad_region(dt, line);
			}
		} else {
			panel = find_panel_by_name(dt, line);
			if ( panel == NULL ) {
				panel = new_panel(dt, line);
			}
		}

		if ( panel != NULL ) {
			if ( parse_field_for_panel(panel, key, val,
			                           dt) ) reject = 1;
		} else {
			if ( parse_field_bad(badregion, key,
			                     val) ) reject = 1;
		}

		free(line);

	} while ( !done );

	if ( dt->n_panels == -1 ) {
		ERROR("No panel descriptions in geometry file.\n");
		free(dt);
		return NULL;
	}

	path_dim = -1;
	dim_reject = 0;

	for ( i=0; i<dt->n_panels; i++ ) {

		int panel_dim = 0;
		char *next_instance;

		next_instance = dt->panels[i].data;

		while ( next_instance ) {
			next_instance = strstr(next_instance, "%");
			if ( next_instance != NULL ) {
				next_instance += 1*sizeof(char);
				panel_dim += 1;
			}
		}

		if ( path_dim == -1 ) {
			path_dim = panel_dim;
		} else {
			if ( panel_dim != path_dim ) {
				dim_reject = 1;
			}
		}

	}

	mask_path_dim = -1;
	for ( i=0; i<dt->n_panels; i++ ) {

		int panel_mask_dim = 0;
		char *next_instance;

		if ( dt->panels[i].mask != NULL ) {

			next_instance = dt->panels[i].mask;

			while ( next_instance ) {
				next_instance = strstr(next_instance, "%");
				if ( next_instance != NULL ) {
					next_instance += 1*sizeof(char);
					panel_mask_dim += 1;
				}
			}

			if ( mask_path_dim == -1 ) {
				mask_path_dim = panel_mask_dim;
			} else {
				if ( panel_mask_dim != mask_path_dim ) {
					dim_reject = 1;
				}
			}

		}
	}

	if ( dim_reject ==  1 ) {
		ERROR("All panels' data and mask entries must have the same "
		      "number of placeholders\n");
		reject = 1;
	}

	if ( mask_path_dim > path_dim ) {
		ERROR("Number of placeholders in mask cannot be larger than "
		      "for data\n");
		reject = 1;
	}

	dt->path_dim = path_dim;

	dim_dim_reject = 0;
	dim_dim = -1;

	for ( i=0; i<dt->n_panels; i++ ) {

		int di;
		int found_ss = 0;
		int found_fs = 0;
		int panel_dim_dim = 0;

		if ( dt->panels[i].dim_structure == NULL ) {
			dt->panels[i].dim_structure = default_dim_structure();
		}

		for ( di=0; di<dt->panels[i].dim_structure->num_dims; di++ ) {

			if ( dt->panels[i].dim_structure->dims[di] ==
			                                   HYSL_UNDEFINED  ) {
				dim_dim_reject = 1;
				ERROR("Dimension %i for panel %s is undefined.\n",
				      di, dt->panels[i].name);
			}
			if ( dt->panels[i].dim_structure->dims[di] ==
			                                   HYSL_PLACEHOLDER  ) {
				panel_dim_dim += 1;
			}
			if ( dt->panels[i].dim_structure->dims[di] ==
			                                   HYSL_SS  ) {
				found_ss += 1;
			}
			if ( dt->panels[i].dim_structure->dims[di] ==
			                                   HYSL_FS  ) {
				found_fs += 1;
			}

		}

		if ( found_ss != 1 ) {
			ERROR("Exactly one slow scan dim coordinate is needed "
			      "(found %i for panel %s)\n", found_ss,
			      dt->panels[i].name);
			dim_dim_reject = 1;
		}

		if ( found_fs != 1 ) {
			ERROR("Exactly one fast scan dim coordinate is needed "
			      "(found %i for panel %s)\n", found_fs,
			      dt->panels[i].name);
			dim_dim_reject = 1;
		}

		if ( panel_dim_dim > 1 ) {
			ERROR("Maximum one placeholder dim coordinate is "
			      "allowed (found %i for panel %s)\n",
			      panel_dim_dim, dt->panels[i].name);
			dim_dim_reject = 1;
		}

		if ( dim_dim == -1 ) {
			dim_dim = panel_dim_dim;
		} else {
			if ( panel_dim_dim != dim_dim ) {
				dim_dim_reject = 1;
			}
		}

	}

	if ( dim_dim_reject ==  1) {
		reject = 1;
	}

	dt->dim_dim = dim_dim;

	for ( i=0; i<dt->n_panels; i++ ) {

		struct panel_template *p = &dt->panels[i];

		if ( p->orig_min_fs < 0 ) {
			ERROR("Please specify the minimum FS coordinate for"
			      " panel %s\n", dt->panels[i].name);
			reject = 1;
		}
		if ( p->orig_max_fs < 0 ) {
			ERROR("Please specify the maximum FS coordinate for"
			      " panel %s\n", dt->panels[i].name);
			reject = 1;
		}
		if ( p->orig_min_ss < 0 ) {
			ERROR("Please specify the minimum SS coordinate for"
			      " panel %s\n", dt->panels[i].name);
			reject = 1;
		}
		if ( p->orig_max_ss < 0 ) {
			ERROR("Please specify the maximum SS coordinate for"
			      " panel %s\n", dt->panels[i].name);
			reject = 1;
		}
		if ( isnan(p->cnx) ) {
			ERROR("Please specify the corner X coordinate for"
			      " panel %s\n", dt->panels[i].name);
			reject = 1;
		}
		if ( isnan(p->cny) ) {
			ERROR("Please specify the corner Y coordinate for"
			      " panel %s\n", dt->panels[i].name);
			reject = 1;
		}
		if ( p->cnz_from == NULL ) {
			ERROR("Please specify the camera length for panel %s\n",
			      dt->panels[i].name);
			reject = 1;
		}
		if ( p->pixel_pitch < 0 ) {
			ERROR("Please specify the pixel size for"
			      " panel %s\n", dt->panels[i].name);
			reject = 1;
		}
		if ( p->data == NULL ) {
			ERROR("Please specify the data location for panel %s\n",
			      p->name);
			reject = 1;
		}
		if ( isnan(p->adu_per_eV) && isnan(p->adu_per_photon) ) {
			ERROR("Please specify either adu_per_eV or "
			      "adu_per_photon for panel %s\n",
			      dt->panels[i].name);
			reject = 1;
		}

		if ( isnan(p->clen_for_centering) && !isnan(p->rail_x) )
		{
			ERROR("You must specify clen_for_centering if you "
			      "specify the rail direction (panel %s)\n",
			      p->name);
			reject = 1;
		}

		if ( (p->mask_file != NULL) && (p->mask == NULL) ) {
			ERROR("You have specified 'mask_file' but not 'mask'.  "
			      "'mask_file' will therefore have no effect.  "
			      "(panel %s)\n", p->name);
			reject = 1;
		}

		/* The default rail direction */
		if ( isnan(p->rail_x) ) {
			p->rail_x = 0.0;
			p->rail_y = 0.0;
			p->rail_z = 1.0;
		}
		if ( isnan(p->clen_for_centering) ) p->clen_for_centering = 0.0;

	}

	for ( i=0; i<dt->n_bad; i++ ) {
		if ( dt->bad[i].is_fsss == 99 ) {
			ERROR("Please specify the coordinate ranges for"
			      " bad region %s\n", dt->bad[i].name);
			reject = 1;
		}
	}

	free(dt->defaults.cnz_from);
	free(dt->defaults.data);
	free(dt->defaults.mask);

	for ( rgi=0; rgi<n_rg_definitions; rgi++) {

		int pi, n1;
		struct dt_rigid_group *rigidgroup = NULL;

		rigidgroup = find_or_add_rg(dt, rg_defl[rgi]->name);

		n1 = assplode(rg_defl[rgi]->pns, ",", &bits, ASSPLODE_NONE);

		for ( pi=0; pi<n1; pi++ ) {

			struct panel_template *p;

			p = find_panel_by_name(dt, bits[pi]);
			if ( p == NULL ) {
				ERROR("Cannot add panel to rigid group\n");
				ERROR("Panel not found: %s\n", bits[pi]);
				return NULL;
			}
			add_to_rigid_group(rigidgroup, p);
			free(bits[pi]);
		}
		free(bits);
		free(rg_defl[rgi]->name);
		free(rg_defl[rgi]->pns);
		free(rg_defl[rgi]);
	}
	free(rg_defl);

	for ( rgci=0; rgci<n_rgc_definitions; rgci++ ) {

		int rgi, n2;
		struct dt_rg_collection *rgcollection = NULL;

		rgcollection = find_or_add_rg_coll(dt, rgc_defl[rgci]->name);

		n2 = assplode(rgc_defl[rgci]->rgs, ",", &bits, ASSPLODE_NONE);

		for ( rgi=0; rgi<n2; rgi++ ) {

			struct dt_rigid_group *r;

			r = find_rigid_group_by_name(dt, bits[rgi]);
			if ( r == NULL ) {
				ERROR("Cannot add rigid group to collection\n");
				ERROR("Rigid group not found: %s\n", bits[rgi]);
				return NULL;
			}
			add_to_rigid_group_coll(rgcollection, r);
			free(bits[rgi]);
		}
		free(bits);
		free(rgc_defl[rgci]->name);
		free(rgc_defl[rgci]->rgs);
		free(rgc_defl[rgci]);

	}
	free(rgc_defl);

	if ( n_rg_definitions == 0 ) {

		int pi;

		for ( pi=0; pi<dt->n_panels; pi++ ) {

			struct dt_rigid_group *rigidgroup = NULL;

			rigidgroup = find_or_add_rg(dt, dt->panels[pi].name);
			add_to_rigid_group(rigidgroup, &dt->panels[pi]);

		}
	}

	if ( n_rgc_definitions == 0 ) {

		int rgi;
		struct dt_rg_collection *rgcollection = NULL;

		rgcollection = find_or_add_rg_coll(dt, "default");

		for ( rgi=0; rgi<dt->n_rigid_groups; rgi++ ) {

			add_to_rigid_group_coll(rgcollection,
			                        dt->rigid_groups[rgi]);

		}
	}

	free(string_orig);

	if ( reject ) return NULL;

	return dt;
}


DataTemplate *data_template_new_from_file(const char *filename)
{
	char *contents;
	DataTemplate *dt;

	contents = load_entire_file(filename);
	if ( contents == NULL ) {
		ERROR("Failed to load geometry file '%s'\n", filename);
		return NULL;
	}

	dt = data_template_new_from_string(contents);
	free(contents);
	return dt;
}


void data_template_free(DataTemplate *dt)
{
	int i;

	free_all_rigid_groups(dt);
	free_all_rigid_group_collections(dt);

	for ( i=0; i<dt->n_panels; i++ ) {
		free(dt->panels[i].cnz_from);
		free_dim_structure(dt->panels[i].dim_structure);
	}

	free(dt->panels);
	free(dt->bad);
	free(dt);
}


static int data_template_find_panel(const DataTemplate *dt,
                                    int fs, int ss, int *ppn)
{
	int p;

	for ( p=0; p<dt->n_panels; p++ ) {
		if ( (fs >= dt->panels[p].orig_min_fs)
		  && (fs < dt->panels[p].orig_max_fs+1)
		  && (ss >= dt->panels[p].orig_min_ss)
		  && (ss < dt->panels[p].orig_max_ss+1) ) {
			*ppn = p;
			return 0;
		}
	}

	return 1;
}


int data_template_file_to_panel_coords(const DataTemplate *dt,
                                       float *pfs, float *pss,
                                       int *ppn)
{
	int pn;

	if ( data_template_find_panel(dt, *pfs, *pss, &pn) ) {
		return 1;
	}

	*ppn = pn;
	*pfs = *pfs - dt->panels[pn].orig_min_fs;
	*pss = *pss - dt->panels[pn].orig_min_ss;
	return 0;
}


int data_template_panel_to_file_coords(const DataTemplate *dt,
                                       int pn, float *pfs, float *pss)
{
	if ( pn >= dt->n_panels ) return 1;
	*pfs = *pfs + dt->panels[pn].orig_min_fs;
	*pss = *pss + dt->panels[pn].orig_min_ss;
	return 0;
}


const char *data_template_panel_name(const DataTemplate *dt, int pn)
{
	if ( pn >= dt->n_panels ) return NULL;
	return dt->panels[pn].name;
}


int data_template_panel_name_to_number(const DataTemplate *dt,
                                       const char *panel_name,
                                       int *pn)
{
	if ( panel_name == NULL ) return 1;

	for ( i=0; i<dt->n_panels; i++ ) {
		if ( strcmp(panel_name, dt->panels[i].name) == 0 ) {
			*pn = i;
			return 0;
		}
	}

	return 1;
}


void data_template_add_copy_header(DataTemplate *dt,
                                   const char *header)
{
	/* FIXME: Add "header" to list of things to copy */
	STATUS("Adding %s\n", header);
}


/* If possible, i.e. if there are no references to image headers in
 * 'dt', generate a detgeom structure from it.
 *
 * NB This is probably not the function you're looking for!
 * The detgeom structure should normally come from loading an image,
 * reading a stream or similar.  This function should only be used
 * when there is really no data involved, e.g. in make_pixelmap.
 */
struct detgeom *data_template_to_detgeom(const DataTemplate *dt)
{
	struct detgeom *detgeom;
	int i;

	if ( dt == NULL ) return NULL;

	detgeom = malloc(sizeof(struct detgeom));
	if ( detgeom == NULL ) return;

	detgeom->panels = malloc(dt->n_panels*sizeof(struct detgeom_panel));
	if ( detgeom->panels == NULL ) return;

	detgeom->n_panels = dt->n_panels;

	for ( i=0; i<dt->n_panels; i++ ) {

		detgeom->panels[i].name = safe_strdup(dt->panels[i].name);

		detgeom->panels[i].pixel_pitch = dt->panels[i].pixel_pitch;

		/* NB cnx,cny are in pixels, cnz is in m */
		detgeom->panels[i].cnx = dt->panels[i].cnx;
		detgeom->panels[i].cny = dt->panels[i].cny;
		detgeom->panels[i].cnz = get_length(image, dt->panels[i].cnz_from);

		/* Apply offset (in m) and then convert cnz from
		 * m to pixels */
		detgeom->panels[i].cnz += dt->panels[i].cnz_offset;
		detgeom->panels[i].cnz /= detgeom->panels[i].pixel_pitch;

		detgeom->panels[i].max_adu = dt->panels[i].max_adu;
		detgeom->panels[i].adu_per_photon = 1.0;  /* FIXME ! */

		detgeom->panels[i].w = dt->panels[i].orig_max_fs
		                        - dt->panels[i].orig_min_fs + 1;
		detgeom->panels[i].h = dt->panels[i].orig_max_ss
		                        - dt->panels[i].orig_min_ss + 1;

		detgeom->panels[i].fsx = dt->panels[i].fsx;
		detgeom->panels[i].fsy = dt->panels[i].fsy;
		detgeom->panels[i].fsz = dt->panels[i].fsz;
		detgeom->panels[i].ssx = dt->panels[i].ssx;
		detgeom->panels[i].ssy = dt->panels[i].ssy;
		detgeom->panels[i].ssz = dt->panels[i].ssz;

	}

	return detgeom;
}


int data_template_get_slab_extents(const DataTemplate *dt,
                                   int *pw, int *ph)
{
	int w, h;
	char *data_from;

	data_from = dt->panels[0].data;

	w = 0;  h = 0;
	for ( i=0; i<dt->n_panels; i++ ) {

		struct panel_template *p = &dt->panels[i];

		if ( strcmp(data_from, p->data) != 0 ) {
			/* Not slabby */
			return 1;
		}

		if ( p->dim_structure != NULL ) {
			int j;
			for ( j=0; j<p->dim_structure->ndims; j++ ) {
				if ( p->dim_structure->dims[j] == HYSL_PLACEHOLDER ) {
					/* Not slabby */
					return 1;
				}
			}
		}

		if ( p->file_max_fs > w ) {
			w = p->file_max_fs;
		}
		if ( p->file_max_ss > h ) {
			h = p->file_max_ss;
		}
	}

	/* Inclusive -> exclusive */
	*pw = w + 1;
	*ph = h + 1;
	return 0;
}


/* Return non-zero if pixel fs,ss on panel p is in a bad region
 * as specified in the geometry file (regions only, not including
 * masks, NaN/inf, no_index etc */
int data_template_in_bad_region(DataTemplate *dtempl,
                                int pn, double fs, double ss)
{
	double rx, ry;
	double xs, ys;
	int i;
	struct panel_template *p;

	if ( p >= dtempl->n_panels ) {
		ERROR("Panel index out of range\n");
		return 0;
	}
	p = dtempl->panels[pn];

	/* Convert xs and ys, which are in fast scan/slow scan coordinates,
	 * to x and y */
	xs = fs*p->fsx + ss*p->ssx;
	ys = fs*p->fsy + ss*p->ssy;

	rx = xs + p->cnx;
	ry = ys + p->cny;

	for ( i=0; i<dtempl->n_bad; i++ ) {

		struct dt_badregion *b = &dtempl->bad[i];

		if ( (b->panel != NULL)
		  && (strcmp(b->panel, p->name) != 0) ) continue;

		if ( b->is_fsss ) {

			int nfs, nss;

			/* fs/ss bad regions are specified according
			 * to the original coordinates */
			nfs = fs + p->orig_min_fs;
			nss = ss + p->orig_min_ss;

			if ( nfs < b->min_fs ) continue;
			if ( nfs > b->max_fs ) continue;
			if ( nss < b->min_ss ) continue;
			if ( nss > b->max_ss ) continue;

		} else {

			if ( rx < b->min_x ) continue;
			if ( rx > b->max_x ) continue;
			if ( ry < b->min_y ) continue;
			if ( ry > b->max_y ) continue;

		}

		return 1;
	}

	return 0;
}
