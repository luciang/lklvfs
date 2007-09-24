CROSS=i586-mingw32msvc-
CC=$(CROSS)gcc
AS=$(CROSS)as
DLLTOOL=$(CROSS)dlltool
HERE=$(PWD)

LKL_SOURCE=../linux-2.6
LKL=lkl/vmlinux

OBJS=$(patsubst %.c,%.o,$(shell find $(1) -type f -name '*.c')) 
DEPS=$(patsubst %.c,.deps/%.d,$(shell find $(1) -type f -name '*.c')) 

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

lkl/vmlinux: lkl/.config
	cd $(LKL_SOURCE) && \
	$(MAKE) O=$(HERE)/lkl $@` ARCH=lkl CROSS_COMPILE=i586-mingw32msvc- \
		LKL_DRIVERS=$(HERE)/drivers/ \
		FILE_DISK=y \
		STDIO_CONSOLE=y FILE_DISK_MAJOR=42 \
		vmlinux

INC=include/asm include/asm-generic include/asm-i386 include/linux

CFLAGS=-Iinclude -D_WIN32_WINNT=0x0500 -DFILE_DISK_MAJOR=42

lib/%.a: lib/%.def
	$(DLLTOOL) --as=$(AS) -k --output-lib $@ --def $^

lklvfs.sys: $(INC) $(call OBJS,src) lib/libmingw-patch.a lkl/vmlinux
	i586-mingw32msvc-gcc -Wall -s lkl/vmlinux \
	$^ -Wl,--subsystem,native -Wl,--entry,_DriverEntry@8 \
	-nostartfiles -Llib -lmingw-patch -lntoskrnl -lhal -nostdlib \
	-shared -o $@ 

clean: 
	rm -f lklvfs.sys include/asm include/asm-i386 \
	include/asm-generic include/linux $(call OBJS,src) lib/*.a
	rm -rf .deps 


.deps/%.d: %.c
	mkdir -p .deps/$(dir $<)
	$(CC) $(CFLAGS) -MM -MT $(patsubst %.c,%.o,$<) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

include $(call DEPS,src)
