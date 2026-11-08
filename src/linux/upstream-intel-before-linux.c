// SPDX-License-Identifier: GPL-2.0-only
/* MacBookPro11,5-specific, volatile Intel panel selection followed by UKI boot.
 * Indexed gmux transactions and switch values follow Linux apple-gmux.c.
 * Apple Set OS protocol follows 0xbb/apple_set_os.efi (MIT).
 * No NVRAM writes, GPU power changes, or external-display mux changes.
 */
#include <efi.h>
#include <efilib.h>
#include <efipciio.h>

#define BASE 0x700
#define VALUE 0xc2
#define READ 0xd0
#define WRITE 0xd4
#define DISPLAY 0x10
#define DDC 0x28
#define DDI_BUF_CTL_A 0x64000
#define DDI_BUF_CTL_E 0x64400
#define DDI_ENABLE (1U << 31)
#define DDI_A_4_LANES (1U << 4)

static EFI_FILE_HANDLE log_root;
/* Append/flush/close each checkpoint on the ESP before Linux starts. */
static VOID checkpoint(char *line) {
    EFI_FILE_HANDLE file = NULL;
    UINTN size = 0;
    while (line[size]) ++size;
    if (!log_root) return;
    EFI_STATUS s = uefi_call_wrapper(log_root->Open, 5, log_root, &file,
        L"\\EFI\\Omarchy\\intel-before-linux.log",
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
    if (EFI_ERROR(s)) return;
    s = uefi_call_wrapper(file->SetPosition, 2, file, (UINT64)-1);
    if (!EFI_ERROR(s)) {
        s = uefi_call_wrapper(file->Write, 3, file, &size, line);
        if (!EFI_ERROR(s)) uefi_call_wrapper(file->Flush, 1, file);
    }
    uefi_call_wrapper(file->Close, 1, file);
}
static VOID log_status(EFI_STATUS status) {
    char line[] = "EFI status 0x0000000000000000\r\n";
    const char hex[] = "0123456789abcdef";
    for (UINTN i = 0; i < 16; ++i)
        line[13 + i] = hex[(status >> ((15-i)*4)) & 15];
    checkpoint(line);
}
static VOID log_register(char *label, UINT32 value) {
    char line[] = "0x00000000\r\n";
    const char hex[] = "0123456789abcdef";
    checkpoint(label);
    for (UINTN i = 0; i < 8; ++i)
        line[2+i] = hex[(value >> ((7-i)*4)) & 15];
    checkpoint(line);
}

static EFI_STATUS find_intel_gpu(EFI_PCI_IO_PROTOCOL **gpu) {
    EFI_HANDLE *handles = NULL;
    UINTN count = 0;
    EFI_STATUS s = uefi_call_wrapper(BS->LocateHandleBuffer, 5, ByProtocol,
        &PciIoProtocol, NULL, &count, &handles);
    if (EFI_ERROR(s)) return s;
    s = EFI_NOT_FOUND;
    for (UINTN i = 0; i < count; ++i) {
        EFI_PCI_IO_PROTOCOL *pci = NULL;
        UINTN segment, bus, device, function;
        UINT32 identity = 0;
        EFI_STATUS t = uefi_call_wrapper(BS->HandleProtocol, 3,
            handles[i], &PciIoProtocol, (VOID **)&pci);
        if (EFI_ERROR(t)) continue;
        t = uefi_call_wrapper(pci->GetLocation, 5, pci, &segment, &bus, &device, &function);
        if (EFI_ERROR(t) || segment || bus || device != 2 || function) continue;
        t = uefi_call_wrapper(pci->Pci.Read, 5, pci, EfiPciIoWidthUint32, 0, 1, &identity);
        if (EFI_ERROR(t) || identity != 0x0d268086) continue;
        *gpu = pci;
        s = EFI_SUCCESS;
        break;
    }
    FreePool(handles);
    return s;
}
static EFI_STATUS read_ddi(EFI_PCI_IO_PROTOCOL *gpu, UINT64 offset, UINT32 *value) {
    return uefi_call_wrapper(gpu->Mem.Read, 6, gpu, EfiPciIoWidthUint32, 0, offset, 1, value);
}
static EFI_STATUS write_ddi(EFI_PCI_IO_PROTOCOL *gpu, UINT32 value) {
    return uefi_call_wrapper(gpu->Mem.Write, 6, gpu, EfiPciIoWidthUint32, 0, DDI_BUF_CTL_A, 1, &value);
}

static inline UINT8 in8(UINT16 port) {
    UINT8 value;
    __asm__ volatile("inb %w1,%0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline VOID out8(UINT16 port, UINT8 value) {
    __asm__ volatile("outb %0,%w1" : : "a"(value), "Nd"(port));
}
static inline UINT32 in32(UINT16 port) {
    UINT32 value;
    __asm__ volatile("inl %w1,%0" : "=a"(value) : "Nd"(port));
    return value;
}
static BOOLEAN ready(VOID) {
    for (UINTN i = 0; i < 200; ++i) {
        if (!(in8(BASE + WRITE) & 1)) return TRUE;
        (void)in8(BASE + READ);
        uefi_call_wrapper(BS->Stall, 1, 100);
    }
    return FALSE;
}
static BOOLEAN complete(VOID) {
    for (UINTN i = 0; i < 200; ++i) {
        if (in8(BASE + WRITE) & 1) {
            (void)in8(BASE + READ);
            return TRUE;
        }
        uefi_call_wrapper(BS->Stall, 1, 100);
    }
    return FALSE;
}
static BOOLEAN read8(UINT8 port, UINT8 *value) {
    if (!ready()) return FALSE;
    out8(BASE + READ, port);
    if (!complete()) return FALSE;
    *value = in8(BASE + VALUE);
    return TRUE;
}
static BOOLEAN write8(UINT8 port, UINT8 value) {
    out8(BASE + VALUE, value);
    if (!ready()) return FALSE;
    out8(BASE + WRITE, port);
    return complete();
}
static BOOLEAN read32(UINT8 port, UINT32 *value) {
    if (!ready()) return FALSE;
    out8(BASE + READ, port);
    if (!complete()) return FALSE;
    *value = in32(BASE + VALUE);
    return TRUE;
}

/* Bounded SMBIOS walk: refuse to touch I/O on any other product. */
static BOOLEAN correct_model(VOID) {
    SMBIOS_TABLE_ENTRY_POINT *table = NULL;
    SMBIOS_TABLE_3_0_ENTRY_POINT *table3 = NULL;
    UINT8 *p, *end;
    if (!EFI_ERROR(LibGetSystemConfigurationTable(&SMBIOSTableGuid, (VOID **)&table))) {
        p = (UINT8 *)(UINTN)table->TableAddress;
        end = p + table->TableLength;
    } else if (!EFI_ERROR(LibGetSystemConfigurationTable(&SMBIOS3TableGuid, (VOID **)&table3))) {
        p = (UINT8 *)(UINTN)table3->TableAddress;
        end = p + table3->TableMaximumSize;
    } else return FALSE;
    while (p + 4 <= end && p[1] >= 4 && p + p[1] < end) {
        UINT8 *strings = p + p[1], *next = strings;
        while (next + 1 < end && (next[0] || next[1])) ++next;
        if (next + 1 >= end) return FALSE;
        if (p[0] == 1 && p[1] >= 6 && p[5]) {
            UINT8 *s = strings;
            for (UINTN i = 1; i < p[5] && s < next; ++i) {
                while (s < next && *s) ++s;
                ++s;
            }
            const CHAR8 expected[] = "MacBookPro11,5";
            if (s + sizeof(expected) <= next + 1 &&
                CompareMem(s, expected, sizeof(expected)) == 0) return TRUE;
            return FALSE;
        }
        if (p[0] == 127) break;
        p = next + 2;
    }
    return FALSE;
}

typedef struct {
    UINT64 version;
    EFI_STATUS (EFIAPI *set_os_version)(CHAR8 *);
    EFI_STATUS (EFIAPI *set_os_vendor)(CHAR8 *);
} APPLE_SET_OS;
static EFI_GUID apple_guid = {0xc5c5da95,0x7d5c,0x45e6,{0xb2,0xf1,0x3f,0xd5,0x2b,0xb1,0x00,0x77}};

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *systab) {
    EFI_STATUS status;
    EFI_LOADED_IMAGE *self = NULL, *child = NULL;
    EFI_HANDLE child_handle = NULL;
    EFI_DEVICE_PATH *path = NULL;
    APPLE_SET_OS *apple = NULL;
    EFI_FILE_IO_INTERFACE *fs = NULL;
    EFI_PCI_IO_PROTOCOL *gpu = NULL;
    UINT32 version;
    UINT32 old_ddi = 0, ddi = 0, ddi_e = 0;
    UINT8 old_display = 0, old_ddc = 0, value;
    BOOLEAN changed = FALSE;
    BOOLEAN lane_changed = FALSE;
    InitializeLib(image, systab);
    Print(L"Intel-before-Linux helper: MacBookPro11,5 test\r\n");
    status = uefi_call_wrapper(BS->HandleProtocol, 3, image, &LoadedImageProtocol, (VOID **)&self);
    if (EFI_ERROR(status)) goto failure;
    status = uefi_call_wrapper(BS->HandleProtocol, 3, self->DeviceHandle, &FileSystemProtocol, (VOID **)&fs);
    if (EFI_ERROR(status)) goto failure;
    status = uefi_call_wrapper(fs->OpenVolume, 2, fs, &log_root);
    if (EFI_ERROR(status)) goto failure;
    checkpoint("\r\nSTART default helper (Intel four lanes; maintained Omarchy UKI)\r\n");
    if (!correct_model()) { status = EFI_UNSUPPORTED; goto failure; }
    checkpoint("SMBIOS model verified\r\n");
    if (!self->LoadOptions || self->LoadOptionsSize < 4 || self->LoadOptionsSize > 32768 ||
        self->LoadOptionsSize % sizeof(CHAR16) ||
        ((CHAR16 *)self->LoadOptions)[self->LoadOptionsSize / sizeof(CHAR16) - 1]) {
        status = EFI_INVALID_PARAMETER; goto failure;
    }
    /* Resolve and load Linux before touching the display mux. */
    path = FileDevicePath(self->DeviceHandle, L"\\EFI\\Linux\\omarchy_linux-omarchy.efi");
    if (!path) { status = EFI_OUT_OF_RESOURCES; goto failure; }
    status = uefi_call_wrapper(BS->LoadImage, 6, FALSE, image, path, NULL, 0, &child_handle);
    if (EFI_ERROR(status)) goto failure;
    checkpoint("Linux UKI loaded\r\n");
    status = uefi_call_wrapper(BS->HandleProtocol, 3, child_handle, &LoadedImageProtocol, (VOID **)&child);
    if (EFI_ERROR(status)) goto failure;
    child->LoadOptions = AllocatePool(self->LoadOptionsSize);
    if (!child->LoadOptions) { status = EFI_OUT_OF_RESOURCES; goto failure; }
    CopyMem(child->LoadOptions, self->LoadOptions, self->LoadOptionsSize);
    child->LoadOptionsSize = self->LoadOptionsSize;
    status = uefi_call_wrapper(BS->LocateProtocol, 3, &apple_guid, NULL, (VOID **)&apple);
    if (EFI_ERROR(status) || !apple || !apple->set_os_version) goto failure;
    status = uefi_call_wrapper(apple->set_os_version, 1, (CHAR8 *)"Mac OS X 10.9");
    if (EFI_ERROR(status)) goto failure;
    if (apple->version >= 2 && apple->set_os_vendor) {
        status = uefi_call_wrapper(apple->set_os_vendor, 1, (CHAR8 *)"Apple Inc.");
        if (EFI_ERROR(status)) goto failure;
    }
    checkpoint("Apple OS identity set\r\n");
    /* Indexed gmux packs major/minor/release into a single 32-bit register. */
    if (!read32(4, &version) || (version & 0xffffff00) != 0x04001400 ||
        !read8(DISPLAY, &old_display) || !read8(DDC, &old_ddc) ||
        (old_display != 2 && old_display != 3) || (old_ddc != 1 && old_ddc != 2)) {
        status = EFI_UNSUPPORTED; goto failure;
    }
    checkpoint("gmux version and initial selectors verified\r\n");
    status = find_intel_gpu(&gpu);
    if (EFI_ERROR(status)) goto failure;
    checkpoint("Intel 8086:0d26 at 0000:00:02.0 verified via EFI PCI I/O\r\n");
    status = read_ddi(gpu, DDI_BUF_CTL_A, &old_ddi);
    if (EFI_ERROR(status)) goto failure;
    status = read_ddi(gpu, DDI_BUF_CTL_E, &ddi_e);
    if (EFI_ERROR(status)) goto failure;
    log_register("DDI_BUF_CTL_A before: ", old_ddi);
    log_register("DDI_BUF_CTL_E before: ", ddi_e);
    /* Haswell A/E share lanes. Never reallocate an enabled port. */
    if (old_ddi == 0xffffffff || ddi_e == 0xffffffff ||
        (old_ddi & DDI_ENABLE) || (ddi_e & DDI_ENABLE)) {
        status = EFI_UNSUPPORTED; goto failure;
    }
    if (!(old_ddi & DDI_A_4_LANES)) {
        lane_changed = TRUE;
        status = write_ddi(gpu, old_ddi | DDI_A_4_LANES);
        if (EFI_ERROR(status)) goto failure;
    }
    status = read_ddi(gpu, DDI_BUF_CTL_A, &ddi);
    if (EFI_ERROR(status)) goto failure;
    log_register("DDI_BUF_CTL_A after: ", ddi);
    if (ddi != (old_ddi | DDI_A_4_LANES)) {
        status = EFI_DEVICE_ERROR; goto failure;
    }
    checkpoint("Intel four-lane allocation verified; ports remain disabled\r\n");
    Print(L"gmux 4.0.20: display=%d, DDC=%d; selecting Intel and starting Linux.\r\n", old_display, old_ddc);
    Print(L"The screen may be blank until the Intel Linux driver initializes.\r\n");
    uefi_call_wrapper(BS->Stall, 1, 2000000);
    changed = TRUE;
    if (!write8(DDC, 1)) { status = EFI_DEVICE_ERROR; goto failure; }
    checkpoint("DDC selected Intel\r\n");
    if (!write8(DISPLAY, 2)) { status = EFI_DEVICE_ERROR; goto failure; }
    checkpoint("Panel selected Intel\r\n");
    if (!read8(DDC, &value) || value != 1 ||
        !read8(DISPLAY, &value) || value != 2) {
        status = EFI_DEVICE_ERROR; goto failure;
    }
    checkpoint("Intel selectors read back successfully; calling StartImage\r\n");
    status = uefi_call_wrapper(BS->StartImage, 3, child_handle, NULL, NULL);
    /* A successfully booted OS never returns. Restore on any return. */
failure:
    checkpoint("Helper failure or Linux image returned\r\n");
    log_status(status);
    if (lane_changed) {
        EFI_STATUS restored = read_ddi(gpu, DDI_BUF_CTL_A, &ddi);
        if (!EFI_ERROR(restored) && !(ddi & DDI_ENABLE)) {
            restored = write_ddi(gpu, old_ddi);
            if (!EFI_ERROR(restored)) restored = read_ddi(gpu, DDI_BUF_CTL_A, &ddi);
            checkpoint(!EFI_ERROR(restored) && ddi == old_ddi ?
                "Original lane allocation restored\r\n" : "WARNING: lane restoration failed\r\n");
        } else checkpoint("WARNING: lane restoration skipped; port enabled or unreadable\r\n");
    }
    if (changed) {
        BOOLEAN restored_ddc = write8(DDC, old_ddc);
        BOOLEAN restored_display = write8(DISPLAY, old_display);
        checkpoint(restored_ddc && restored_display ?
            "Original mux selections restored\r\n" : "WARNING: mux restoration failed\r\n");
    }
    Print(L"Helper returned: %r. See EFI/Omarchy/intel-before-linux.log.\r\n", status);
    if (child && child->LoadOptions) { FreePool(child->LoadOptions); child->LoadOptions = NULL; }
    if (child_handle) uefi_call_wrapper(BS->UnloadImage, 1, child_handle);
    if (path) FreePool(path);
    if (log_root) uefi_call_wrapper(log_root->Close, 1, log_root);
    uefi_call_wrapper(BS->Stall, 1, 5000000);
    return EFI_ERROR(status) ? status : EFI_ABORTED;
}
