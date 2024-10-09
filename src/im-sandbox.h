/*
 * im-sandbox.h
 *
 * Sandbox for indexing
 *
 * Copyright © 2012-2021 Deutsches Elektronen-Synchrotron DESY,
 *                       a research centre of the Helmholtz Association.
 * Copyright © 2012 Richard Kirian
 * Copyright © 2012 Lorenzo Galli
 *
 * Authors:
 *   2010-2019 Thomas White <taw@physics.org>
 *   2011      Richard Kirian
 *   2012      Lorenzo Galli
 *   2012      Chunhong Yoon
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

#ifndef IM_SANDBOX_H
#define IM_SANDBOX_H

#include <semaphore.h>

struct sb_shm;

#include "index.h"
#include "stream.h"
#include "cell.h"
#include "process_image.h"
#include "im-zmq.h"
#include "im-asapo.h"
#include "im-argparse.h"

/* Length of event queue */
#define QUEUE_SIZE (256)

/* Maximum length of an event ID including serial number */
#define MAX_EV_LEN (1024)

/* Maximum length of a task ID, e.g. indexing:xgandalf.
 * NB If changing this, also update the value in index.c */
#define MAX_TASK_LEN (32)

/* Maximum number of workers */
#define MAX_NUM_WORKERS (1024)

struct sb_shm
{
	pthread_mutex_t term_lock;

	pthread_mutex_t queue_lock;
	int n_events;
	char queue[QUEUE_SIZE][MAX_EV_LEN];
	int no_more;
	char last_ev[MAX_NUM_WORKERS][MAX_EV_LEN];
	char last_task[MAX_NUM_WORKERS][MAX_TASK_LEN];
	int pings[MAX_NUM_WORKERS];
	int end_of_stream[MAX_NUM_WORKERS];
	time_t time_last_start[MAX_NUM_WORKERS];
	int warned_long_running[MAX_NUM_WORKERS];

	pthread_mutex_t totals_lock;
	int n_processed;
	int n_hits;
	int n_hadcrystals;
	int n_crystals;
	int should_shutdown;
};

extern char *create_tempdir(const char *temp_location);

extern void set_last_task(char *lt, const char *task);

extern int run_work(const struct indexamajig_arguments *args);

extern int create_sandbox(struct index_args *iargs, int n_proc, char *prefix,
                          int config_basename, FILE *fh,  Stream *stream,
                          const char *tempdir, int serial_start,
                          struct im_zmq_params *zmq_params,
                          struct im_asapo_params *asapo_params,
                          int timeout, int profile, int cpu_pin,
                          int no_data_timeout);

#endif /* IM_SANDBOX_H */
