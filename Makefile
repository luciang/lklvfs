CROSS=i586-mingw32msvc-
CC=$(CROSS)gcc
AS=$(CROSS)as
DLLTOOL=$(CROSS)dlltool
HERE=$(PWD)

LKL_SOURCE=$(HERE)/../linux-2.6
LKL=lkl/vmlinux

SRCS=$(shell find $(1) -type f -name '*.c')
OBJS=$(patsubst %.c,%.o,$(call SRCS,$(1)))
DEPS=$(patsubst %.c,.deps/%.d,$(call SRCS,$(1)))

INC=include/asm include/asm-generic include/x86 include/linux include/drivers
MKDIR=mkdir -p

all: lklvfs.sys

include/asm:
	-$(MKDIR) `dirname $@` || true
	ln -s $(LKL_SOURCE)/arch/lkl/include/asm include/asm

include/x86:
	-$(MKDIR) `dirname $@` || true
	ln -s $(LKL_SOURCE)/arch/x86 include/x86

include/asm-generic:
	-$(MKDIR) `dirname $@` || true
	ln -s $(LKL_SOURCE)/include/asm-generic include/asm-generic

include/linux:
	-$(MKDIR) `dirname $@` || true
	ln -s $(LKL_SOURCE)/include/linux include/linux

include/drivers:
	-$(MKDIR) `dirname $@` || true
	ln -s $(HERE)/drivers/ include/drivers

lkl/.config: $(LKL_SOURCE)
	-$(MKDIR) `dirname $@` || true
	cp $^/arch/lkl/defconfig $@

lkl/vmlinux: lkl/.config $(call SRCS,drivers) drivers/Makefile Makefile
	cd $(LKL_SOURCE) && \
	$(MAKE) O=$(HERE)/lkl ARCH=lkl CROSS_COMPILE=i586-mingw32msvc- \
		LKL_DRIVERS=$(HERE)/drivers/ \
		vmlinux

CFLAGS=-Iinclude -D_WIN32_WINNT=0x0500 -Wall -Wno-multichar

lib/%.a: lib/%.def
	$(DLLTOOL) --as=$(AS) -k --output-lib $@ --def $^

LKLVFS_SRC = $(call OBJS,src) lib/libmingw-patch.a lkl/vmlinux

lklvfs.sys: $(INC) $(LKLVFS_SRC)
	i586-mingw32msvc-gcc -Wall -s \
	$(LKLVFS_SRC) -Wl,--subsystem,native -Wl,--entry,_DriverEntry@8 \
	-nostartfiles -Llib -lmingw-patch -lntoskrnl -lhal -nostdlib \
	-shared -o $@

clean:
	rm -rf .deps lklvfs.sys
	rm -f drivers/*.o drivers/.*.o.cmd drivers/built-in.o
	rm -f src/*.o

clean-all: clean
	rm -f  include/asm include/x86 \
	include/asm-generic include/linux $(call OBJS,src) lib/*.a
	rm -fr lkl

TAGS: $(call SRCS,src) $(call SRCS,drivers) Makefile include/*.h
	cd $(LKL_SOURCE) && \
	$(MAKE) O=$(HERE)/lkl ARCH=lkl CROSS_COMPILE=i586-mingw32msvc- TAGS
	etags -f TAGS.tmp $^
	cat lkl/TAGS TAGS.tmp > TAGS
	rm TAGS.tmp

.deps/%.d: %.c
	$(MKDIR) .deps/$(dir $<)
	$(CC) $(CFLAGS) -MM -MT $(patsubst %.c,%.o,$<) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

include $(call DEPS,src)
