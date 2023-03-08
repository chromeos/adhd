/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cras/src/server/cras_apm_reverse.h"

#include <pthread.h>

#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_stream_apm.h"
#include "cras/src/server/cras_system_state.h"
#include "cras/src/server/float_buffer.h"
#include "third_party/utlist/utlist.h"

/*
 * Object used to analyze playback audio from output iodev. It is responsible
 * to get buffer containing latest output data and provide it to the APM
 * instances which want to analyze reverse stream.
 *
 * The concept of this reverse_module is somewhat associated but not tied
 * to its |odev| member. The best way to understand it is to consider it
 * as a node connecting some streams to one output device.
 *
 * For example below diagram shows the topology when stream A wants to use
 * system default(device X) while stream B wants to use device Y as echo ref.
 *
 * stream A   ------> reverse module M  --> output device X (default)
 * stream B   ------> reverse module N  --> output device Y
 *
 * Once we add in the fact that default reverse module may change its tracking
 * device upon system settings switches, the complexity becomes higher.
 * Below diagram shows the topology when system default switches to device Y
 * which coincides with what reverse module N was tracking.
 *
 * It may seem strange at the first glance but actually is valid since all
 * streams are connected to an output device.
 *
 * The decision to make topology change in this way that leaves reverse module
 * N unused is based on our stats that 90% of the streams are set up to track
 * the system default output device. Therefore dropping module N causes less
 * structural change than dropping module M.
 *
 * stream A   ------> reverse module M  --> output device Y (default)
 *                /
 * stream B   ---/    reverse module N
 *
 * In above examples, reverse module M and N are indenpendent entities because
 * there exists a scenario requiring both to run simultaneously. Multiple
 * reverse modules share the same |process_reverse_callback| but have their
 * own private data.
 *
 * See start_reverse_process_on_dev and stop_reverse_process_on_dev for
 * how it interacts with a cras_iodev.
 */
struct cras_apm_reverse_module {
  // The interface for a processing block to add to cras_iodev's DSP
  // pipeline. Here it is implemented to serve output data from |odev|
  // for active apms to decide whether they need and to really use this
  // data for AEC reverse processing.
  struct ext_dsp_module ext;
  // The mutex to protect |fbuf| from main thread reconfigure it
  // while audio thread is processing the content data.
  pthread_mutex_t mutex;
  // Middle buffer holding reverse data for APMs to analyze.
  struct float_buffer* fbuf;
  // Pointer to the output iodev playing audio as the reverse
  // stream. NULL if there's no playback stream.
  struct cras_iodev* odev;
  // The sample rate odev is opened for.
  unsigned int dev_rate;
  // Flag to indicate if this reverse module needs to
  // process. The logic could be complex to determine if the overall
  // APM states requires this reverse module to process. Given that
  // ext->run() is called rather frequently from DSP pipeline, we use
  // this flag to save the computation every time.
  unsigned needs_to_process;
};

/* Structure to hold a list of cras_stream_apm instances.
 * This is going to be used with echo_ref_request to represent
 * records of request from stream_apms.
 */
struct stream_apm_request {
  struct cras_stream_apm* stream;
  struct stream_apm_request *prev, *next;
};
/* Structure to represent request to use certain output device as echo
 * ref for a handful of stream_apm. The purpose is to easily track
 * whether an output device is being used as echo reference.
 */
struct echo_ref_request {
  // the reverse module whose odev member is the echo ref.
  struct cras_apm_reverse_module rmod;
  // A list of stream_apm_requests where each
  // stream_apm_request holds a cras_apm_stream that wants to use
  // rmod->odev as echo ref.
  struct stream_apm_request* stream_apm_reqs;
  struct echo_ref_request *prev, *next;
};

// List of client requests to set specific aec ref for APMs.
static struct echo_ref_request* echo_ref_requests;

static bool hw_echo_ref_disabled = 0;

/* The reverse module corresponding to the dynamically changing default
 * enabled iodev in cras_iodev_list. It is subjected to change along
 * with audio output device selection. */
static struct cras_apm_reverse_module* default_rmod = NULL;

// The utilitiy functions provided during init and wrapper to call into them.
static process_reverse_t process_reverse_callback;
static process_reverse_needed_t process_reverse_needed_callback;
static output_devices_changed_t output_devices_changed_callback;

static int apm_process_reverse_callback(struct float_buffer* fbuf,
                                        unsigned int frame_rate,
                                        const struct cras_iodev* odev) {
  if (process_reverse_callback == NULL) {
    return 0;
  }
  return process_reverse_callback(fbuf, frame_rate, odev);
}
static int apm_process_reverse_needed(bool default_reverse,
                                      const struct cras_iodev* echo_ref) {
  if (process_reverse_needed_callback == NULL) {
    return 0;
  }
  return process_reverse_needed_callback(default_reverse, echo_ref);
}

static struct echo_ref_request* find_echo_ref_request_for_dev(
    const struct cras_iodev* echo_ref) {
  struct echo_ref_request* request;

  DL_FOREACH (echo_ref_requests, request) {
    if (request->rmod.odev == echo_ref) {
      return request;
    }
  }
  return NULL;
}

/*
 * Starts reverse processing on |dev|. An instance of reverse module is set
 * as a (struct ext_dsp_module *) to cras_iodev_set_ext_dsp_module() so that
 * when audio data runs through the dev's DSP pipeline it will trigger
 * reverse_data_run() with |rmod| passed in as the callback data.
 *
 * This is called from main thread so it ensures the open/close state of |dev|
 * is unchanged.
 */
static void start_reverse_process_on_dev(struct cras_iodev* dev,
                                         struct cras_apm_reverse_module* rmod) {
  /* Below call is safe even if |dev| is running in audio thread, because
   * accessing iodev's dsp pipeline is protected by mutex.
   */
  cras_iodev_set_ext_dsp_module(dev, &rmod->ext);
}

/*
 * Stops reverse processing on |dev|. Called from main thread.
 */
static void stop_reverse_process_on_dev(struct cras_iodev* dev) {
  // Safe call for the same reason as in start_reverse_process_on_dev.
  cras_iodev_set_ext_dsp_module(dev, NULL);
}

/*
 * Determines the iodev to be used as the echo reference for APM reverse
 * analysis. If there exists the special purpose "echo reference dev" then
 * use it. Otherwise just use this output iodev.
 */
static struct cras_iodev* get_echo_reference_target(struct cras_iodev* iodev) {
  // Don't use HW echo_reference_dev if specified in board config.
  if (hw_echo_ref_disabled) {
    return iodev;
  }
  return iodev->echo_reference_dev ? iodev->echo_reference_dev : iodev;
}

static void destroy_echo_ref_request(struct echo_ref_request* req) {
  if (req->rmod.fbuf) {
    float_buffer_destroy(&req->rmod.fbuf);
  }
  pthread_mutex_destroy(&req->rmod.mutex);
  free(req);
}

/*
 * Gets the first enabled output iodev in the list, determines the echo
 * reference target base on this output iodev, and registers default_rmod as
 * ext dsp module to this echo reference target.
 * When this echo reference iodev is opened and audio data flows through its
 * dsp pipeline, APMs will anaylize the reverse stream. This is expected to be
 * called in main thread when output devices enable/dsiable state changes.
 */
static void handle_iodev_states_changed(struct cras_iodev* iodev,
                                        void* cb_data) {
  struct cras_iodev *echo_ref, *old;
  struct echo_ref_request* request;

  if (iodev && (iodev->direction != CRAS_STREAM_OUTPUT)) {
    return;
  }

  // Register to the first enabled output device.
  iodev = cras_iodev_list_get_first_enabled_iodev(CRAS_STREAM_OUTPUT);
  if (iodev == NULL) {
    return;
  }

  echo_ref = get_echo_reference_target(iodev);

  // If default_rmod is already tracking echo_ref, do nothing.
  if (default_rmod->odev == echo_ref) {
    return;
  }

  /* Swap the odev on default_rmod to |echo_ref|. Changes will be updated
   * to audio thread in two stages asynchronously:
   *
   * (1) The odev argument to apm_process_reverse_callback in iodev's DSP
   * pipeline changes upon start/stop_reverse_process_on_dev calls. Audio
   * thread accesses reverse modules ONLY through the DSP pipeline of an
   * iodev which is mutex protected.
   * (2) Main thread events trigger cras_apm_reverse_state_update() call
   * in audio thread to update the |needs_to_process| flag on reverse
   * modules.
   *
   * The above (1) decides what iodev to pass to processing callbacks as
   * echo ref, while (2) decides whether processing callbacks shall be
   * executed or not.
   * This 2-stages control can't be merged just because signals originate
   * from separate threads: audio thread knows when APMs are started with
   * what effects, main thread knows where the latest default output is
   * selected to and user requests for echo ref.
   */
  old = default_rmod->odev;
  default_rmod->odev = echo_ref;
  start_reverse_process_on_dev(echo_ref, default_rmod);

  /* Detach from the old iodev that default_rmod was tracking.
   * Note that |old| could be NULL when this function is called for the
   * first time during init. */
  if (old) {
    request = find_echo_ref_request_for_dev(old);
    // If there was a request for |old| set to its rmod.
    if (request) {
      start_reverse_process_on_dev(old, &request->rmod);
    } else {
      stop_reverse_process_on_dev(old);
    }
  }

  // Notify stream APMs of the output devices change on reverse side.
  if (output_devices_changed_callback) {
    output_devices_changed_callback();
  }
}

/* When notified about iodev removal, we need to clean up the echo ref request
 * record to avoid audio thread using it. These clean up tasks were supposed to
 * be done in cras_apm_reverse_link_echo_ref() but browser client just doesn't
 * react quick enough to tell CRAS to fallback to default.
 */
static void handle_iodev_removed(struct cras_iodev* iodev) {
  struct echo_ref_request* request;
  struct stream_apm_request* stream_apm_req;

  request = find_echo_ref_request_for_dev(iodev);
  if (request == NULL) {
    return;
  }

  /* If there exists an echo ref request on this iodev, detach it from any
   * reverse module set earlier. This means to remove any reverse module
   * from iodev's DSP pipeline, which is mutex protected so we're safe
   * to do that here in main thread. */
  stop_reverse_process_on_dev(iodev);

  DL_FOREACH (request->stream_apm_reqs, stream_apm_req) {
    DL_DELETE(request->stream_apm_reqs, stream_apm_req);
    free(stream_apm_req);
  }
  DL_DELETE(echo_ref_requests, request);
  destroy_echo_ref_request(request);
}

static void reverse_data_run(struct ext_dsp_module* ext, unsigned int nframes) {
  struct cras_apm_reverse_module* rmod = (struct cras_apm_reverse_module*)ext;
  unsigned int writable;
  int i, offset = 0;
  float* const* wp;

  if (!rmod->needs_to_process) {
    return;
  }

  /* Repeat the loop to copy total nframes of data from the DSP pipeline
   * (i.e ext->ports) over to rmod->fbuf as AEC reference for the actual
   * processing work in apm_process_reverse_callback.
   */
  pthread_mutex_lock(&rmod->mutex);
  while (nframes) {
    /* If at any moment the rmod->fbuf is full, call out to
     * the process reverse callback and then reset it to mark
     * AEC reference data as consumed. */
    if (!float_buffer_writable(rmod->fbuf)) {
      apm_process_reverse_callback(rmod->fbuf, rmod->dev_rate, rmod->odev);
      float_buffer_reset(rmod->fbuf);
    }
    writable = float_buffer_writable(rmod->fbuf);
    writable = MIN(nframes, writable);
    wp = float_buffer_write_pointer(rmod->fbuf);

    // Discard higher channels beyond the limit.
    unsigned int channels = MIN(rmod->fbuf->num_channels, MAX_EXT_DSP_PORTS);
    for (i = 0; i < channels; i++) {
      memcpy(wp[i], ext->ports[i] + offset, writable * sizeof(float));
    }

    offset += writable;
    float_buffer_written(rmod->fbuf, writable);
    nframes -= writable;
  }
  pthread_mutex_unlock(&rmod->mutex);
}

static void reverse_data_configure(struct ext_dsp_module* ext,
                                   unsigned int buffer_size,
                                   unsigned int num_channels,
                                   unsigned int rate) {
  struct cras_apm_reverse_module* rmod = (struct cras_apm_reverse_module*)ext;
  pthread_mutex_lock(&rmod->mutex);
  if (rmod->fbuf) {
    float_buffer_destroy(&rmod->fbuf);
  }
  rmod->fbuf =
      float_buffer_create(rate / APM_NUM_BLOCKS_PER_SECOND, num_channels);
  rmod->dev_rate = rate;
  pthread_mutex_unlock(&rmod->mutex);
}

/* Creates a cras_apm_reverse_module, which represents a DSP module runs
 * within the DSP pipeline of an iodev.
 * Args:
 *    odev - The echo ref iodev to add the reverse module in its DSP pipeline.
 * Returns:
 *    Newly created reverse module associated with odev. It represents
 *    an echo reference entity for our APM processing implementation.
 */
static struct cras_apm_reverse_module* create_apm_reverse_module(
    struct cras_iodev* odev) {
  struct cras_apm_reverse_module* rmod;

  rmod = (struct cras_apm_reverse_module*)calloc(1, sizeof(*rmod));
  if (rmod == NULL) {
    return NULL;
  }
  pthread_mutex_init(&rmod->mutex, NULL);
  rmod->ext.run = reverse_data_run;
  rmod->ext.configure = reverse_data_configure;
  rmod->odev = odev;
  return rmod;
}

int cras_apm_reverse_init(process_reverse_t process_cb,
                          process_reverse_needed_t process_needed_cb,
                          output_devices_changed_t output_devices_changed_cb) {
  process_reverse_callback = process_cb;
  process_reverse_needed_callback = process_needed_cb;
  output_devices_changed_callback = output_devices_changed_cb;

  hw_echo_ref_disabled = cras_system_get_hw_echo_ref_disabled();

  if (default_rmod == NULL) {
    default_rmod = create_apm_reverse_module(NULL);
    if (!default_rmod) {
      return -ENOMEM;
    }
  }

  cras_iodev_list_set_device_enabled_callback(handle_iodev_states_changed,
                                              handle_iodev_states_changed,
                                              handle_iodev_removed, NULL);
  handle_iodev_states_changed(NULL, NULL);
  return 0;
}

void cras_apm_reverse_state_update() {
  struct echo_ref_request* request;

  default_rmod->needs_to_process =
      apm_process_reverse_needed(1, default_rmod->odev);

  DL_FOREACH (echo_ref_requests, request) {
    request->rmod.needs_to_process =
        apm_process_reverse_needed(0, request->rmod.odev);
  }
}

static struct echo_ref_request* create_echo_ref_request(
    struct cras_iodev* echo_ref) {
  struct echo_ref_request* req;

  // NULL echo ref is invalid.
  if (echo_ref == NULL) {
    return NULL;
  }

  req = (struct echo_ref_request*)calloc(1, sizeof(*req));
  if (req == NULL) {
    return NULL;
  }
  pthread_mutex_init(&req->rmod.mutex, NULL);
  req->rmod.odev = echo_ref;
  req->rmod.ext.run = reverse_data_run;
  req->rmod.ext.configure = reverse_data_configure;
  DL_APPEND(echo_ref_requests, req);
  return req;
}

static struct stream_apm_request* create_stream_apm_request(
    struct cras_stream_apm* stream) {
  struct stream_apm_request* req;

  req = (struct stream_apm_request*)calloc(1, sizeof(*req));
  if (req == NULL) {
    return NULL;
  }
  req->stream = stream;
  return req;
}

/* Look up in the existing records if there exists a request to link |stream|
 * to a specific echo ref device.
 * Args:
 *     stream - To match with the stream_apm_requests in all echo_ref_requests.
 *     echo_ref_req - To be filled with the echo_ref_request that matched.
 *     stream_apm_req - To be filled with the stream_apm_request that matched.
 *         echo_ref_req and stream_apm_req should both be NULL if no match,
 *         otherwise both should be non-NULL.
 */
static void find_echo_ref_request_for_stream(
    struct cras_stream_apm* stream,
    struct echo_ref_request** echo_ref_req,
    struct stream_apm_request** stream_apm_req) {
  *echo_ref_req = NULL;
  *stream_apm_req = NULL;
  DL_FOREACH (echo_ref_requests, *echo_ref_req) {
    DL_FOREACH ((*echo_ref_req)->stream_apm_reqs, *stream_apm_req) {
      if ((*stream_apm_req)->stream == stream) {
        return;
      }
    }
  }
}

static void remove_stream_apm_request(
    struct echo_ref_request* request,
    struct stream_apm_request* stream_apm_req) {
  // Unlink this |stream_apm_req| with the old echo ref request.
  DL_DELETE(request->stream_apm_reqs, stream_apm_req);

  if (request->stream_apm_reqs) {
    return;
  }

  // Deep clean up if no one is using this echo ref.
  DL_DELETE(echo_ref_requests, request);
  if (request->rmod.odev != default_rmod->odev) {
    stop_reverse_process_on_dev(request->rmod.odev);
  }
  destroy_echo_ref_request(request);
}

/*
 * CRAS client could request to add arbitrary output devices as echo ref.
 * Each explicitly set echo ref should have a reverse module allocated.
 * When clients have unset the echo ref to the point that non of the
 * stream apm is using it, cras_apm_reverse_state_update() takes the job to
 * free up the rmod.
 */
int cras_apm_reverse_link_echo_ref(struct cras_stream_apm* stream,
                                   struct cras_iodev* echo_ref) {
  struct echo_ref_request* request = NULL;
  struct stream_apm_request* stream_apm_req = NULL;

  find_echo_ref_request_for_stream(stream, &request, &stream_apm_req);

  if (request && stream_apm_req) {
    // Skip if the request has already been fulfilled.
    if (request->rmod.odev == echo_ref) {
      return 0;
    }
    // Remove stream_apm_req from this old echo ref request.
    remove_stream_apm_request(request, stream_apm_req);
  }
  // This is a unset echo ref request, simply free this |stream_apm_req|.
  if (echo_ref == NULL) {
    if (stream_apm_req) {
      free(stream_apm_req);
    }
    return 0;
  }
  // The first request from |stream|, make a record for it.
  if (stream_apm_req == NULL) {
    stream_apm_req = create_stream_apm_request(stream);
    if (stream_apm_req == NULL) {
      return -ENOMEM;
    }
  }

  // Find or create the echo_ref_request and add stream_apm_req to it.
  request = find_echo_ref_request_for_dev(echo_ref);
  if (request == NULL) {
    request = create_echo_ref_request(echo_ref);
    if (request == NULL) {
      free(stream_apm_req);
      return -ENOMEM;
    }

    /* New request to link |echo_ref|, if it's not what the default
     * rmod is tracking then start reverse process on it.
     */
    if (echo_ref != default_rmod->odev) {
      start_reverse_process_on_dev(echo_ref, &request->rmod);
    }
  }
  DL_APPEND(request->stream_apm_reqs, stream_apm_req);
  return 0;
}

bool cras_apm_reverse_is_aec_use_case(struct cras_iodev* echo_ref) {
  struct echo_ref_request* request;

  DL_FOREACH (echo_ref_requests, request) {
    if (request->rmod.odev == echo_ref) {
      return cras_iodev_is_tuned_aec_use_case(echo_ref->active_node);
    }
  }
  /* Invalid usage if caller didn't call init first. And we don't care
   * what is returned in that case, so let's give it a false. */
  if (!default_rmod) {
    return 0;
  }
  return cras_iodev_is_tuned_aec_use_case(default_rmod->odev->active_node);
}

void cras_apm_reverse_deinit() {
  struct echo_ref_request* request = NULL;
  struct stream_apm_request* stream_apm_req = NULL;

  DL_FOREACH (echo_ref_requests, request) {
    DL_FOREACH (request->stream_apm_reqs, stream_apm_req) {
      DL_DELETE(request->stream_apm_reqs, stream_apm_req);
      free(stream_apm_req);
    }
    DL_DELETE(echo_ref_requests, request);

    stop_reverse_process_on_dev(request->rmod.odev);
    destroy_echo_ref_request(request);
  }
  if (default_rmod) {
    if (default_rmod->odev) {
      stop_reverse_process_on_dev(default_rmod->odev);
    }
    if (default_rmod->fbuf) {
      float_buffer_destroy(&default_rmod->fbuf);
    }
    pthread_mutex_destroy(&default_rmod->mutex);
    free(default_rmod);
    default_rmod = NULL;
  }
}
