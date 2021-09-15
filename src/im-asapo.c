/*
 * asapo.c
 *
 * ASAP::O data interface
 *
 * Copyright © 2021 Deutsches Elektronen-Synchrotron DESY,
 *                  a research centre of the Helmholtz Association.
 *
 * Authors:
 *   2021 Thomas White <taw@physics.org>
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
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>
#include <asapo/consumer_c.h>

#include <image.h>
#include <utils.h>

#include "im-asapo.h"

#include "datatemplate_priv.h"


struct im_asapo
{
	char *stream;
	AsapoConsumerHandle consumer;
	AsapoStringHandle group_id;
};


static void show_asapo_error(const char *msg, const AsapoErrorHandle err)
{
	char buf[1024];
	asapo_error_explain(err, buf, sizeof(buf));
	ERROR("%s: %s\n", msg, buf);
}


char *im_asapo_make_unique_group_id(const char *endpoint,
                                    const char *token)
{
	AsapoConsumerHandle consumer;
	AsapoSourceCredentialsHandle cred;
	AsapoStringHandle group_id;
	AsapoErrorHandle err = asapo_new_handle();

	cred = asapo_create_source_credentials(kProcessed, "", "", "", token);
	consumer = asapo_create_consumer(endpoint, "", 0, cred, &err);
	asapo_free_handle(&cred);
	if ( asapo_is_error(err) ) {
		show_asapo_error("Cannot create temporary ASAP::O consumer", err);
		asapo_free_handle(&consumer);
		return NULL;
	}

	group_id = asapo_consumer_generate_new_group_id(consumer, &err);
	asapo_free_handle(&consumer);
	if ( asapo_is_error(err) ) {
		show_asapo_error("Cannot create ASAP::O group ID", err);
		return NULL;
	}

	return strdup(asapo_string_c_str(group_id));
}


struct im_asapo *im_asapo_connect(const char *endpoint,
                                  const char *token,
                                  const char *beamtime,
                                  const char *group_id,
                                  const char *data_source)
{
	struct im_asapo *a;
	AsapoSourceCredentialsHandle cred;
	AsapoErrorHandle err = asapo_new_handle();

	a = malloc(sizeof(struct im_asapo));
	if ( a == NULL ) return NULL;

	cred = asapo_create_source_credentials(kProcessed, beamtime, "",
	                                       data_source, token);
	a->consumer = asapo_create_consumer(endpoint, "", 0, cred, &err);
	asapo_free_handle(&cred);
	if ( asapo_is_error(err) ) {
		show_asapo_error("Cannot create ASAP::O consumer", err);
		free(a);
		return NULL;
	}

	asapo_consumer_set_timeout(a->consumer, 1000);

	a->group_id = asapo_string_from_c_str(group_id);
	a->stream = NULL;

	return a;
}


static int select_last_stream(struct im_asapo *a)
{
	AsapoStreamInfosHandle si;
	size_t len;
	int i;
	AsapoStreamInfoHandle st;
	AsapoErrorHandle err = asapo_new_handle();

	si = asapo_consumer_get_stream_list(a->consumer, NULL,
	                                    kAllStreams, &err);

	if ( asapo_is_error(err) ) {
		show_asapo_error("Couldn't get ASAP::O stream list", err);
		asapo_free_handle(err);
		return 1;
	}

	STATUS("for info: stream list:\n");
	n = asapo_stream_infos_get_size(si);
	for ( i=0; i<n; i++ ) {
		AsapoStreamInfoHandle st;
		st = asapo_stream_infos_get_item(si, i);
		STATUS("Stream %i: %s\n", i, asapo_stream_info_get_name(st));
		asapo_free_handle(st);
	}
	STATUS("end of stream list\n");

	st = asapo_stream_infos_get_item(si, n-1);
	a->stream = strdup(asapo_stream_info_get_name(st));
	asapo_free_handle(st);

	asapo_free_handle(si);
	asapo_free_handle(err);
	return 0;
}


static int select_next_stream(struct im_asapo *a)
{
	AsapoStreamInfosHandle si;
	size_t len;
	int i;
	AsapoErrorHandle err = asapo_new_handle();

	si = asapo_consumer_get_stream_list(a->consumer, NULL,
	                                    kAllStreams, &err);

	if ( asapo_is_error(err) ) {
		show_asapo_error("Couldn't get ASAP::O stream list", err);
		asapo_free_handle(err);
		return 1;
	}

	n = asapo_stream_infos_get_size(si);
	for ( i=n-1; i>0; i-- ) {

		AsapoStreamInfoHandle st;
		const char *name;

		st = asapo_stream_infos_get_item(si, i);
		name = asapo_stream_info_get_name(st);

		if ( strcmp(name, a->stream) == 0 ) {
			free(a->stream);
			a->stream = strdup(asapo_stream_info_get_next_stream(st));
			asapo_free_handle(st);
			break;
		}

		asapo_free_handle(st);
	}
	asapo_free_handle(si);
	asapo_free_handle(err);

	return 0;
}


static void skip_to_stream_end(struct im_asapo *a)
{
	/* FIXME: Implementation */
}


void *im_asapo_fetch(struct im_asapo *a, size_t *pdata_size)
{
	void *data_copy;
	AsapoMessageMetaHandle meta;
	AsapoMessageDataHandle data;
	AsapoErrorHandle err = asapo_new_handle();
	uint64_t msg_size;

	if ( a->stream == NULL ) {
		if ( select_last_stream(a) ) {
			asapo_free_handle(&err);
			return NULL;
		}
		skip_to_stream_end(a);
	}

	meta = asapo_new_handle();
	data = asapo_new_handle();

	asapo_consumer_get_next(a->consumer, a->group_id, &meta, &data,
	                        a->stream, &err);
	if ( asapo_error_get_type(err) == kEndOfStream ) {
		select_next_stream(a);
		asapo_free_handle(&err);
		asapo_free_handle(&meta);
		asapo_free_handle(&data);
		return NULL;  /* Please call back later! */
	}

	if ( asapo_is_error(err) ) {
		show_asapo_error("Couldn't get next ASAP::O record", err);
		asapo_free_handle(&err);
		asapo_free_handle(&meta);
		asapo_free_handle(&data);
		return NULL;
	}

	msg_size = asapo_message_meta_get_size(meta);

	STATUS("ASAP::O ID: %llu\n", asapo_message_meta_get_id(meta));
	STATUS("ASAP::O filename: %s\n", asapo_message_meta_get_name(meta));
	STATUS("ASAP::O size: %lli\n", (long long int)msg_size);

	data_copy = malloc(msg_size);
	if ( data_copy == NULL ) {
		ERROR("Failed to copy data block.\n");
		asapo_free_handle(&err);
		asapo_free_handle(&meta);
		asapo_free_handle(&data);
		return NULL;
	}
	memcpy(data_copy, asapo_message_data_get_as_chars(data), msg_size);

	asapo_free_handle(&err);
	asapo_free_handle(&meta);
	asapo_free_handle(&data);

	*pdata_size = msg_size;
	return data_copy;
}


void im_asapo_shutdown(struct im_asapo *a)
{
	if ( a == NULL ) return;
	asapo_free_handle(&a->consumer);
	asapo_free_handle(&a->group_id);
	free(a);
}
