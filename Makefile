CROSS=i586-mingw32msvc-
CC=$(CROSS)gcc
AS=$(CROSS)as
DLLTOOL=$(CROSS)dlltool

LKL_SOURCE=../linux-2.6
LKL=lkl/vmlinux

OBJS=$(patsubst %.c,%.o,$(shell find $(1) -type f -name '*.c')) 
DEPS=$(patsubst %.c,.deps/%.d,$(shell find $(1) -type f -name '*.c')) 

all: lklvfs.sys

include/asm: $(LKL_SOURCE)/include/asm-lkl
	-mkdir `dirname $@`
	ln -s $^ $@

include/asm-i386: $(LKL_SOURCE)/include/asm-i386
	-mkdir `dirname $@`
	ln -s $^ $@ 

include/asm-generic: $(LKL_SOURCE)/include/asm-generic
	-mkdir `dirname $@`
	ln -s $^ $@ 

include/linux: $(LKL_SOURCE)/include/linux
	-mkdir `dirname $@`
	ln -s  $^ $@

lkl/.config: $(LKL_SOURCE)
	-mkdir `dirname $@`
	cp $^/arch/lkl/defconfig $@

lkl/vmlinux: lkl/.config
	make -C $(LKL_SOURCE) O=$(PWD)/lkl ARCH=lkl 

CFLAGS=-Iinclude -D_WIN32_WINNT=0x0500 

lib/%.a: lib/%.def
	$(DLLTOOL) --as=$(AS) -k --output-lib $@ --def $^

lklvfs.sys: $(call OBJS,src) lib/libmingw-patch.a
	i586-mingw32msvc-gcc -Wall -s \
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
