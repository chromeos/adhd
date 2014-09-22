# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

AUTOMAKE_OPTIONS = subdir-objects
ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS}

COMMON_CPPFLAGS = -O2 -Wall -Werror -Wno-error=cpp

bin_PROGRAMS = cras cras_test_client
cras_SOURCES = \
	common/cras_audio_format.c \
	common/cras_checksum.c \
	common/cras_config.c \
	common/cras_fmt_conv.c \
	common/cras_metrics.c \
	common/cras_util.c \
	common/dumper.c \
	common/edid_utils.c \
	dsp/biquad.c \
	dsp/crossover.c \
	dsp/crossover2.c \
	dsp/drc.c \
	dsp/drc_kernel.c \
	dsp/drc_math.c \
	dsp/dsp_util.c \
	dsp/eq.c \
	dsp/eq2.c \
	server/audio_thread.c \
	server/config/cras_card_config.c \
	server/config/cras_device_blacklist.c \
	server/cras.c \
	server/cras_a2dp_endpoint.c \
	server/cras_a2dp_info.c \
	server/cras_a2dp_iodev.c \
	server/cras_alert.c \
	server/cras_alsa_card.c \
	server/cras_alsa_helpers.c \
	server/cras_alsa_io.c \
	server/cras_alsa_jack.c \
	server/cras_alsa_mixer.c \
	server/cras_alsa_ucm.c \
	server/cras_audio_area.c \
	server/cras_bt_manager.c \
	server/cras_bt_adapter.c \
	server/cras_bt_device.c \
	server/cras_bt_transport.c \
	server/cras_bt_endpoint.c \
	server/cras_bt_profile.c \
	server/cras_dbus.c \
	server/cras_dbus_util.c \
	server/cras_dbus_control.c \
	server/cras_dsp.c \
	server/cras_dsp_ini.c \
	server/cras_dsp_mod_builtin.c \
	server/cras_dsp_mod_ladspa.c \
	server/cras_dsp_pipeline.c \
	server/cras_empty_iodev.c \
	server/cras_expr.c \
	server/cras_gpio_jack.c \
	server/cras_hfp_ag_profile.c \
	server/cras_hfp_info.c \
	server/cras_hfp_iodev.c \
	server/cras_hfp_slc.c \
	server/cras_iodev.c \
	server/cras_iodev_list.c \
	server/cras_loopback_iodev.c \
	server/cras_mix.c \
	server/cras_rclient.c \
	server/cras_rstream.c \
	server/buffer_share.c \
	common/cras_sbc_codec.c \
	server/cras_server.c \
	server/cras_server_metrics.c \
	server/cras_system_state.c \
	server/cras_telephony.c \
	server/cras_tm.c \
	server/cras_udev.c \
	server/cras_volume_curve.c \
	server/dev_stream.c \
	server/rate_estimator.c \
	server/softvol_curve.c

cras_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/dsp -I$(top_srcdir)/src/server \
	-I$(top_srcdir)/src/server/config \
	$(DBUS_CFLAGS) $(SBC_CFLAGS)
cras_LDADD = -lpthread -lasound -lrt -liniparser -ludev -ldl -lm -lspeexdsp \
	$(SBC_LIBS) \
	$(DBUS_LIBS)

lib_LTLIBRARIES = libcras.la
libcras_la_SOURCES = \
	common/cras_audio_format.c \
	common/cras_config.c \
	common/cras_fmt_conv.c \
	common/cras_sbc_codec.c \
	common/cras_util.c \
	common/edid_utils.c \
	libcras/cras_client.c

include_HEADERS = \
	common/cras_audio_format.h \
	common/cras_config.h \
	common/cras_fmt_conv.h \
	common/cras_iodev_info.h \
	common/cras_messages.h \
	common/cras_sbc_codec.h \
	common/cras_shm.h \
	common/cras_types.h \
	common/cras_util.h \
	common/edid_utils.h \
	common/utlist.h \
	libcras/cras_client.h

libcras_la_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/libcras $(SBC_CFLAGS)
libcras_la_LIBADD = -lpthread -lasound -lrt -lspeexdsp \
	$(SBC_LIBS)
libcras_la_LDFLAGS = -version-info 0:0:0

asound_module_pcm_cras_LTLIBRARIES = libasound_module_pcm_cras.la
asound_module_ctl_cras_LTLIBRARIES = libasound_module_ctl_cras.la
asound_module_pcm_crasdir = @ALSA_PLUGIN_DIR@
asound_module_ctl_crasdir = @ALSA_PLUGIN_DIR@
libasound_module_pcm_cras_la_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/libcras
libasound_module_pcm_cras_la_LDFLAGS = -module -avoid-version -export-dynamic -no-undefined \
	$(LDFLAGS_NOUNDEFINED)
libasound_module_ctl_cras_la_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/libcras
libasound_module_ctl_cras_la_LDFLAGS = -module -avoid-version -export-dynamic -no-undefined \
	$(LDFLAGS_NOUNDEFINED)
libasound_module_pcm_cras_la_SOURCES = alsa_plugin/pcm_cras.c
libasound_module_pcm_cras_la_LIBADD = -lasound libcras.la
libasound_module_ctl_cras_la_SOURCES = alsa_plugin/ctl_cras.c
libasound_module_ctl_cras_la_LIBADD = -lasound libcras.la

# Inject a dependency between the installation rules of libcras and its modules.
# This avoids a race when the modules are relinked before libcras is actually
# installed.
#
# Automake will refuse to generate a rule that collides with a user-specified
# one. The hide_install variable prevents automake from noticing these rules,
# so everything else will behave as usual.
hide_install=install
$(hide_install)-asound_module_pcm_crasLTLIBRARIES: install-libLTLIBRARIES
$(hide_install)-asound_module_ctl_crasLTLIBRARIES: install-libLTLIBRARIES

TESTS = \
	audio_area_unittest \
	audio_format_unittest \
	a2dp_info_unittest \
	a2dp_iodev_unittest \
	alert_unittest \
	alsa_card_unittest \
	alsa_helpers_unittest \
	alsa_io_unittest \
	alsa_jack_unittest \
	alsa_mixer_unittest \
	alsa_ucm_unittest \
	array_unittest \
	card_config_unittest \
	checksum_unittest \
	cras_client_unittest \
	cras_tm_unittest \
	dev_stream_unittest \
	device_blacklist_unittest \
	dsp_core_unittest \
	dsp_ini_unittest \
	dsp_pipeline_unittest \
	dsp_unittest \
	dumper_unittest \
	edid_utils_unittest \
	expr_unittest \
	fmt_conv_unittest \
	hfp_info_unittest \
	hfp_iodev_unittest \
	hfp_slc_unittest \
	buffer_share_unittest \
	iodev_list_unittest \
	iodev_unittest \
	loopback_iodev_unittest \
	mix_unittest \
	rate_estimator_unittest \
	rclient_unittest \
	rstream_unittest \
	shm_unittest \
	system_state_unittest \
	util_unittest \
	volume_curve_unittest

check_PROGRAMS = $(TESTS)

cras_test_client_SOURCES = tests/cras_test_client.c
cras_test_client_LDADD = -lm libcras.la
cras_test_client_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/libcras \
	-I$(top_srcdir)/src/common

tests/cras_test_client.c: common/cras_version.h

CLEANFILES = common/cras_version.h
.PHONY: common/cras_version.h
common/cras_version.h:
	if echo "$(CPPFLAGS)" | grep -q -- '-DVCSID='; then \
		echo > $@.tmp; \
	else \
		commit="outoftree-$$(git rev-parse HEAD)"; \
		if [ -n "$$(git diff-index --name-only HEAD)" ]; then \
			commit="$${commit}-dirty"; \
		fi; \
		echo "#define VCSID \"$$commit\"" > $@.tmp; \
	fi
	diff $@.tmp $@ && rm -f $@.tmp || mv $@.tmp $@

# dsp test programs (not run automatically)
check_PROGRAMS += \
	crossover_test \
	crossover2_test \
	drc_test \
	eq_test \
	eq2_test \
	cmpraw

crossover_test_SOURCES = dsp/crossover.c dsp/biquad.c dsp/dsp_util.c \
	dsp/tests/crossover_test.c dsp/tests/dsp_test_util.c dsp/tests/raw.c
crossover_test_LDADD = -lrt -lm
crossover_test_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/dsp

crossover2_test_SOURCES = dsp/crossover2.c dsp/biquad.c dsp/dsp_util.c \
	dsp/tests/crossover2_test.c dsp/tests/dsp_test_util.c dsp/tests/raw.c
crossover2_test_LDADD = -lrt -lm
crossover2_test_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/dsp

drc_test_SOURCES = dsp/drc.c dsp/drc_kernel.c dsp/drc_math.c \
	dsp/crossover2.c dsp/eq2.c dsp/biquad.c dsp/dsp_util.c \
	dsp/tests/drc_test.c dsp/tests/dsp_test_util.c dsp/tests/raw.c
drc_test_LDADD = -lrt -lm
drc_test_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/dsp

eq_test_SOURCES = dsp/biquad.c dsp/eq.c dsp/dsp_util.c dsp/tests/eq_test.c \
	dsp/tests/dsp_test_util.c dsp/tests/raw.c
eq_test_LDADD = -lrt -lm
eq_test_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/dsp

eq2_test_SOURCES = dsp/biquad.c dsp/eq2.c dsp/dsp_util.c dsp/tests/eq2_test.c \
	dsp/tests/dsp_test_util.c dsp/tests/raw.c
eq2_test_LDADD = -lrt -lm
eq2_test_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/dsp

cmpraw_SOURCES = dsp/tests/cmpraw.c dsp/tests/raw.c
cmpraw_LDADD = -lm
cmpraw_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/dsp

# unit tests
alert_unittest_SOURCES = tests/alert_unittest.cc \
	server/cras_alert.c
alert_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) \
	-I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server
alert_unittest_LDADD = -lgtest -lpthread

alsa_card_unittest_SOURCES = tests/alsa_card_unittest.cc \
	server/cras_alsa_card.c
alsa_card_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) \
	-I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server \
	-I$(top_srcdir)/src/server/config
alsa_card_unittest_LDADD = -lgtest -lpthread

alsa_helpers_unittest_SOURCES = tests/alsa_helpers_unittest.cc \
	common/cras_audio_format.c
alsa_helpers_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) \
	-I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server
alsa_helpers_unittest_LDADD = -lgtest -lpthread

audio_area_unittest_SOURCES = tests/audio_area_unittest.cc \
	server/cras_audio_area.c
audio_area_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/server \
	-I$(top_srcdir)/src/common
audio_area_unittest_LDADD = -lgtest -lpthread

audio_format_unittest_SOURCES = tests/audio_format_unittest.cc
audio_format_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/server \
	-I$(top_srcdir)/src/common
audio_format_unittest_LDADD = -lgtest -lpthread

a2dp_info_unittest_SOURCES = tests/a2dp_info_unittest.cc \
	server/cras_a2dp_info.c
a2dp_info_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/server \
	-I$(top_srcdir)/src/common
a2dp_info_unittest_LDADD = -lgtest -lpthread

a2dp_iodev_unittest_SOURCES = tests/a2dp_iodev_unittest.cc \
	server/cras_a2dp_iodev.c
a2dp_iodev_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/server \
	-I$(top_srcdir)/src/common $(DBUS_CFLAGS)
a2dp_iodev_unittest_LDADD = -lgtest -lpthread $(DBUS_LIBS)

alsa_io_unittest_SOURCES = tests/alsa_io_unittest.cc
alsa_io_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/server \
	-I$(top_srcdir)/src/common
alsa_io_unittest_LDADD = -lgtest -lpthread

alsa_jack_unittest_SOURCES = tests/alsa_jack_unittest.cc \
	server/cras_alsa_jack.c
alsa_jack_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server
alsa_jack_unittest_LDADD = -lgtest -lpthread

alsa_mixer_unittest_SOURCES = tests/alsa_mixer_unittest.cc \
	server/cras_alsa_mixer.c
alsa_mixer_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) \
	-I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server \
	-I$(top_srcdir)/src/server/config
alsa_mixer_unittest_LDADD = -lgtest -lpthread

alsa_ucm_unittest_SOURCES = tests/alsa_ucm_unittest.cc
alsa_ucm_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) \
	-I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server \
	-I$(top_srcdir)/src/server/config
alsa_ucm_unittest_LDADD = -lgtest -lpthread

array_unittest_SOURCES = tests/array_unittest.cc
array_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common
array_unittest_LDADD = -lgtest -lpthread

audio_thread_unittest_SOURCES = tests/audio_thread_unittest.cc
audio_thread_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) \
	-I$(top_srcdir)/src/common -I$(top_srcdir)/src/server
audio_thread_unittest_LDADD = -lgtest -lpthread -lrt

bt_profile_unittest_SOURCES = tests/bt_profile_unittest.cc tests/dbus_test.cc \
	server/cras_bt_profile.c
bt_profile_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server $(DBUS_CFLAGS)
bt_profile_unittest_LDADD = -lgtest -lpthread $(DBUS_LIBS)

card_config_unittest_SOURCES = tests/card_config_unittest.cc \
	server/config/cras_card_config.c
card_config_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) \
	-I$(top_srcdir)/src/common -I$(top_srcdir)/src/server \
	-I$(top_srcdir)/src/server/config
card_config_unittest_LDADD = -lgtest -liniparser -lpthread

checksum_unittest_SOURCES = tests/checksum_unittest.cc common/cras_checksum.c
checksum_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common
checksum_unittest_LDADD = -lgtest -lpthread

cras_client_unittest_SOURCES = tests/cras_client_unittest.cc \
	common/cras_config.c common/cras_util.c
cras_client_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/libcras
cras_client_unittest_LDADD = -lgtest -lpthread -lspeexdsp

cras_tm_unittest_SOURCES = tests/cras_tm_unittest.cc server/cras_tm.c
cras_tm_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	 -I$(top_srcdir)/src/server
cras_tm_unittest_LDADD = -lgtest -lpthread

dev_stream_unittest_SOURCES = tests/dev_stream_unittest.cc \
	server/dev_stream.c
dev_stream_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) \
	-I$(top_srcdir)/src/common -I$(top_srcdir)/src/server
dev_stream_unittest_LDADD = -lgtest -liniparser -lpthread

device_blacklist_unittest_SOURCES = tests/device_blacklist_unittest.cc \
	server/config/cras_device_blacklist.c
device_blacklist_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) \
	-I$(top_srcdir)/src/common -I$(top_srcdir)/src/server \
	-I$(top_srcdir)/src/server/config
device_blacklist_unittest_LDADD = -lgtest -liniparser -lpthread

dsp_core_unittest_SOURCES = tests/dsp_core_unittest.cc dsp/eq.c dsp/eq2.c \
	dsp/biquad.c dsp/dsp_util.c dsp/crossover.c dsp/crossover2.c dsp/drc.c \
	dsp/drc_kernel.c dsp/drc_math.c
dsp_core_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/dsp
dsp_core_unittest_LDADD = -lgtest -lpthread

dsp_ini_unittest_SOURCES = tests/dsp_ini_unittest.cc \
	server/cras_dsp_ini.c server/cras_expr.c common/dumper.c
dsp_ini_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server
dsp_ini_unittest_LDADD = -lgtest -liniparser -lpthread

dsp_pipeline_unittest_SOURCES = tests/cras_dsp_pipeline_unittest.cc \
	server/cras_dsp_ini.c server/cras_expr.c server/cras_dsp_pipeline.c \
	common/dumper.c dsp/dsp_util.c
dsp_pipeline_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server -I$(top_srcdir)/src/dsp
dsp_pipeline_unittest_LDADD = -lgtest -lrt -liniparser -lpthread

dsp_unittest_SOURCES = tests/dsp_unittest.cc \
	server/cras_dsp.c server/cras_dsp_ini.c server/cras_dsp_pipeline.c \
	server/cras_expr.c common/dumper.c dsp/dsp_util.c \
	dsp/tests/dsp_test_util.c
dsp_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server -I$(top_srcdir)/src/dsp
dsp_unittest_LDADD = -lgtest -lrt -liniparser -lpthread

dumper_unittest_SOURCES = tests/dumper_unittest.cc common/dumper.c
dumper_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common
dumper_unittest_LDADD = -lgtest -lpthread

edid_utils_unittest_SOURCES = tests/edid_utils_unittest.cc common/edid_utils.c
edid_utils_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common
edid_utils_unittest_LDADD = -lgtest -lpthread

expr_unittest_SOURCES = tests/expr_unittest.cc server/cras_expr.c common/dumper.c
expr_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server
expr_unittest_LDADD = -lgtest -lpthread

fmt_conv_unittest_SOURCES = tests/fmt_conv_unittest.cc common/cras_fmt_conv.c
fmt_conv_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	 -I$(top_srcdir)/src/server
fmt_conv_unittest_LDADD = -lasound -lspeexdsp -lgtest -lpthread

hfp_info_unittest_SOURCES = tests/hfp_info_unittest.cc
hfp_info_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server
hfp_info_unittest_LDADD = -lgtest -lpthread

hfp_iodev_unittest_SOURCES = tests/hfp_iodev_unittest.cc \
	server/cras_hfp_iodev.c
hfp_iodev_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server $(DBUS_CFLAGS)
hfp_iodev_unittest_LDADD = -lgtest -lpthread $(DBUS_LIBS)

hfp_slc_unittest_SOURCES = tests/hfp_slc_unittest.cc \
	server/cras_hfp_slc.c server/cras_telephony.c
hfp_slc_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server $(DBUS_CFLAGS)
hfp_slc_unittest_LDADD = -lgtest -lpthread $(DBUS_LIBS)

buffer_share_unittest_SOURCES = tests/buffer_share_unittest.cc \
	server/buffer_share.c
buffer_share_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) \
	-I$(top_srcdir)/src/common -I$(top_srcdir)/src/server
buffer_share_unittest_LDADD = -lgtest -liniparser -lpthread

iodev_list_unittest_SOURCES = tests/iodev_list_unittest.cc \
	server/cras_iodev_list.c
iodev_list_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	 -I$(top_srcdir)/src/server
iodev_list_unittest_LDADD = -lgtest -lpthread

loopback_iodev_unittest_SOURCES = tests/loopback_iodev_unittest.cc \
	server/cras_loopback_iodev.c
loopback_iodev_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) \
	-I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server
loopback_iodev_unittest_LDADD = -lgtest -lpthread

iodev_unittest_SOURCES = tests/iodev_unittest.cc \
	server/cras_iodev.c
iodev_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	 -I$(top_srcdir)/src/server
iodev_unittest_LDADD = -lgtest -lpthread

mix_unittest_SOURCES = tests/mix_unittest.cc server/cras_mix.c
mix_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	 -I$(top_srcdir)/src/server
mix_unittest_LDADD = -lgtest -lpthread

rate_estimator_unittest_SOURCES = tests/rate_estimator_unittest.cc server/rate_estimator.c
rate_estimator_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	 -I$(top_srcdir)/src/server
rate_estimator_unittest_LDADD = -lgtest -lpthread

rclient_unittest_SOURCES = tests/rclient_unittest.cc server/cras_rclient.c
rclient_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	 -I$(top_srcdir)/src/server
rclient_unittest_LDADD = -lgtest -lpthread

rstream_unittest_SOURCES = tests/rstream_unittest.cc server/cras_rstream.c
rstream_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	 -I$(top_srcdir)/src/server
rstream_unittest_LDADD = -lasound -lgtest -lpthread

shm_unittest_SOURCES = tests/shm_unittest.cc
shm_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common
shm_unittest_LDADD = -lgtest -lpthread

system_state_unittest_SOURCES = tests/system_state_unittest.cc \
	server/cras_system_state.c
system_state_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) \
	-I$(top_srcdir)/src/common -I$(top_srcdir)/src/server \
	-I$(top_srcdir)/src/server/config
system_state_unittest_LDADD = -lgtest -lrt -lpthread

util_unittest_SOURCES = tests/util_unittest.cc common/cras_util.c \
	common/cras_config.c
util_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common
util_unittest_LDADD = -lgtest -lpthread

volume_curve_unittest_SOURCES = tests/volume_curve_unittest.cc \
	server/cras_volume_curve.c
volume_curve_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) \
	-I$(top_srcdir)/src/common -I$(top_srcdir)/src/server
volume_curve_unittest_LDADD = -lgtest -lpthread
