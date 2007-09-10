driver unload filedisk
driver unload lklvfs
@cd /src
build -cZ
@cd ..
driver load filedisk\filedisk.sys
driver load sys\i386\lklvfs.sys
@cd filedisk
filedisk.exe /mount 0 disk f:
@cd ..