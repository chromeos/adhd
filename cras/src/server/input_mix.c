
#include <stdlib.h>

#include "cras_types.h"
#include "input_mix.h"

static inline struct dev_write *find_dev(const struct dev_mix *mix,
					 unsigned int dev_id)
{
	unsigned int i;

	for (i = 0; i < mix->dev_sz; i++) {
		if (dev_id == mix->wr_idx[i].id)
			return &mix->wr_idx[i];
	}

	return NULL;
}

static void alloc_more_devs(struct dev_mix *mix)
{
	unsigned int new_size = mix->dev_sz * 2;
	unsigned int i;

	mix->wr_idx = realloc(mix->wr_idx, sizeof(mix->wr_idx[0]) * new_size);

	for (i = 0; i < mix->dev_sz; i++)
		mix->wr_idx[mix->dev_sz + i].id = NO_DEVICE;

	mix->dev_sz = new_size;
}

struct dev_mix *dev_mix_create(unsigned int buf_sz)
{
	struct dev_mix *mix;

	mix = calloc(1, sizeof(*mix));
	mix->dev_sz = INITIAL_DEV_SIZE;
	mix->wr_idx = calloc(mix->dev_sz, sizeof(mix->wr_idx[0]));
	mix->buf_sz = buf_sz;

	return mix;
}

void dev_mix_destroy(struct dev_mix *mix)
{
	if (!mix)
		return;
	free(mix->wr_idx);
	free(mix);
}

int dev_mix_add_dev(struct dev_mix *mix, unsigned int dev_id)
{
	struct dev_write *wr;

	wr = find_dev(mix, dev_id);
	if (wr)
		return -EEXIST;

	wr = find_dev(mix, NO_DEVICE);
	if (!wr)
		alloc_more_devs(mix);

	wr = find_dev(mix, NO_DEVICE);
	wr->id = dev_id;
	wr->wr_offset = mix->wr_point;

	return 0;
}

int dev_mix_rm_dev(struct dev_mix *mix, unsigned int dev_id)
{
	struct dev_write *wr;

	wr = find_dev(mix, dev_id);
	if (!wr)
		return -ENOENT;
	wr->id = NO_DEVICE;

	return 0;
}

int dev_mix_frames_added(struct dev_mix *mix, unsigned int dev_id,
			 unsigned int frames)
{
	unsigned int i;

	for (i = 0; i < mix->dev_sz; i++) {
		if (dev_id != mix->wr_idx[i].id)
			continue;

		mix->wr_idx[i].wr_offset += frames;
		mix->wr_idx[i].wr_offset %= mix->buf_sz;

		break;
	}

	return 0;
}

unsigned int dev_mix_get_new_write_point(struct dev_mix *mix)
{
	unsigned int min_written = mix->buf_sz;
	unsigned int min_index = 0;
	unsigned int i;
	unsigned int distance;
	struct dev_write *wr;

	for (i = 0; i < mix->dev_sz; i++) {
		wr = &mix->wr_idx[i];

		if (wr->id == NO_DEVICE)
			continue;

		if (wr->wr_offset >= mix->wr_point)
			distance = wr->wr_offset - mix->wr_point;
		else
			distance = mix->buf_sz -
					(mix->wr_point - wr->wr_offset);

		if (distance < min_written) {
			min_written = distance;
			min_index = i;
		}
	}

	mix->wr_point = mix->wr_idx[min_index].wr_offset;
	return min_written;
}

unsigned int dev_mix_dev_offset(const struct dev_mix *mix, unsigned int dev_id)
{
	unsigned int i;
	struct dev_write *wr;

	for (i = 0; i < mix->dev_sz; i++) {
		wr = &mix->wr_idx[i];

		if (wr->id != dev_id)
			continue;

		if (wr->wr_offset >= mix->wr_point)
			return wr->wr_offset - mix->wr_point;
		else
			return mix->buf_sz - (mix->wr_point - wr->wr_offset);
	}

	return 0;
}
