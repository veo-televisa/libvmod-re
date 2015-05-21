ACLOCAL_AMFLAGS = -I m4 -I ${LIBVARNISHAPI_DATAROOTDIR}/aclocal

SUBDIRS = src

DISTCHECK_CONFIGURE_FLAGS = \
	VMOD_DIR='$${libdir}/varnish/vmods'

dist_man_MANS = src/vmod_re.3
MAINTAINERCLEANFILES = $(dist_man_MANS)
