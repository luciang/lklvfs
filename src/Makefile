HERE=$(PWD)
INC=$(HERE)/../inc

LKLVFS_SRC =init.c fscontrol.c devcontrol.c dircontrol.c misc.c alloc.c create.c close.c cleanup.c fastIo.c geninfo.c cmcallbacks.c write.c read.c workqueue.c

lklvfs.sys: $(ASYS) $(INC) 
	i586-mingw32msvc-gcc -Wall -s -I$(INC) -D_WIN32_WINNT=0x0500 \
	$(LKLVFS_SRC) -Wl,--subsystem,native -Wl,--entry,_DriverEntry@8  -nostartfiles \
	-lntoskrnl -lhal -nostdlib -shared -o $@

clean: 
	rm lklvfs.sys
