xlator_LTLIBRARIES = io-batch.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance

io_batch_la_LDFLAGS = -module -avoid-version

io_batch_la_SOURCES = io-batch.c
io_batch_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = io-batch.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
