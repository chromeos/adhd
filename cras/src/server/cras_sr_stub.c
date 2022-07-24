/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras_sr.h"

struct cras_sr *cras_sr_create(struct cras_sr_model_spec spec,
			       size_t input_nbytes)
{
	return NULL;
};

void cras_sr_destroy(struct cras_sr *sr){};

int cras_sr_process(struct cras_sr *sr, struct byte_buffer *input_buf,
		    struct byte_buffer *output_buf)
{
	return 0;
}
