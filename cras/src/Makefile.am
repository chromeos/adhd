AUTOMAKE_OPTIONS = subdir-objects
ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS}

COMMON_CPPFLAGS = -O2 -Wall -Werror

bin_PROGRAMS = cras cras_test_client
cras_SOURCES = server/cras.c server/cras_iodev.c server/cras_iodev_list.c \
	server/cras_rclient.c server/cras_alsa_io.c server/cras_rstream.c \
	server/cras_mix.c common/cras_util.c common/cras_config.c \
	server/cras_alsa_helpers.c common/cras_audio_format.c \
	server/cras_system_state.c  server/cras_volume_curve.c \
	server/cras_alsa_mixer.c server/cras_alsa_card.c \
	server/cras_empty_iodev.c server/cras_server.c \
	server/cras_alsa_jack.c server/cras_dbus.c server/cras_udev.c \
	server/cras_bluetooth.c \
	server/config/cras_card_config.c server/cras_gpio_jack.c \
	server/config/cras_device_blacklist.c server/cras_alsa_ucm.c \
	server/cras_tm.c common/edid_utils.c common/cras_checksum.c \
	common/dumper.c server/cras_expr.c server/cras_dsp_ini.c \
	server/cras_dsp_mod_ladspa.c server/cras_dsp_mod_builtin.c \
	server/cras_dsp_pipeline.c server/cras_dsp.c \
	server/audio_thread.c
cras_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server -I$(top_srcdir)/src/server/config \
	$(DBUS_CFLAGS)
cras_LDADD = -lpthread -lasound -lrt -liniparser -ludev -ldl $(DBUS_LIBS)

lib_LTLIBRARIES = libcras.la
libcras_la_SOURCES = libcras/cras_client.c common/cras_util.c \
	common/cras_fmt_conv.c common/cras_config.c common/cras_audio_format.c \
	common/edid_utils.c
include_HEADERS = libcras/cras_client.h common/cras_types.h \
	common/cras_messages.h common/cras_config.h common/cras_fmt_conv.h \
	common/cras_shm.h common/cras_iodev_info.h common/utlist.h \
	common/cras_util.h common/edid_utils.h
libcras_la_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/libcras
libcras_la_LIBADD = -lpthread -lasound -lrt -lspeexdsp
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

check_PROGRAMS = iodev_list_unittest rstream_unittest \
	fmt_conv_unittest shm_unittest mix_unittest alsa_io_unittest \
	rclient_unittest system_state_unittest volume_curve_unittest \
	alsa_card_unittest alsa_mixer_unittest iodev_unittest \
	alsa_jack_unittest card_config_unittest device_blacklist_unittest \
	alsa_ucm_unittest edid_utils_unittest cras_tm_unittest \
	checksum_unittest bluetooth_unittest array_unittest dumper_unittest \
	expr_unittest dsp_ini_unittest dsp_pipeline_unittest dsp_unittest \
	audio_thread_unittest
TESTS = $(check_PROGRAMS)

cras_test_client_SOURCES = tests/cras_test_client.c
cras_test_client_LDADD = libcras.la
cras_test_client_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/libcras \
	-I$(top_srcdir)/src/common

# unit tests
array_unittest_SOURCES = tests/array_unittest.cc
array_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common
array_unittest_LDADD = -lgtest -lpthread

card_config_unittest_SOURCES = tests/card_config_unittest.cc \
	server/config/cras_card_config.c
card_config_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) \
	-I$(top_srcdir)/src/common -I$(top_srcdir)/src/server \
	-I$(top_srcdir)/src/server/config
card_config_unittest_LDADD = -lgtest -liniparser -lpthread

checksum_unittest_SOURCES = tests/checksum_unittest.cc common/cras_checksum.c
checksum_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common
checksum_unittest_LDADD = -lgtest -lpthread

cras_tm_unittest_SOURCES = tests/cras_tm_unittest.cc server/cras_tm.c
cras_tm_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	 -I$(top_srcdir)/src/server
cras_tm_unittest_LDADD = -lgtest -lpthread

device_blacklist_unittest_SOURCES = tests/device_blacklist_unittest.cc \
	server/config/cras_device_blacklist.c
device_blacklist_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) \
	-I$(top_srcdir)/src/common -I$(top_srcdir)/src/server \
	-I$(top_srcdir)/src/server/config
device_blacklist_unittest_LDADD = -lgtest -liniparser -lpthread

dumper_unittest_SOURCES = tests/dumper_unittest.cc common/dumper.c
dumper_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common
dumper_unittest_LDADD = -lgtest -lpthread

dsp_unittest_SOURCES = tests/dsp_unittest.cc \
	server/cras_dsp.c server/cras_dsp_ini.c server/cras_dsp_pipeline.c \
	server/cras_expr.c ../src/server/cras_dsp_mod_builtin.c \
	common/dumper.c
dsp_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server
dsp_unittest_LDADD = -lgtest -liniparser -lpthread

dsp_ini_unittest_SOURCES = tests/dsp_ini_unittest.cc \
	server/cras_dsp_ini.c server/cras_expr.c common/dumper.c
dsp_ini_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server
dsp_ini_unittest_LDADD = -lgtest -liniparser -lpthread

dsp_pipeline_unittest_SOURCES = tests/cras_dsp_pipeline_unittest.cc \
	server/cras_dsp_ini.c server/cras_expr.c server/cras_dsp_pipeline.c \
	common/dumper.c
dsp_pipeline_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server
dsp_pipeline_unittest_LDADD = -lgtest -liniparser -lpthread

expr_unittest_SOURCES = tests/expr_unittest.cc server/cras_expr.c common/dumper.c
expr_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server
expr_unittest_LDADD = -lgtest -lpthread

edid_utils_unittest_SOURCES = tests/edid_utils_unittest.cc common/edid_utils.c
edid_utils_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common
edid_utils_unittest_LDADD = -lgtest -lpthread

mix_unittest_SOURCES = tests/mix_unittest.cc server/cras_mix.c
mix_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	 -I$(top_srcdir)/src/server
mix_unittest_LDADD = -lgtest -lpthread

audio_thread_unittest_SOURCES = tests/audio_thread_unittest.cc \
	server/audio_thread.c
audio_thread_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) \
	-I$(top_builddir)/src/common -I$(top_builddir)/src/server
audio_thread_unittest_LDADD = -lgtest -lpthread

iodev_list_unittest_SOURCES = tests/iodev_list_unittest.cc \
	server/cras_iodev_list.c
iodev_list_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	 -I$(top_srcdir)/src/server
iodev_list_unittest_LDADD = -lgtest -lpthread

rclient_unittest_SOURCES = tests/rclient_unittest.cc server/cras_rclient.c
rclient_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	 -I$(top_srcdir)/src/server
rclient_unittest_LDADD = -lgtest -lpthread

rstream_unittest_SOURCES = tests/rstream_unittest.cc server/cras_rstream.c
rstream_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	 -I$(top_srcdir)/src/server
rstream_unittest_LDADD = -lasound -lgtest -lpthread

fmt_conv_unittest_SOURCES = tests/fmt_conv_unittest.cc common/cras_fmt_conv.c
fmt_conv_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	 -I$(top_srcdir)/src/server
fmt_conv_unittest_LDADD = -lasound -lspeexdsp -lgtest -lpthread

shm_unittest_SOURCES = tests/shm_unittest.cc
shm_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common
shm_unittest_LDADD = -lgtest -lpthread

alsa_io_unittest_SOURCES = tests/alsa_io_unittest.cc
alsa_io_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/server \
	-I$(top_srcdir)/src/common
alsa_io_unittest_LDADD = -lgtest -lpthread

system_state_unittest_SOURCES = tests/system_state_unittest.cc \
	server/cras_system_state.c
system_state_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) \
	-I$(top_srcdir)/src/common -I$(top_srcdir)/src/server \
	-I$(top_srcdir)/src/server/config
system_state_unittest_LDADD = -lgtest -lrt -lpthread

volume_curve_unittest_SOURCES = tests/volume_curve_unittest.cc \
	server/cras_volume_curve.c
volume_curve_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) \
	-I$(top_srcdir)/src/common -I$(top_srcdir)/src/server
volume_curve_unittest_LDADD = -lgtest -lpthread

alsa_card_unittest_SOURCES = tests/alsa_card_unittest.cc \
	server/cras_alsa_card.c
alsa_card_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) \
	-I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server \
	-I$(top_srcdir)/src/server/config
alsa_card_unittest_LDADD = -lgtest -lpthread

alsa_mixer_unittest_SOURCES = tests/alsa_mixer_unittest.cc \
	server/cras_alsa_mixer.c
alsa_mixer_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) \
	-I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server \
	-I$(top_srcdir)/src/server/config
alsa_mixer_unittest_LDADD = -lgtest -lpthread

alsa_ucm_unittest_SOURCES = tests/alsa_ucm_unittest.cc \
	server/cras_alsa_ucm.c
alsa_ucm_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) \
	-I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server \
	-I$(top_srcdir)/src/server/config
alsa_ucm_unittest_LDADD = -lgtest -lpthread
iodev_unittest_SOURCES = tests/iodev_unittest.cc \
	server/cras_iodev.c
iodev_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	 -I$(top_srcdir)/src/server
iodev_unittest_LDADD = -lgtest -lpthread

alsa_jack_unittest_SOURCES = tests/alsa_jack_unittest.cc \
	server/cras_alsa_jack.c
alsa_jack_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server
alsa_jack_unittest_LDADD = -lgtest -lpthread

bluetooth_unittest_SOURCES = tests/bluetooth_unittest.cc tests/dbus_test.cc \
	server/cras_bluetooth.c
bluetooth_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/server $(DBUS_CFLAGS)
bluetooth_unittest_LDADD = -lgtest -lpthread $(DBUS_LIBS)
