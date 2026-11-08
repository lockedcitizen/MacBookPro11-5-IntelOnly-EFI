ARCH := x86_64
EFI_INC ?= /usr/include/efi
EFI_ARCH_INC ?= /usr/include/efi/$(ARCH)
EFI_LIB ?= /usr/lib
EFI_CRT ?= $(EFI_LIB)/crt0-efi-$(ARCH).o
EFI_LDS ?= $(EFI_LIB)/elf_$(ARCH)_efi.lds

BUILD := build
CC := gcc
LD := ld
OBJCOPY := objcopy
CFLAGS := -I$(EFI_INC) -I$(EFI_ARCH_INC) -DGNU_EFI_USE_MS_ABI \
          -fpic -ffreestanding -fno-stack-protector -fno-stack-check \
          -fshort-wchar -mno-red-zone -maccumulate-outgoing-args \
          -Wall -Wextra -Werror -O2
LDFLAGS := -nostdlib -znocombreloc -T $(EFI_LDS) -shared -Bsymbolic
EFI_SECTIONS := -j .text -j .sdata -j .data -j .rodata -j .dynamic \
                -j .dynsym -j .dynstr -j .rel -j .rela -j .reloc

.PHONY: all clean
all: $(BUILD)/IntelOnly.efi $(BUILD)/IntelLinux.efi $(BUILD)/IntelPanel.efi

$(BUILD):
	mkdir -p $@

$(BUILD)/main.o: src/main.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/linux.o: src/linux/intel-before-linux.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/intel-panel.o: src/intel-panel.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/IntelOnly.so: $(BUILD)/main.o
	$(LD) $(LDFLAGS) $(EFI_CRT) $< -L$(EFI_LIB) -lefi -lgnuefi -o $@
	test -z "$$(nm -u $@)"

$(BUILD)/IntelLinux.so: $(BUILD)/linux.o
	$(LD) $(LDFLAGS) $(EFI_CRT) $< -L$(EFI_LIB) -lefi -lgnuefi -o $@
	test -z "$$(nm -u $@)"

$(BUILD)/IntelOnly.efi: $(BUILD)/IntelOnly.so
	$(OBJCOPY) $(EFI_SECTIONS) -O pei-x86-64 --subsystem=10 $< $@
	file $@
	sha256sum $@

$(BUILD)/IntelLinux.efi: $(BUILD)/IntelLinux.so
	$(OBJCOPY) $(EFI_SECTIONS) -O pei-x86-64 --subsystem=10 $< $@
	file $@
	sha256sum $@

$(BUILD)/IntelPanel.efi: $(BUILD)/IntelPanel.so
	$(OBJCOPY) $(EFI_SECTIONS) -O pei-x86-64 --subsystem=10 $< $@
	file $@
	sha256sum $@

clean:
	rm -rf $(BUILD)
