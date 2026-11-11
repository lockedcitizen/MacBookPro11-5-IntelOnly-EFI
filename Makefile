ARCH := x86_64
TARGET := build/MacBookPro11-5-GMUX.efi
OBJECT := build/main.o
EFI_INC := /usr/include/efi
EFI_LIB := /usr/lib

CFLAGS := -I$(EFI_INC) -I$(EFI_INC)/$(ARCH) -DGNU_EFI_USE_MS_ABI \
          -fpic -ffreestanding -fno-stack-protector -fno-stack-check \
          -fshort-wchar -mno-red-zone -maccumulate-outgoing-args \
          -Wall -Wextra -Werror -O2 -D$(ARCH)
LDFLAGS := -nostdlib -znocombreloc -T $(EFI_LIB)/elf_$(ARCH)_efi.lds \
           -shared -Bsymbolic

.PHONY: all clean
all: $(TARGET)

build:
	mkdir -p build

$(OBJECT): src/main.c | build
	gcc $(CFLAGS) -c $< -o $@

build/GMUX.so: $(OBJECT)
	ld $(LDFLAGS) $(EFI_LIB)/crt0-efi-$(ARCH).o $< \
	   -L$(EFI_LIB) -lefi -lgnuefi -o $@

$(TARGET): build/GMUX.so
	objcopy -j .text -j .data -j .rodata -j .dynamic -j .dynsym \
	        -j .dynstr -j .rel -j .rela -j .reloc \
	        -O pei-x86-64 --subsystem=10 $< $@
	test -z "$$(nm -u build/GMUX.so)"
	file $@
	sha256sum $@

clean:
	rm -rf build
