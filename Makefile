
# Makefile для AOS (svzt@NeonEngine)

CC = gcc
CFLAGS = -m32 -std=gnu11 -ffreestanding -O2 -Wall -Wextra -nostdlib -fno-pie
AS = nasm
ASFLAGS = -f elf32
LD = ld
LDFLAGS = -m elf_i386 -T linker.ld

OBJS = kernel_asm.o kernel.o keyboard.o acpi.o

.PHONY: all run clean

all: myos.iso

kernel_asm.o: kernel.asm
	$(AS) $(ASFLAGS) kernel.asm -o kernel_asm.o

kernel.o: kernel.c acpi.h
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

keyboard.o: keyboard.c
	$(CC) $(CFLAGS) -c keyboard.c -o keyboard.o

acpi.o: acpi.c acpi.h
	$(CC) $(CFLAGS) -c acpi.c -o acpi.o

kernel.bin: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o kernel.bin

myos.iso: kernel.bin
	mkdir -p iso/boot/grub
	cp kernel.bin iso/boot/kernel.bin
	echo 'set timeout=3' > iso/boot/grub/grub.cfg
	echo 'set default=0' >> iso/boot/grub/grub.cfg
	echo 'insmod all_video' >> iso/boot/grub/grub.cfg
	echo 'set gfxmode=1024x768x32' >> iso/boot/grub/grub.cfg
	echo 'set gfxpayload=keep' >> iso/boot/grub/grub.cfg
	echo 'menuentry "AOS (svzt@NeonEngine)" {' >> iso/boot/grub/grub.cfg
	echo '    multiboot2 /boot/kernel.bin' >> iso/boot/grub/grub.cfg
	echo '    boot' >> iso/boot/grub/grub.cfg
	echo '}' >> iso/boot/grub/grub.cfg
	grub-mkrescue -o myos.iso iso

run: myos.iso
	qemu-system-x86_64 -machine q35 -m 512M \
		-cdrom "$(CURDIR)/myos.iso" \
		-drive file=disk.qcow2,format=qcow2,if=ide,index=0 \
		-boot order=d -serial stdio -vga std
clean:
	rm -rf *.o kernel.bin myos.iso iso
