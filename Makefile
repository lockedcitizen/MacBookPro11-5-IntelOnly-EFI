ARCH := x86_64
EFI_INC ?= /usr/include/efi
EFI_ARCH_INC ?= /usr/include/efi/$(ARCH)
EFI_LIB ?= /usr/lib
EFI_CRT ?= $(EFI_LIB)/crt0-efi-$(ARCH).o
EFI_LDS ?= $(EFI_LIB)/elf_$(ARCH)_efi.lds

BUILD := build
OBJ := $(BUILD)/main.o
SO := $(BUILD)/IntelOnly.so
TARGET := $(BUILD)/IntelOnly.efi

CC := gcc
LD := ld
OBJCOPY := objcopy

CFLAGS := -I$(EFI_INC) -I$(EFI_ARCH_INC) -DGNU_EFI_USE_MS_ABI \
          -fpic -ffreestanding -fno-stack-protector -fno-stack-check \
          -fshort-wchar -mno-red-zone -maccumulate-outgoing-args \
          -Wall -Wextra -O2
LDFLAGS := -nostdlib -znocombreloc -T $(EFI_LDS) -shared -Bsymbolic

.PHONY: all clean
all: $(TARGET)

$(BUILD):
	mkdir -p $@

$(OBJ): src/main.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(SO): $(OBJ)
	$(LD) $(LDFLAGS) $(EFI_CRT) $< -L$(EFI_LIB) -lefi -lgnuefi -o $@

$(TARGET): $(SO)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
	  -j .rel -j .rela -j .reloc -O pei-x86-64 --subsystem=10 $< $@
	file $@
	sha256sum $@

clean:
	rm -rf $(BUILD)
