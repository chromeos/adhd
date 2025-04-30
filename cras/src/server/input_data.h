/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_INPUT_DATA_H_
#define CRAS_SRC_SERVER_INPUT_DATA_H_

#include "cras/src/server/buffer_share.h"
#include "cras/src/server/cras_dsp_pipeline.h"
#include "cras/src/server/float_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

struct cras_iodev;
struct cras_rstream;

/*
 * Structure holding the information used when a chunk of input buffer
 * is accessed by multiple streams with different properties and
 * processing requirements.
 */
struct input_data {
  // Provides interface to read and process buffer in dsp pipeline.
  struct ext_dsp_module ext;
  // Pointer to the associated input iodev.
  struct cras_iodev* idev;
  // The audio area used for deinterleaved data copy.
  struct cras_audio_area* area;
  // Floating point buffer from input device.
  struct float_buffer* fbuffer;
};

/*
 * Creates an input_data instance for input iodev.
 * Args:
 *    idev - Pointer to the associated input device.
 */
struct input_data* input_data_create(struct cras_iodev* idev);

// Destroys an input_data instance.
void input_data_destroy(struct input_data** data);

// Sets how many frames in buffer has been read by all input streams.
void input_data_set_all_streams_read(struct input_data* data,
                                     unsigned int nframes);

/*
 * Gets an audio area for |stream| to read data from. An input_data may be
 * accessed by multiple streams while some requires processing, the
 * |offsets| arguments helps track the offset value each stream has read
 * into |data|.
 * Args:
 *    data - The input data to get audio area from.
 *    stream - The stream that reads data.
 *    offsets - Structure holding the mapping from stream to the offset value
 *        of how many frames each stream has read into input buffer.
 *    preprocessing_gain_scalar - Gain to apply before APM processing.
 *    area - To be filled with a pointer to an audio area struct for stream to
 *        read data.
 *    offset - To be filled with the samples offset in |area| that |stream|
 *        should start reading.
 */
int input_data_get_for_stream(struct input_data* data,
                              struct cras_rstream* stream,
                              struct buffer_share* offsets,
                              float preprocessing_gain_scalar,
                              struct cras_audio_area** area,
                              unsigned int* offset);

/*
 * Marks |frames| of audio data as read by |stream|.
 * Args:
 *    data - The input_data to mark frames has been read by |stream|.
 *    stream - The stream that has read audio data.
 *    offsets - Structure holding the mapping from stream to the offset value
 *        of how many frames each stream has read into input buffer.
 *    frames - Number of frames |stream| has read.
 */
int input_data_put_for_stream(struct input_data* data,
                              struct cras_rstream* stream,
                              struct buffer_share* offsets,
                              unsigned int frames);

struct input_data_gain {
  // Software gain scalar that should be applied before WebRTC-APM processing.
  float preprocessing_scalar;
  // Software gain scalar that should be applied after WebRTC-APM processing.
  float postprocessing_scalar;
};

/*
 * The software gain scaler of input path consist of two parts:
 * (1) The device gain scaler used when lack of hardware gain control.
 * Configured by the IntrinsicSensitivity label in alsa UCM config.
 * (2) The gain scaler in cras_rstream set by app, for example the AGC
 * module in Chrome.
 * Args:
 *    data - The input data that holds pointer to APM instance.
 *    idev_sw_agin_scaler - The gain scaler configured on input iodev.
 *    stream - To provide stream layer software gain.
 * Returns:
 *    The preprocessing and postprocessing gain.
 *    The preprocessing gain should be passed to input_data_get_for_stream().
 */
struct input_data_gain input_data_get_software_gain_scaler(
    struct input_data* data,
    float ui_gain_scalar,
    float idev_sw_gain_scaler,
    struct cras_rstream* stream);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_INPUT_DATA_H_
