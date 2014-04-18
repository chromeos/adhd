/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <sys/socket.h>
#include <syslog.h>

#include "cras_hfp_slc.h"
#include "cras_system_state.h"

#define SLC_BUF_SIZE_BYTES 256


/* Hands-free Audio Gateway feature bits, listed in according
 * to their order in the bitmap defined in HFP spec.
 */
/* Call waiting and 3-way calling */
#define HFP_THREE_WAY_CALLING           0x0001
/* EC and/or NR function */
#define HFP_EC_ANDOR_NR                 0x0002
/* Voice recognition activation */
#define HFP_VOICE_RECOGNITION           0x0004
/* Inband ringtone */
#define HFP_INBAND_RINGTONE             0x0008
/* Attach a number to voice tag */
#define HFP_ATTACH_NUMBER_TO_VOICETAG   0x0010
/* Ability to reject a call */
#define HFP_REJECT_A_CALL               0x0020
/* Enhanced call status */
#define HFP_ENHANCED_CALL_STATUS        0x0040
/* Enhanced call control */
#define HFP_ENHANCED_CALL_CONTRO        0x0080
/* Extended error result codes */
#define HFP_EXTENDED_ERROR_RESULT_CODES 0x0100
/* Codec negotiation */
#define HFP_CODEC_NEGOTIATION           0x0200

/* Indicator update command response and indicator indices.
 * Note that indicator index starts from '1'.
 */
#define CALL_IND_INDEX			4
#define CALLSETUP_IND_INDEX		5
#define INDICATOR_UPDATE_RSP		\
	"\r\n+CIND: "			\
	"(\"battchg\",(0-5),"		\
	"(\"signal\",(0-5)),"		\
	"(\"service\",(0,1)),"		\
	"(\"call\",(0,1)),"		\
	"(\"callsetup\",(0-3)),"	\
	"(\"callheld\",(0-2)),"		\
	"(\"roam\",(0,1))"		\
	"\r\n"

/* Handle object to hold required info to initialize and maintain
 * an HFP service level connection.
 * Args:
 *    buf - Buffer hold received commands.
 *    buf_read_idx - Read index for buf.
 *    buf_write_idx - Write index for buf.
 *    rfcomm_fd - File descriptor for the established RFCOMM connection.
 *    init_cb - Callback to be triggered when an SLC is initialized.
 *    data - Private data to be passed to init_cb.
 *    initialized - The service level connection is fully initilized of not
 *    cli_active - Calling line identification notification is enabled or not
 */
struct hfp_slc_handle {
	char buf[SLC_BUF_SIZE_BYTES];
	int buf_read_idx;
	int buf_write_idx;

	int rfcomm_fd;
	hfp_slc_init_cb init_cb;
	hfp_slc_disconnect_cb disconnect_cb;
	void *init_cb_data;
	int initialized;
	int cli_active;
	int call;
	int callsetup;
};

/* AT command exchanges between AG(Audio gateway) and HF(Hands-free device) */
struct at_command {
	const char *cmd;
	int (*callback) (struct hfp_slc_handle *handle, const char *cmd);
};

/* The active SLC handle, which is exposed mainly for HFP qualification. */
static struct hfp_slc_handle *active_slc_handle;

/* Sends a response or command to HF */
static int hfp_send(struct hfp_slc_handle *handle, const char *buf)
{
	int written, err, len;

	if (handle->rfcomm_fd < 0)
		return -EIO;

	len = strlen(buf);
	written = 0;
	while (written < len) {
		err = write(handle->rfcomm_fd,
			    buf + written, len - written);
		if (err < 0)
			return -errno;
		written += err;
	}

	return 0;
}

/* Sends a response for indicator event reporting. */
static int hfp_send_ind_event_report(struct hfp_slc_handle *handle,
				     int ind_index,
				     int value)
{
	char cmd[64];

	if (ind_index == CALL_IND_INDEX)
		handle->call = value;
	if (ind_index == CALLSETUP_IND_INDEX)
		handle->callsetup = value;

	snprintf(cmd, 64, "\r\n+CIEV: %d,%d\r\n", ind_index, value);
	return hfp_send(handle, cmd);
}

/* Sends calling line identification unsolicited result code. */
static int hfp_send_calling_line_identification(struct hfp_slc_handle *handle,
						const char *number,
						int type)
{
	char cmd[64];
	snprintf(cmd, 64, "\r\n+CLIP: \"%s\",%d\r\n", number, type);
	return hfp_send(handle, cmd);
}

/* ATA command to accept an incoming call. Mandatory support per spec 4.13. */
static int answer_call(struct hfp_slc_handle *handle, const char *cmd)
{
	int rc;
	rc = hfp_send(handle, "\r\nOK\r\n");
	if (rc)
		return rc;

	hfp_send_ind_event_report(handle, CALL_IND_INDEX, 1);
	if (rc)
		return rc;

	return hfp_send_ind_event_report(handle, CALLSETUP_IND_INDEX, 0);
}

/* AT+CCWA command to enable the "Call Waiting notification" function.
 * Mandatory support per spec 4.21. */
static int call_waiting_notify(struct hfp_slc_handle *handle, const char *buf)
{
	return hfp_send(handle, "\r\nOK\r\n");
}

/* AT+CLIP command to enable the "Calling Line Identification notification"
 * function. Mandatory per spec 4.23.
 */
static int cli_notification(struct hfp_slc_handle *handle, const char *cmd)
{
	handle->cli_active = (cmd[8] == '1');
	return hfp_send(handle, "\r\nOK\r\n");
}

/* ATDdd...dd command to place call with supplied number, or ATD>nnn...
 * command to dial the number stored at memory location. Mandatory per
 * spec 4.18 and 4.19.
 */
static int dial_number(struct hfp_slc_handle *handle, const char *cmd)
{
	int rc;
	rc = hfp_send(handle, "\r\nOK\r\n");
	if (rc)
		return rc;

	return hfp_send_ind_event_report(handle, CALLSETUP_IND_INDEX, 2);
}

/* AT+VTS command to generate a DTMF code. Mandatory per spec 4.27. */
static int dtmf_tone(struct hfp_slc_handle *handle, const char *buf)
{
	return hfp_send(handle, "\r\nOK\r\n");
}

/* AT+CMER command enables the registration status update function in AG.
 * The service level connection is consider initialized when successfully
 * responded OK to the AT+CMER command. Mandatory support per spec 4.4.
 */
static int event_reporting(struct hfp_slc_handle *handle, const char *cmd)
{
	char *tokens, *mode, *tmp;
	int err = 0;

	/* AT+CMER=[<mode>[,<keyp>[,<disp>[,<ind> [,<bfr>]]]]]
	 * Parse <ind>, the only token we care about.
	 */
	tokens = strdup(cmd);
	strtok(tokens, "=");

	mode = strtok(NULL, ",");
	tmp = strtok(NULL, ",");
	tmp = strtok(NULL, ",");
	tmp = strtok(NULL, ",");

	/* mode = 3 for forward unsolicited result codes.
	 * AT+CMER=3,0,0,1 activates “indicator events reporting” and the
	 * service level connection is established. Ignore other AT+CMER=
	 * command values.
	 */
	if (mode && atoi(mode) == 3 && tmp && atoi(tmp) == 1) {
		/* "indicator events reporting” is activated */
		if (handle->initialized) {
			syslog(LOG_ERR, "Service level connection has already"
					"been initialized");
		} else {
			err = hfp_send(handle, "\r\nOK\r\n");
			if (err == 0) {
				handle->init_cb(handle, handle->init_cb_data);
				handle->initialized = 1;
				handle->call = 0;
				handle->callsetup = 0;
			}
		}
	} else {
		syslog(LOG_ERR, "No service level connection established,"
				"got cmd=%s", cmd);
	}

	free(tokens);
	return err;
}

/* AT+CMEE command to set the "Extended Audio Gateway Error Result Code".
 * Mandatory per spec 4.9.
 */
static int extended_errors(struct hfp_slc_handle *handle, const char *buf)
{
	return hfp_send(handle, "\r\nOK\r\n");
}

/* AT+BLDN command to re-dial the last number. Mandatory support
 * per spec 4.20.
 */
static int last_dialed_number(struct hfp_slc_handle *handle, const char *buf)
{
	return hfp_send(handle, "\r\nOK\r\n");
}

/* AT+CLCC command to query list of current calls. Mandatory support
 * per spec 4.31.
 */
static int list_current_calls(struct hfp_slc_handle *handle, const char *cmd)
{
	return hfp_send(handle, "\r\nOK\r\n");
}

/* AT+COPS command to query currently selected operator or set name format.
 * Mandatory support per spec 4.8.
 */
static int operator_selection(struct hfp_slc_handle *handle, const char *buf)
{
	if (buf[7] == '?')
		/* HF sends AT+COPS? command to find current network operator.
		 * AG responds with +COPS:<mode>,<format>,<operator>, where
		 * the mode=0 means automatic for network selection. If no
		 * operator is selected, <format> and <operator> are omitted.
		 */
		return hfp_send(handle, "\r\n+COPS: 0\r\n");
	else
		return hfp_send(handle, "\r\nOK\r\n");
}

/* AT+CIND command retrieves the supported indicator and its corresponding
 * range and order index or read current status of indicators. Mandatory
 * support per spec 4.2.
 */
static int report_indicators(struct hfp_slc_handle *handle, const char *cmd)
{
	int err;

	if (cmd[7] == '=')
		/* Indicator update test command "AT+CIND=?" */
		err = hfp_send(handle, INDICATOR_UPDATE_RSP);
	else
		/* Indicator update read command "AT+CIND?"
		 * Battery charge and signal strength are full. Presence
		 * of service. No active call, held call, call set up and
		 * roaming.
		 */
		err = hfp_send(handle, "\r\n+CIND: 5,5,1,0,0,0,0\r\n");

	if (err < 0)
		return err;

	return hfp_send(handle, "\r\nOK\r\n");
}

/* AT+BIA command to change the subset of indicators that shall be
 * sent by the AG. It is okay to ignore this command here since we
 * don't do event reporting(CMER).
 */
static int indicator_activation(struct hfp_slc_handle *handle, const char *cmd)
{
	/* AT+BIA=[[<indrep 1>][,[<indrep 2>][,...[,[<indrep n>]]]]] */
	syslog(LOG_ERR, "Bluetooth indicator activation command %s", cmd);
	return hfp_send(handle, "\r\nOK\r\n");
}

/* AT+VGM and AT+VGS command reports the current mic and speaker gain
 * level respectively. Optional support per spec 4.28.
 */
static int signal_gain_setting(struct hfp_slc_handle *handle,
			       const char *cmd)
{
	int gain;

	if (strlen(cmd) < 8) {
		syslog(LOG_ERR, "Invalid gain setting command %s", cmd);
		return -EINVAL;
	}

	// TODO(hychao): set mic/speaker gain
	gain = atoi(&cmd[7]);
	syslog(LOG_ERR, "Reported gain level %d for %s", gain,
			cmd[5] == 'S' ? "speaker" : "microphone");

	return hfp_send(handle, "\r\nOK\r\n");
}

/* AT+CNUM command to query the subscriber number. Mandatory support
 * per spec 4.30.
 */
static int subscriber_number(struct hfp_slc_handle *handle, const char *buf)
{
	return hfp_send(handle, "\r\nOK\r\n");
}

/* AT+BRSF command notifies the HF(Hands-free device) supported features
 * and retrieves the AG(Audio gateway) supported features. Mandatory
 * support per spec 4.2.
 */
static int supported_features(struct hfp_slc_handle *handle, const char *cmd)
{
	int err;
	unsigned ag_supported_featutres = 0;
	char response[128];
	if (strlen(cmd) < 9)
		return -EINVAL;

	/* AT+BRSF=<feature> command received, ignore the HF supported feature
	 * for now. Respond with +BRSF:<feature> to notify mandatory supported
	 * features in AG(audio gateway).
	 */
	ag_supported_featutres = HFP_ENHANCED_CALL_STATUS;

	snprintf(response, 128, "\r\n+BRSF: %u\r\n", ag_supported_featutres);
	err = hfp_send(handle, response);
	if (err < 0)
		return err;

	return hfp_send(handle, "\r\nOK\r\n");
}

/* AT+CHUP command to terminate current call. Mandatory support
 * per spec 4.15.
 */
static int terminate_call(struct hfp_slc_handle *handle, const char *cmd)
{
	int rc;
	rc = hfp_send(handle, "\r\nOK\r\n");
	if (rc)
		return rc;

	if (handle->call) {
		rc = hfp_send_ind_event_report(handle, CALL_IND_INDEX, 0);
		if (rc)
			return rc;
	}
	if (handle->callsetup) {
		rc = hfp_send_ind_event_report(handle, CALLSETUP_IND_INDEX, 0);
		if (rc)
			return rc;
	}

	return 0;
}

/* AT commands to support in order to conform HFP specification.
 *
 * An initialized service level connection is the pre-condition for all
 * call related procedures. Note that for the call related commands,
 * we are good to just respond with a dummy "OK".
 *
 * The procedure to establish a service level connection is described below:
 *
 * 1. HF notifies AG about its own supported features and AG responds
 * with its supported feature.
 *
 * HF(hands-free)                             AG(audio gateway)
 *                     AT+BRSF=<HF supported feature> -->
 *                 <-- +BRSF:<AG supported feature>
 *                 <-- OK
 *
 * 2. HF retrieves the information about the indicators supported in AG.
 *
 * HF(hands-free)                             AG(audio gateway)
 *                     AT+CIND=? -->
 *                 <-- +CIND:...
 *                 <-- OK
 *
 * 3. The HF requests the current status of the indicators in AG.
 *
 * HF(hands-free)                             AG(audio gateway)
 *                     AT+CIND -->
 *                 <-- +CIND:...
 *                 <-- OK
 *
 * 4. HF requests enabling indicator status update in the AG.
 *
 * HF(hands-free)                             AG(audio gateway)
 *                     AT+CMER= -->
 *                 <-- OK
 */
static struct at_command at_commands[] = {
	{ "ATA", answer_call },
	{ "ATD", dial_number },
	{ "AT+BIA", indicator_activation },
	{ "AT+BLDN", last_dialed_number },
	{ "AT+BRSF", supported_features },
	{ "AT+CCWA", call_waiting_notify },
	{ "AT+CHUP", terminate_call },
	{ "AT+CIND", report_indicators },
	{ "AT+CLCC", list_current_calls },
	{ "AT+CLIP", cli_notification },
	{ "AT+CMEE", extended_errors },
	{ "AT+CMER", event_reporting },
	{ "AT+CNUM", subscriber_number },
	{ "AT+COPS", operator_selection },
	{ "AT+VG", signal_gain_setting },
	{ "AT+VTS", dtmf_tone },
	{ 0 }
};

static int handle_at_command(struct hfp_slc_handle *slc_handle,
			     const char *cmd) {
	struct at_command *atc;

	for (atc = at_commands; atc->cmd; atc++)
		if (!strncmp(cmd, atc->cmd, strlen(atc->cmd)))
			return atc->callback(slc_handle, cmd);

	syslog(LOG_ERR, "AT command %s not supported", cmd);
	return 0;
}

static void slc_watch_callback(void *arg)
{
	struct hfp_slc_handle *handle = (struct hfp_slc_handle *)arg;
	ssize_t bytes_read;
	int err;

	bytes_read = read(handle->rfcomm_fd,
			  &handle->buf[handle->buf_write_idx],
			  SLC_BUF_SIZE_BYTES - handle->buf_write_idx - 1);
	if (bytes_read < 0) {
		if (errno == ECONNRESET) {
			syslog(LOG_ERR,
			       "HFP service level connection disconnected "
			       "unexpectedly.");
			handle->disconnect_cb(handle);
			return;
		}

		handle->buf_read_idx = 0;
		handle->buf_write_idx = 0;
		syslog(LOG_ERR, "Error reading slc command %s",
				strerror(errno));
		return;
	}
	handle->buf_write_idx += bytes_read;
	handle->buf[handle->buf_write_idx] = '\0';

	while (handle->buf_read_idx != handle->buf_write_idx) {
		char *end_char;
		end_char = strchr(&handle->buf[handle->buf_read_idx], '\r');
		if (end_char == NULL)
			break;

		*end_char = '\0';
		err = handle_at_command(handle,
					&handle->buf[handle->buf_read_idx]);

		/* Shift the read index */
		handle->buf_read_idx = 1 + end_char - handle->buf;
		if (handle->buf_read_idx == handle->buf_write_idx) {
			handle->buf_read_idx = 0;
			handle->buf_write_idx = 0;
		}

		if (err < 0)
			break;
	}

	/* Handle the case when buffer is full and no command found. */
	if (handle->buf_write_idx == SLC_BUF_SIZE_BYTES - 1) {
		if (handle->buf_read_idx) {
			memmove(handle->buf,
				&handle->buf[handle->buf_read_idx],
				handle->buf_write_idx - handle->buf_read_idx);
			handle->buf_write_idx -= handle->buf_read_idx;
			handle->buf_read_idx = 0;
		} else {
			syslog(LOG_ERR,
			       "Parse SLC command error, clean up buffer");
			handle->buf_write_idx = 0;
		}
	}

	return;
}

/* Exported interfaces */

struct hfp_slc_handle *hfp_slc_create(int fd,
				      hfp_slc_init_cb init_cb,
				      void *init_cb_data,
				      hfp_slc_disconnect_cb disconnect_cb)
{
	struct hfp_slc_handle *handle;

	handle = calloc(1, sizeof(*handle));
	if (!handle)
		return NULL;

	handle->rfcomm_fd = fd;
	handle->init_cb = init_cb;
	handle->disconnect_cb = disconnect_cb;
	handle->init_cb_data = init_cb_data;
	active_slc_handle = handle;
	cras_system_add_select_fd(handle->rfcomm_fd,
				  slc_watch_callback, handle);

	return handle;
}

void hfp_slc_destroy(struct hfp_slc_handle *slc_handle)
{
	active_slc_handle = NULL;
	cras_system_rm_select_fd(slc_handle->rfcomm_fd);
	free(slc_handle);
}

struct hfp_slc_handle *hfp_slc_get_handle()
{
	return active_slc_handle;
}

/* Procedure to setup a call when AG sees incoming call.
 *
 * HF(hands-free)                             AG(audio gateway)
 *                                                     <-- Incoming call
 *                 <-- +CIEV: (callsetup = 1)
 *                 <-- RING (ALERT)
 */
int hfp_event_incoming_call(struct hfp_slc_handle *handle,
			    const char *number,
			    int type)
{
	int rc;

	rc = hfp_send_ind_event_report(handle, CALLSETUP_IND_INDEX, 1);
	if (rc)
		return rc;
	if (handle->cli_active) {
		rc = hfp_send_calling_line_identification(handle, number, type);
		if (rc)
			return rc;
	}
	return hfp_send(handle, "\r\nRING\r\n");
}

/* Procedure to setup a call from AG.
 *
 * HF(hands-free)                             AG(audio gateway)
 *                                                     <-- Call dropped
 *                 <-- +CIEV: (call = 0)
 */
int hfp_event_terminate_call(struct hfp_slc_handle *handle)
{
	return hfp_send_ind_event_report(handle, CALL_IND_INDEX, 0);
}

/* Procedure to answer a call from AG.
 *
 * HF(hands-free)                             AG(audio gateway)
 *                                                     <-- Call answered
 *                 <-- +CIEV: (call = 1)
 *                 <-- +CIEV: (callsetup = 0)
 */
int hfp_event_answer_call(struct hfp_slc_handle *handle)
{
	int rc;
	rc = hfp_send_ind_event_report(handle, CALL_IND_INDEX, 1);
	if (rc)
		return rc;
	return hfp_send_ind_event_report(handle, CALLSETUP_IND_INDEX, 0);
}
