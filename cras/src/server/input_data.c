/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <syslog.h>

#include "buffer_share.h"
#include "cras_audio_area.h"
#include "cras_mix.h"
#include "cras_rstream.h"
#include "cras_system_state.h"
#include "input_data.h"
#include "utlist.h"

struct input_data *input_data_create(void *dev_ptr)
{
	struct input_data *data = (struct input_data *)calloc(1, sizeof(*data));

	data->dev_ptr = dev_ptr;
	return data;
}

void input_data_destroy(struct input_data **data)
{
	free(*data);
	*data = NULL;
}

int input_data_get_for_stream(struct input_data *data,
			      struct cras_rstream *stream,
			      struct buffer_share *offsets,
			      struct cras_audio_area **area,
			      unsigned int *offset)
{
	*area = data->area;
	*offset = buffer_share_id_offset(offsets, stream->stream_id);

	return 0;
}

int input_data_put_for_stream(struct input_data *data,
			      struct cras_rstream *stream,
			      struct buffer_share *offsets,
			      unsigned int frames)
{
	buffer_share_offset_update(offsets, stream->stream_id, frames);

	return 0;
}
