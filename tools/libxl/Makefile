#
# tools/libxl/Makefile
#

XEN_ROOT = ../..
include $(XEN_ROOT)/tools/Rules.mk

MAJOR = 1.0
MINOR = 0

XLUMAJOR = 1.0
XLUMINOR = 0

#CFLAGS += -Werror
CFLAGS += -I. -fPIC
CFLAGS += $(CFLAGS_libxenctrl) $(CFLAGS_libxenguest) $(CFLAGS_libxenstore)

LIBS = $(LDFLAGS_libxenctrl) $(LDFLAGS_libxenguest) $(LDFLAGS_libxenstore)

LIBXL_OBJS-y = osdeps.o
LIBXL_OBJS = flexarray.o libxl.o libxl_dom.o libxl_exec.o libxl_xshelp.o libxl_device.o libxl_internal.o xenguest.o libxl_utils.o $(LIBXL_OBJS-y)

AUTOINCS= libxlu_cfg_y.h libxlu_cfg_l.h
AUTOSRCS= libxlu_cfg_y.c libxlu_cfg_l.c
LIBXLU_OBJS = libxlu_cfg_y.o libxlu_cfg_l.o libxlu_cfg.o

CLIENTS = xl

.PHONY: all
all: $(CLIENTS) libxenlight.so libxenlight.a libxlutil.so libxlutil.a \
	$(AUTOSRCS) $(AUTOINCS)

$(AUTOINCS): $(AUTOSRCS)

#%_y.h: %_y.c
#%_l.h: %_l.c

$(LIBXLU_OBJS): $(AUTOINCS)

%.c: %.y
	$(BISON) --output=$@ $<

%.c: %.l
	$(FLEX) --header-file=$*.h --outfile=$@ $<

libxenlight.so: libxenlight.so.$(MAJOR)
	ln -sf $< $@

libxenlight.so.$(MAJOR): libxenlight.so.$(MAJOR).$(MINOR)
	ln -sf $< $@

libxenlight.so.$(MAJOR).$(MINOR): $(LIBXL_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,$(SONAME_LDFLAG) -Wl,libxenlight.so.$(MAJOR) $(SHLIB_CFLAGS) -o $@ $^

libxenlight.a: $(LIBXL_OBJS)
	$(AR) rcs libxenlight.a $^

libxlutil.so: libxlutil.so.$(XLUMAJOR)
	ln -sf $< $@

libxlutil.so.$(XLUMAJOR): libxlutil.so.$(XLUMAJOR).$(XLUMINOR)
	ln -sf $< $@

libxlutil.so.$(XLUMAJOR).$(XLUMINOR): $(LIBXLU_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,$(SONAME_LDFLAG) -Wl,libxlutil.so.$(XLUMAJOR) $(SHLIB_CFLAGS) -o $@ $^

libxlutil.a: $(LIBXLU_OBJS)
	$(AR) rcs libxlutil.a $^

xl.o: xl.c
	$(CC) $(CFLAGS) -c xl.c

$(CLIENTS): xl.o libxlutil.so libxenlight.so
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

.PHONY: install
install: all
	$(INSTALL_PROG) xl $(DESTDIR)$(SBINDIR)
	$(INSTALL_PROG) libxenlight.so.$(MAJOR).$(MINOR) $(DESTDIR)$(LIBDIR)
	ln -sf libxenlight.so.$(MAJOR).$(MINOR) $(DESTDIR)$(LIBDIR)/libxenlight.so.$(MAJOR)
	ln -sf libxenlight.so.$(MAJOR) $(DESTDIR)$(LIBDIR)/libxenlight.so
	$(INSTALL_DATA) libxenlight.a $(DESTDIR)$(LIBDIR)
	$(INSTALL_PROG) libxlutil.so.$(XLUMAJOR).$(XLUMINOR) $(DESTDIR)$(LIBDIR)
	ln -sf libxlutil.so.$(XLUMAJOR).$(XLUMINOR) $(DESTDIR)$(LIBDIR)/libxlutil.so.$(XLUMAJOR)
	ln -sf libxlutil.so.$(XLUMAJOR) $(DESTDIR)$(LIBDIR)/libxlutil.so
	$(INSTALL_DATA) libxlutil.a $(DESTDIR)$(LIBDIR)
	$(INSTALL_DATA) libxl.h $(DESTDIR)$(INCLUDEDIR)

.PHONY: clean
clean:
	$(RM) -f *.o *.so* *.a $(CLIENTS) $(DEPS)
#	$(RM) -f $(AUTOSRCS) $(AUTOINCS)

distclean: clean

-include $(DEPS)
