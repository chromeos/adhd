AUTOMAKE_OPTIONS = subdir-objects
ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS}

COMMON_CPPFLAGS = -O2 -Wall -Werror

bin_PROGRAMS = cras cras_test_client
cras_SOURCES = server/cras.c server/cras_iodev.c server/cras_iodev_list.c \
	server/cras_rclient.c server/cras_alsa_io.c server/cras_rstream.c \
	server/cras_mix.c common/cras_util.c common/cras_config.c \
	server/cras_alsa_helpers.c common/cras_audio_format.c \
	server/cras_system_settings.c  server/cras_volume_curve.c
cras_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_builddir)/src/common
cras_LDADD = -lpthread -lasound -lrt

lib_LTLIBRARIES = libcras.la
libcras_la_SOURCES = libcras/cras_client.c common/cras_util.c \
	common/cras_fmt_conv.c common/cras_config.c common/cras_audio_format.c
include_HEADERS = libcras/cras_client.h common/cras_types.h \
	common/cras_messages.h common/cras_config.h common/cras_fmt_conv.h \
	common/cras_shm.h common/utlist.h
libcras_la_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_builddir)/src/common \
	-I$(top_builddir)/src/libcras
libcras_la_LIBADD = -lpthread -lasound -lrt -lspeexdsp
libcras_la_LDFLAGS = -version-info 0:0:0

check_PROGRAMS = iodev_list_unittest rstream_unittest \
	fmt_conv_unittest shm_unittest mix_unittest alsa_io_unittest \
	rclient_unittest system_settings_unittest volume_curve_unittest
TESTS = $(check_PROGRAMS)

cras_test_client_SOURCES = tests/cras_test_client.c
cras_test_client_LDADD = libcras.la
cras_test_client_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_builddir)/src/libcras \
	-I$(top_builddir)/src/common

# unit tests
mix_unittest_SOURCES = tests/mix_unittest.cc server/cras_mix.c
mix_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_builddir)/src/common \
	 -I$(top_builddir)/src/server
mix_unittest_LDADD = -lgtest

iodev_list_unittest_SOURCES = tests/iodev_list_unittest.cc \
	server/cras_iodev_list.c server/cras_iodev.c
iodev_list_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_builddir)/src/common \
	 -I$(top_builddir)/src/server
iodev_list_unittest_LDADD = -lgtest

rclient_unittest_SOURCES = tests/rclient_unittest.cc server/cras_rclient.c
rclient_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_builddir)/src/common \
	 -I$(top_builddir)/src/server
rclient_unittest_LDADD = -lgtest

rstream_unittest_SOURCES = tests/rstream_unittest.cc server/cras_rstream.c
rstream_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_builddir)/src/common \
	 -I$(top_builddir)/src/server
rstream_unittest_LDADD = -lasound -lgtest

fmt_conv_unittest_SOURCES = tests/fmt_conv_unittest.cc common/cras_fmt_conv.c
fmt_conv_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_builddir)/src/common \
	 -I$(top_builddir)/src/server
fmt_conv_unittest_LDADD = -lasound -lspeexdsp -lgtest

shm_unittest_SOURCES = tests/shm_unittest.cc
shm_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_builddir)/src/common
shm_unittest_LDADD = -lgtest

alsa_io_unittest_SOURCES = tests/alsa_io_unittest.cc
alsa_io_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) -I$(top_builddir)/src/server \
	-I$(top_builddir)/src/common
alsa_io_unittest_LDADD = -lgtest -lpthread

system_settings_unittest_SOURCES = tests/system_settings_unittest.cc \
	server/cras_system_settings.c
system_settings_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) \
	-I$(top_builddir)/src/common -I$(top_builddir)/src/server
system_settings_unittest_LDADD = -lgtest

volume_curve_unittest_SOURCES = tests/volume_curve_unittest.cc \
	server/cras_volume_curve.c
volume_curve_unittest_CPPFLAGS = $(COMMON_CPPFLAGS) \
	-I$(top_builddir)/src/common -I$(top_builddir)/src/server
volume_curve_unittest_LDADD = -lgtest
