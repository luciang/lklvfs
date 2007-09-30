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

INC=include/asm include/asm-generic include/asm-i386 include/linux include/drivers

all: lklvfs.sys

include/asm:
	-mkdir `dirname $@`
	ln -s $(LKL_SOURCE)/include/asm-lkl include/asm

include/asm-i386:
	-mkdir `dirname $@`
	ln -s $(LKL_SOURCE)/include/asm-i386 include/asm-i386

include/asm-generic:
	-mkdir `dirname $@`
	ln -s $(LKL_SOURCE)/include/asm-generic include/asm-generic

include/linux:
	-mkdir `dirname $@`
	ln -s $(LKL_SOURCE)/include/linux include/linux

lkl/.config: $(LKL_SOURCE)
	-mkdir `dirname $@`
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
	rm -f  include/asm include/asm-i386 \
	include/asm-generic include/linux $(call OBJS,src) lib/*.a
	rm -fd lkl

TAGS: $(call SRCS,src) $(call SRCS,drivers) Makefile include/*.h
	cd $(LKL_SOURCE) && \
	$(MAKE) O=$(HERE)/lkl ARCH=lkl CROSS_COMPILE=i586-mingw32msvc- TAGS
	etags -f TAGS.tmp $^
	cat lkl/TAGS TAGS.tmp > TAGS
	rm TAGS.tmp

.deps/%.d: %.c
	mkdir -p .deps/$(dir $<)
	$(CC) $(CFLAGS) -MM -MT $(patsubst %.c,%.o,$<) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

include $(call DEPS,src)
