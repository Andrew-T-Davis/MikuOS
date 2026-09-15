NASM := nasm
NASM_ARG := -f bin

HOLY_MIKU_FILES_DIR = ./UserHolyMikuFilesDir/
HOLY_MIKU_FILES := $(shell find $(HOLY_MIKU_FILES_DIR) -type f)

bootloader.bin: bootloader.asm
	$(NASM) $(NASM_ARG) -o $@ $<

UserFiles.Bin: PackFiles.py $(HOLY_MIKU_FILES)
	python3 PackFiles.py $(HOLY_MIKU_FILES_DIR) UserFiles.Bin

MikuOS.img: bootloader.bin UserFiles.Bin
	rm -f MikuOS.img
	dd if=/dev/zero of=MikuOS.img bs=512 count=2048
	dd if=bootloader.bin of=MikuOS.img bs=512 conv=notrunc
	dd if=UserFiles.Bin of=MikuOS.img bs=512 seek=4 conv=notrunc

run: MikuOS.img
	qemu-system-x86_64 \
		-hda MikuOS.img \
		-boot order=c \
		-vga std \
		-accel tcg,thread=multi \
		-vnc :1 \
		-serial stdio

clear:
	rm -rf *.bin *.iso *.img

clean:
	rm -rf *.bin *.iso *.img

