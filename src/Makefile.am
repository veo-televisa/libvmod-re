AM_CPPFLAGS = @VMOD_INCLUDES@ @PCRE_CFLAGS@

vmoddir = @VMOD_DIR@
vmod_LTLIBRARIES = libvmod_re.la

libvmod_re_la_LDFLAGS = -module -export-dynamic -avoid-version
libvmod_re_la_LIBADD = @PCRE_LIBS@
libvmod_re_la_SOURCES = \
	vcc_if.c \
	vcc_if.h \
	vmod_re.c

man3_MANS = vmod_re.3
dist_man_MANS =

vcc_if.c vcc_if.h: @VMODTOOL@ $(top_srcdir)/src/vmod_re.vcc
	@VMODTOOL@ $(top_srcdir)/src/vmod_re.vcc

VMOD_TESTS = tests/*.vtc
.PHONY: $(VMOD_TESTS)

tests/*.vtc:
	@VARNISHTEST@ -Dvarnishd=@VARNISHD@ -Dvmod_topbuild=$(abs_top_builddir) $@

check: $(VMOD_TESTS)

vmod_re.3: vmod_re.man.rst
if HAVE_RST2MAN
	${RST2MAN} vmod_re.man.rst $@
else
	@echo "========================================"
	@echo "You need rst2man installed to make dist"
	@echo "========================================"
	@false
endif

EXTRA_DIST = \
	vmod_re.vcc \
	$(VMOD_TESTS)

CLEANFILES = $(builddir)/vcc_if.c $(builddir)/vcc_if.h \
	$(builddir)/vmod_re.man.rst \
	$(builddir)/vmod_re.rst
