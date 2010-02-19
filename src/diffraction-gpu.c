/*
 * diffraction-gpu.c
 *
 * Calculate diffraction patterns by Fourier methods (GPU version)
 *
 * (c) 2006-2010 Thomas White <taw@physics.org>
 *
 * Part of CrystFEL - crystallography with a FEL
 *
 */


#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <complex.h>
#include <CL/cl.h>

#include "image.h"
#include "utils.h"
#include "cell.h"
#include "diffraction.h"
#include "sfac.h"


#define SAMPLING (4)
#define BWSAMPLING (10)
#define BANDWIDTH (1.0 / 100.0)


struct gpu_context
{
	cl_context ctx;
	cl_command_queue cq;
	cl_program prog;
	cl_kernel kern;
	cl_mem sfacs;

	cl_mem tt;
	size_t tt_size;

	cl_mem diff;
	size_t diff_size;

};


static const char *clError(cl_int err)
{
	switch ( err ) {
	case CL_SUCCESS : return "no error";
	case CL_INVALID_PLATFORM : return "invalid platform";
	case CL_INVALID_KERNEL : return "invalid kernel";
	case CL_INVALID_ARG_INDEX : return "invalid argument index";
	case CL_INVALID_ARG_VALUE : return "invalid argument value";
	case CL_INVALID_MEM_OBJECT : return "invalid memory object";
	case CL_INVALID_SAMPLER : return "invalid sampler";
	case CL_INVALID_ARG_SIZE : return "invalid argument size";
	case CL_INVALID_COMMAND_QUEUE  : return "invalid command queue";
	case CL_INVALID_CONTEXT : return "invalid context";
	case CL_INVALID_VALUE : return "invalid value";
	case CL_INVALID_EVENT_WAIT_LIST : return "invalid wait list";
	case CL_MAP_FAILURE : return "map failure";
	case CL_MEM_OBJECT_ALLOCATION_FAILURE : return "object allocation failure";
	case CL_OUT_OF_HOST_MEMORY : return "out of host memory";
	case CL_OUT_OF_RESOURCES : return "out of resources";
	case CL_INVALID_KERNEL_NAME : return "invalid kernel name";
	case CL_INVALID_KERNEL_ARGS : return "invalid kernel arguments";
	default :
		ERROR("Error code: %i\n", err);
		return "unknown error";
	}
}


static cl_device_id get_first_dev(cl_context ctx)
{
	cl_device_id dev;
	cl_int r;

	r = clGetContextInfo(ctx, CL_CONTEXT_DEVICES, sizeof(dev), &dev, NULL);
	if ( r != CL_SUCCESS ) {
		ERROR("Couldn't get device\n");
		return 0;
	}

	return dev;
}


static void show_build_log(cl_program prog, cl_device_id dev)
{
	cl_int r;
	char log[4096];
	size_t s;

	r = clGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG, 4096, log,
	                          &s);

	STATUS("%s\n", log);
}


static cl_program load_program(const char *filename, cl_context ctx,
                               cl_device_id dev, cl_int *err)
{
	FILE *fh;
	cl_program prog;
	char *source;
	size_t len;
	cl_int r;

	fh = fopen(filename, "r");
	if ( fh == NULL ) {
		ERROR("Couldn't open '%s'\n", filename);
		*err = CL_INVALID_PROGRAM;
		return 0;
	}
	source = malloc(16384);
	len = fread(source, 1, 16383, fh);
	fclose(fh);
	source[len] = '\0';

	prog = clCreateProgramWithSource(ctx, 1, (const char **)&source,
	                                 NULL, err);
	if ( *err != CL_SUCCESS ) {
		ERROR("Couldn't load source\n");
		return 0;
	}

	r = clBuildProgram(prog, 0, NULL, "-Werror -I"DATADIR"/crystfel/",
	                   NULL, NULL);
	if ( r != CL_SUCCESS ) {
		ERROR("Couldn't build program '%s'\n", filename);
		show_build_log(prog, dev);
		*err = r;
		return 0;
	}

	free(source);
	*err = CL_SUCCESS;
	return prog;
}


void get_diffraction_gpu(struct gpu_context *gctx, struct image *image,
                         int na, int nb, int nc, int no_sfac)
{
	cl_int err;
	double ax, ay, az;
	double bx, by, bz;
	double cx, cy, cz;
	float k, klow;
	cl_event *event;
	int p;
	float *tt_ptr;
	int x, y;
	cl_float16 cell;
	float *diff_ptr;
	cl_float4 orientation;
	cl_int4 ncells;
	const int sampling = SAMPLING;
	cl_float bwstep;

	cell_get_cartesian(image->molecule->cell, &ax, &ay, &az,
		                                  &bx, &by, &bz,
		                                  &cx, &cy, &cz);
	cell[0] = ax;  cell[1] = ay;  cell[2] = az;
	cell[3] = bx;  cell[4] = by;  cell[5] = bz;
	cell[6] = cx;  cell[7] = cy;  cell[8] = cz;

	/* Calculate wavelength */
	k = 1.0/image->lambda;  /* Centre value */
	klow = k - k*(BANDWIDTH/2.0);  /* Lower value */
	bwstep = k * BANDWIDTH / BWSAMPLING;

	/* Orientation */
	orientation[0] = image->orientation.w;
	orientation[1] = image->orientation.x;
	orientation[2] = image->orientation.y;
	orientation[3] = image->orientation.z;

	ncells[0] = na;
	ncells[1] = nb;
	ncells[2] = nc;
	ncells[3] = 0;  /* unused */

	err = clSetKernelArg(gctx->kern, 0, sizeof(cl_mem), &gctx->diff);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't set arg 0: %s\n", clError(err));
		return;
	}
	clSetKernelArg(gctx->kern, 1, sizeof(cl_mem), &gctx->tt);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't set arg 1: %s\n", clError(err));
		return;
	}
	clSetKernelArg(gctx->kern, 2, sizeof(cl_float), &klow);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't set arg 2: %s\n", clError(err));
		return;
	}
	clSetKernelArg(gctx->kern, 3, sizeof(cl_int), &image->width);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't set arg 3: %s\n", clError(err));
		return;
	}
	clSetKernelArg(gctx->kern, 8, sizeof(cl_float16), &cell);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't set arg 8: %s\n", clError(err));
		return;
	}
	clSetKernelArg(gctx->kern, 9, sizeof(cl_mem), &gctx->sfacs);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't set arg 9: %s\n", clError(err));
		return;
	}
	clSetKernelArg(gctx->kern, 10, sizeof(cl_float4), &orientation);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't set arg 10: %s\n", clError(err));
		return;
	}
	clSetKernelArg(gctx->kern, 11, sizeof(cl_int4), &ncells);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't set arg 11: %s\n", clError(err));
		return;
	}
	clSetKernelArg(gctx->kern, 14, sizeof(cl_int), &sampling);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't set arg 14: %s\n", clError(err));
		return;
	}
	/* Local memory for reduction */
	clSetKernelArg(gctx->kern, 15,
	               BWSAMPLING*SAMPLING*SAMPLING*2*sizeof(cl_float), NULL);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't set arg 15: %s\n", clError(err));
		return;
	}
	/* Bandwidth sampling step */
	clSetKernelArg(gctx->kern, 16, sizeof(cl_float), &bwstep);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't set arg 16: %s\n", clError(err));
		return;
	}

	/* Iterate over panels */
	event = malloc(image->det.n_panels * sizeof(cl_event));
	for ( p=0; p<image->det.n_panels; p++ ) {

		size_t dims[3];
		size_t ldims[3] = {SAMPLING, SAMPLING, BWSAMPLING};

		/* In a future version of OpenCL, this could be done
		 * with a global work offset.  But not yet... */
		dims[0] = 1+image->det.panels[0].max_x-image->det.panels[0].min_x;
		dims[1] = 1+image->det.panels[0].max_y-image->det.panels[0].min_y;
		dims[0] *= SAMPLING;
		dims[1] *= SAMPLING;
		dims[2] = BWSAMPLING;

		clSetKernelArg(gctx->kern, 4, sizeof(cl_float),
		               &image->det.panels[p].cx);
		if ( err != CL_SUCCESS ) {
			ERROR("Couldn't set arg 4: %s\n", clError(err));
			return;
		}
		clSetKernelArg(gctx->kern, 5, sizeof(cl_float),
		               &image->det.panels[p].cy);
		if ( err != CL_SUCCESS ) {
			ERROR("Couldn't set arg 5: %s\n", clError(err));
			return;
		}
		clSetKernelArg(gctx->kern, 6, sizeof(cl_float),
		               &image->det.panels[p].res);
		if ( err != CL_SUCCESS ) {
			ERROR("Couldn't set arg 6: %s\n", clError(err));
			return;
		}
		clSetKernelArg(gctx->kern, 7, sizeof(cl_float),
		               &image->det.panels[p].clen);
		if ( err != CL_SUCCESS ) {
			ERROR("Couldn't set arg 7: %s\n", clError(err));
			return;
		}
		clSetKernelArg(gctx->kern, 12, sizeof(cl_int),
		               &image->det.panels[p].min_x);
		if ( err != CL_SUCCESS ) {
			ERROR("Couldn't set arg 12: %s\n", clError(err));
			return;
		}
		clSetKernelArg(gctx->kern, 13, sizeof(cl_int),
		               &image->det.panels[p].min_y);
		if ( err != CL_SUCCESS ) {
			ERROR("Couldn't set arg 13: %s\n", clError(err));
			return;
		}

		err = clEnqueueNDRangeKernel(gctx->cq, gctx->kern, 3, NULL,
		                             dims, ldims, 0, NULL, &event[p]);
		if ( err != CL_SUCCESS ) {
			ERROR("Couldn't enqueue diffraction kernel: %s\n",
			      clError(err));
			return;
		}
	}

	diff_ptr = clEnqueueMapBuffer(gctx->cq, gctx->diff, CL_TRUE,
	                              CL_MAP_READ, 0, gctx->diff_size,
	                              image->det.n_panels, event, NULL, &err);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't map diffraction buffer: %s\n", clError(err));
		return;
	}
	tt_ptr = clEnqueueMapBuffer(gctx->cq, gctx->tt, CL_TRUE, CL_MAP_READ, 0,
	                            gctx->tt_size, image->det.n_panels, event,
	                            NULL, &err);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't map tt buffer\n");
		return;
	}

	free(event);

	image->sfacs = calloc(image->width * image->height,
	                      sizeof(double complex));
	image->twotheta = calloc(image->width * image->height, sizeof(double));

	for ( x=0; x<image->width; x++ ) {
	for ( y=0; y<image->height; y++ ) {

		float re, im, tt;

		re = diff_ptr[2*(x + image->width*y)+0];
		im = diff_ptr[2*(x + image->width*y)+1];
		tt = tt_ptr[x + image->width*y];

		image->sfacs[x + image->width*y] = re + I*im;
		image->twotheta[x + image->width*y] = tt;

	}
	}

	clEnqueueUnmapMemObject(gctx->cq, gctx->diff, diff_ptr, 0, NULL, NULL);
	clEnqueueUnmapMemObject(gctx->cq, gctx->tt, tt_ptr, 0, NULL, NULL);
}


/* Setup the OpenCL stuff, create buffers, load the structure factor table */
struct gpu_context *setup_gpu(int no_sfac, struct image *image,
                              struct molecule *molecule)
{
	struct gpu_context *gctx;
	cl_uint nplat;
	cl_platform_id platforms[8];
	cl_context_properties prop[3];
	cl_int err;
	cl_device_id dev;
	size_t sfac_size;
	float *sfac_ptr;

	if ( molecule == NULL ) return NULL;

	/* Generate structure factors if required */
	if ( !no_sfac ) {
		if ( molecule->reflections == NULL ) {
			get_reflections_cached(molecule,
			                       ph_lambda_to_en(image->lambda));
		}
	}

	err = clGetPlatformIDs(8, platforms, &nplat);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't get platform IDs: %i\n", err);
		return NULL;
	}
	if ( nplat == 0 ) {
		ERROR("Couldn't find at least one platform!\n");
		return NULL;
	}
	prop[0] = CL_CONTEXT_PLATFORM;
	prop[1] = (cl_context_properties)platforms[0];
	prop[2] = 0;

	gctx = malloc(sizeof(*gctx));
	gctx->ctx = clCreateContextFromType(prop, CL_DEVICE_TYPE_GPU,
	                                    NULL, NULL, &err);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't create OpenCL context: %i\n", err);
		free(gctx);
		return NULL;
	}

	dev = get_first_dev(gctx->ctx);

	gctx->cq = clCreateCommandQueue(gctx->ctx, dev, 0, &err);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't create OpenCL command queue\n");
		free(gctx);
		return NULL;
	}

	/* Create buffer for the picture */
	gctx->diff_size = image->width*image->height*sizeof(cl_float)*2;
	gctx->diff = clCreateBuffer(gctx->ctx, CL_MEM_WRITE_ONLY,
	                            gctx->diff_size, NULL, &err);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't allocate diffraction memory\n");
		free(gctx);
		return NULL;
	}

	/* Create a single-precision version of the scattering factors */
	sfac_size = IDIM*IDIM*IDIM*sizeof(cl_float)*2; /* complex */
	sfac_ptr = malloc(sfac_size);
	if ( !no_sfac ) {
		int i;
		for ( i=0; i<IDIM*IDIM*IDIM; i++ ) {
			sfac_ptr[2*i+0] = creal(molecule->reflections[i]);
			sfac_ptr[2*i+1] = cimag(molecule->reflections[i]);
		}
	} else {
		int i;
		for ( i=0; i<IDIM*IDIM*IDIM; i++ ) {
			sfac_ptr[2*i+0] = 1000.0;
			sfac_ptr[2*i+1] = 0.0;
		}
	}
	gctx->sfacs = clCreateBuffer(gctx->ctx,
	                             CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
	                             sfac_size, sfac_ptr, &err);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't allocate sfac memory\n");
		free(gctx);
		return NULL;
	}
	free(sfac_ptr);

	gctx->tt_size = image->width*image->height*sizeof(cl_float);
	gctx->tt = clCreateBuffer(gctx->ctx, CL_MEM_WRITE_ONLY, gctx->tt_size,
	                          NULL, &err);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't allocate twotheta memory\n");
		free(gctx);
		return NULL;
	}

	gctx->prog = load_program(DATADIR"/crystfel/diffraction.cl", gctx->ctx,
	                          dev, &err);
	if ( err != CL_SUCCESS ) {
		free(gctx);
		return NULL;
	}

	gctx->kern = clCreateKernel(gctx->prog, "diffraction", &err);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't create kernel\n");
		free(gctx);
		return NULL;
	}

	return gctx;
}


void cleanup_gpu(struct gpu_context *gctx)
{
	clReleaseProgram(gctx->prog);
	clReleaseMemObject(gctx->diff);
	clReleaseMemObject(gctx->tt);
	clReleaseMemObject(gctx->sfacs);
	clReleaseCommandQueue(gctx->cq);
	clReleaseContext(gctx->ctx);
	free(gctx);
}
