#ifndef INPUT_MIX_H_
#define INPUT_MIX_H_

#define INITIAL_DEV_SIZE 3

struct dev_write {
	unsigned int id;
	unsigned int wr_offset;
};

struct dev_mix {
	unsigned int buf_sz;
	unsigned int dev_sz;
	unsigned int wr_point;
	struct dev_write *wr_idx;
};

struct dev_mix *dev_mix_create(unsigned int buf_sz);

void dev_mix_destroy(struct dev_mix *mix);

int dev_mix_add_dev(struct dev_mix *mix, unsigned int dev_id);

int dev_mix_rm_dev(struct dev_mix *mix, unsigned int dev_id);

int dev_mix_frames_added(struct dev_mix *mix, unsigned int dev_id,
			 unsigned int frames);

/* Updates the write point to the minimum full buffer point.
 * Returns the number of minimum number of frames written.
 */
unsigned int dev_mix_get_new_write_point(struct dev_mix *mix);

#endif /* INPUT_MIX_H_ */
