/* Copyright (c) 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras_rstream.h"
#include "cras_types.h"
#include "stream_list.h"
#include "utlist.h"

struct stream_list {
	struct cras_rstream *streams;
	stream_callback *stream_added_cb;
	stream_callback *stream_removed_cb;
};

struct stream_list *stream_list_create(stream_callback *add_cb,
				       stream_callback *rm_cb)
{
	struct stream_list *list = calloc(1, sizeof(struct stream_list));

	list->stream_added_cb = add_cb;
	list->stream_removed_cb = rm_cb;
	return list;
}

void stream_list_destroy(struct stream_list *list)
{
	free(list);
}

const struct cras_rstream *stream_list_get(struct stream_list *list)
{
	return list->streams;
}

int stream_list_add(struct stream_list *list, struct cras_rstream *stream)
{
	DL_APPEND(list->streams, stream);
	return list->stream_added_cb(stream);
}

struct cras_rstream *stream_list_rm(struct stream_list *list,
				    cras_stream_id_t id)
{
	struct cras_rstream *to_remove;
	struct cras_rstream *removed = NULL;

	DL_SEARCH_SCALAR(list->streams, to_remove, stream_id, id);
	if (to_remove) {
		DL_DELETE(list->streams, to_remove);
		list->stream_removed_cb(to_remove);
		DL_APPEND(removed, to_remove);
	}

	return removed;
}

struct cras_rstream *stream_list_rm_all_client_streams(
		struct stream_list *list, struct cras_rclient *rclient)
{
	struct cras_rstream *to_remove;
	struct cras_rstream *removed_list = NULL;

	DL_FOREACH(list->streams, to_remove) {
		if (to_remove->client == rclient) {
			DL_DELETE(list->streams, to_remove);
			list->stream_removed_cb(to_remove);
			DL_APPEND(removed_list, to_remove);
		}
	}

	return removed_list;

}

