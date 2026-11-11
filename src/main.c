// SPDX-License-Identifier: GPL-2.0-only
/*
 * MacBookPro11,5 indexed-GMUX UEFI utility.
 *
 * Model/device guards and Haswell DDI preconditions are based on published
 * MacBookPro11,5 hardware research. Indexed GMUX transactions follow the
 * upstream Linux apple-gmux driver. This implementation is intentionally
 * limited to volatile display routing; it does not change NVRAM or GPU power.
 */
#include <efi.h>
#include <efilib.h>
#include <efipciio.h>

#define GMUX_BASE          0x700
#define GMUX_VALUE         0xc2
#define GMUX_READ          0xd0
#define GMUX_WRITE         0xd4
#define GMUX_VERSION       0x04
#define GMUX_DISPLAY       0x10
#define GMUX_DDC           0x28
#define GMUX_INTEL_DDC     1
#define GMUX_INTERNAL_PANEL 2

#define INTEL_VENDOR       0x8086
#define INTEL_DEVICE       0x0d26
#define DDI_BUF_CTL_A      0x64000
#define DDI_BUF_CTL_E      0x64400
#define DDI_ENABLE         (1U << 31)
#define DDI_A_4_LANES      (1U << 4)

typedef struct {
    UINT8 anchor[4];
    UINT8 checksum;
    UINT8 length;
    UINT8 major;
    UINT8 minor;
    UINT16 max_structure_size;
    UINT8 revision;
    UINT8 formatted[5];
    UINT8 intermediate_anchor[5];
    UINT8 intermediate_checksum;
    UINT16 table_length;
    UINT32 table_address;
    UINT16 structure_count;
    UINT8 bcd_revision;
} SMBIOS2_ENTRY;

typedef struct {
    UINT8 anchor[5];
    UINT8 checksum;
    UINT8 length;
    UINT8 major;
    UINT8 minor;
    UINT8 doc_revision;
    UINT8 revision;
    UINT8 reserved;
    UINT32 table_max_size;
    UINT64 table_address;
} SMBIOS3_ENTRY;

static EFI_GUID smbios2_guid =
    {0xeb9d2d31, 0x2d88, 0x11d3, {0x9a,0x16,0x00,0x90,0x27,0x3f,0xc1,0x4d}};
static EFI_GUID smbios3_guid =
    {0xf2fd1544, 0x9794, 0x4a2c, {0x99,0x2e,0xe5,0xbb,0xcf,0x20,0xe3,0x94}};

static EFI_PCI_IO_PROTOCOL *intel_gpu;
static UINT8 original_display;
static UINT8 original_ddc;
static UINT32 original_port_a;
static UINT32 original_port_e;
static BOOLEAN have_original;
static BOOLEAN lane_was_changed;

static inline UINT8 io_in8(UINT16 port)
{
    UINT8 value;
    __asm__ volatile("inb %w1,%0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline UINT32 io_in32(UINT16 port)
{
    UINT32 value;
    __asm__ volatile("inl %w1,%0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline VOID io_out8(UINT16 port, UINT8 value)
{
    __asm__ volatile("outb %0,%w1" : : "a"(value), "Nd"(port));
}

static BOOLEAN gmux_wait_ready(VOID)
{
    for (UINTN i = 0; i < 200; ++i) {
        if (!(io_in8(GMUX_BASE + GMUX_WRITE) & 1))
            return TRUE;
        (void)io_in8(GMUX_BASE + GMUX_READ);
        uefi_call_wrapper(BS->Stall, 1, 100);
    }
    return FALSE;
}

static BOOLEAN gmux_wait_complete(VOID)
{
    for (UINTN i = 0; i < 200; ++i) {
        if (io_in8(GMUX_BASE + GMUX_WRITE) & 1) {
            (void)io_in8(GMUX_BASE + GMUX_READ);
            return TRUE;
        }
        uefi_call_wrapper(BS->Stall, 1, 100);
    }
    return FALSE;
}

static BOOLEAN gmux_read8(UINT8 reg, UINT8 *value)
{
    if (!gmux_wait_ready())
        return FALSE;
    io_out8(GMUX_BASE + GMUX_READ, reg);
    if (!gmux_wait_complete())
        return FALSE;
    *value = io_in8(GMUX_BASE + GMUX_VALUE);
    return TRUE;
}

static BOOLEAN gmux_read32(UINT8 reg, UINT32 *value)
{
    if (!gmux_wait_ready())
        return FALSE;
    io_out8(GMUX_BASE + GMUX_READ, reg);
    if (!gmux_wait_complete())
        return FALSE;
    *value = io_in32(GMUX_BASE + GMUX_VALUE);
    return TRUE;
}

static BOOLEAN gmux_write8(UINT8 reg, UINT8 value)
{
    io_out8(GMUX_BASE + GMUX_VALUE, value);
    if (!gmux_wait_ready())
        return FALSE;
    io_out8(GMUX_BASE + GMUX_WRITE, reg);
    return gmux_wait_complete();
}

/* Resolve SMBIOS Type 1 Product Name without assuming a particular table rev. */
static BOOLEAN model_is_mbp115(VOID)
{
    VOID *table = NULL;
    UINT8 *start = NULL;
    UINTN length = 0;
    SMBIOS2_ENTRY *v2 = NULL;
    SMBIOS3_ENTRY *v3 = NULL;

    if (!EFI_ERROR(LibGetSystemConfigurationTable(&smbios2_guid, &table)) && table) {
        v2 = (SMBIOS2_ENTRY *)table;
        start = (UINT8 *)(UINTN)v2->table_address;
        length = v2->table_length;
    } else if (!EFI_ERROR(LibGetSystemConfigurationTable(&smbios3_guid, &table)) && table) {
        v3 = (SMBIOS3_ENTRY *)table;
        start = (UINT8 *)(UINTN)v3->table_address;
        length = v3->table_max_size;
    } else {
        return FALSE;
    }

    if (!start || !length || length > 0x100000)
        return FALSE;

    UINT8 *p = start;
    UINT8 *end = start + length;
    while (p + 4 <= end) {
        UINT8 type = p[0];
        UINT8 formatted_length = p[1];
        if (formatted_length < 4 || p + formatted_length > end)
            return FALSE;

        UINT8 *strings = p + formatted_length;
        UINT8 *next = strings;
        while (next + 1 < end && (next[0] || next[1]))
            ++next;
        if (next + 1 >= end)
            return FALSE;

        if (type == 1 && formatted_length >= 6) {
            UINT8 product_index = p[5];
            if (!product_index)
                return FALSE;
            UINT8 *s = strings;
            for (UINTN idx = 1; idx < product_index && s < next; ++idx) {
                while (s < next && *s)
                    ++s;
                ++s;
            }
            static const CHAR8 expected[] = "MacBookPro11,5";
            if (s + sizeof(expected) <= next + 1 &&
                CompareMem(s, expected, sizeof(expected)) == 0)
                return TRUE;
            return FALSE;
        }

        if (type == 127)
            break;
        p = next + 2;
    }
    return FALSE;
}

static EFI_STATUS locate_intel_gpu(EFI_PCI_IO_PROTOCOL **found)
{
    EFI_HANDLE *handles = NULL;
    UINTN count = 0;
    EFI_STATUS status = uefi_call_wrapper(BS->LocateHandleBuffer, 5,
        ByProtocol, &PciIoProtocol, NULL, &count, &handles);
    if (EFI_ERROR(status))
        return status;

    status = EFI_NOT_FOUND;
    for (UINTN i = 0; i < count; ++i) {
        EFI_PCI_IO_PROTOCOL *pci = NULL;
        UINTN segment = 0, bus = 0, device = 0, function = 0;
        UINT32 identity = 0;

        EFI_STATUS s = uefi_call_wrapper(BS->HandleProtocol, 3,
            handles[i], &PciIoProtocol, (VOID **)&pci);
        if (EFI_ERROR(s))
            continue;
        s = uefi_call_wrapper(pci->GetLocation, 5,
            pci, &segment, &bus, &device, &function);
        if (EFI_ERROR(s) || segment || bus || device != 2 || function)
            continue;
        s = uefi_call_wrapper(pci->Pci.Read, 5, pci,
            EfiPciIoWidthUint32, 0, 1, &identity);
        if (EFI_ERROR(s) || identity != ((INTEL_DEVICE << 16) | INTEL_VENDOR))
            continue;
        *found = pci;
        status = EFI_SUCCESS;
        break;
    }
    FreePool(handles);
    return status;
}

static EFI_STATUS read_ddi(UINT64 offset, UINT32 *value)
{
    return uefi_call_wrapper(intel_gpu->Mem.Read, 6, intel_gpu,
        EfiPciIoWidthUint32, 0, offset, 1, value);
}

static EFI_STATUS write_ddi_a(UINT32 value)
{
    return uefi_call_wrapper(intel_gpu->Mem.Write, 6, intel_gpu,
        EfiPciIoWidthUint32, 0, DDI_BUF_CTL_A, 1, &value);
}

static EFI_STATUS capture_hardware_state(VOID)
{
    UINT32 version = 0;
    EFI_STATUS status;

    if (!gmux_read32(GMUX_VERSION, &version) ||
        (version & 0xffffff00U) != 0x04001400U) {
        Print(L"Unsupported or unreadable indexed GMUX version: 0x%08x\r\n", version);
        return EFI_UNSUPPORTED;
    }
    if (!gmux_read8(GMUX_DISPLAY, &original_display) ||
        !gmux_read8(GMUX_DDC, &original_ddc)) {
        Print(L"Could not read GMUX selectors.\r\n");
        return EFI_DEVICE_ERROR;
    }
    if ((original_display != 2 && original_display != 3) ||
        (original_ddc != 1 && original_ddc != 2)) {
        Print(L"Unexpected initial GMUX selector values. Refusing to write.\r\n");
        return EFI_UNSUPPORTED;
    }

    status = locate_intel_gpu(&intel_gpu);
    if (EFI_ERROR(status)) {
        Print(L"Intel Iris Pro 5200 (8086:0d26) not found at 0000:00:02.0.\r\n");
        return status;
    }
    status = read_ddi(DDI_BUF_CTL_A, &original_port_a);
    if (EFI_ERROR(status))
        return status;
    status = read_ddi(DDI_BUF_CTL_E, &original_port_e);
    if (EFI_ERROR(status))
        return status;
    if (original_port_a == 0xffffffffU || original_port_e == 0xffffffffU) {
        Print(L"Intel DDI registers are not readable.\r\n");
        return EFI_UNSUPPORTED;
    }
    have_original = TRUE;

    Print(L"MacBookPro11,5 verified. GMUX signature: 0x%08x\r\n", version);
    Print(L"GMUX display=%d DDC=%d\r\n", original_display, original_ddc);
    Print(L"DDI A=0x%08x E=0x%08x\r\n", original_port_a, original_port_e);
    return EFI_SUCCESS;
}

static EFI_STATUS restore_state(VOID)
{
    EFI_STATUS status = EFI_SUCCESS;
    UINT32 current = 0;

    if (!have_original)
        return EFI_NOT_READY;

    if (lane_was_changed) {
        status = read_ddi(DDI_BUF_CTL_A, &current);
        if (EFI_ERROR(status) || (current & DDI_ENABLE)) {
            Print(L"Cannot safely restore lane allocation: Port A is enabled or unreadable.\r\n");
            return EFI_ACCESS_DENIED;
        }
        status = write_ddi_a(original_port_a);
        if (EFI_ERROR(status))
            return status;
        status = read_ddi(DDI_BUF_CTL_A, &current);
        if (EFI_ERROR(status) || current != original_port_a)
            return EFI_DEVICE_ERROR;
        lane_was_changed = FALSE;
    }

    if (!gmux_write8(GMUX_DDC, original_ddc) ||
        !gmux_write8(GMUX_DISPLAY, original_display))
        return EFI_DEVICE_ERROR;

    UINT8 ddc = 0, display = 0;
    if (!gmux_read8(GMUX_DDC, &ddc) ||
        !gmux_read8(GMUX_DISPLAY, &display) ||
        ddc != original_ddc || display != original_display)
        return EFI_DEVICE_ERROR;

    Print(L"Original GMUX selectors and Port A lane allocation restored.\r\n");
    return EFI_SUCCESS;
}

static EFI_STATUS select_intel(VOID)
{
    EFI_STATUS status;
    UINT32 a = 0, e = 0;
    UINT8 ddc = 0, display = 0;

    status = read_ddi(DDI_BUF_CTL_A, &a);
    if (EFI_ERROR(status))
        return status;
    status = read_ddi(DDI_BUF_CTL_E, &e);
    if (EFI_ERROR(status))
        return status;

    /* Haswell Port A/E share lanes. Never reallocate while either port is enabled. */
    if ((a & DDI_ENABLE) || (e & DDI_ENABLE)) {
        Print(L"Refusing lane change: Haswell DDI Port A or E is enabled.\r\n");
        return EFI_ACCESS_DENIED;
    }

    if (!(a & DDI_A_4_LANES)) {
        status = write_ddi_a(a | DDI_A_4_LANES);
        if (EFI_ERROR(status))
            return status;
        lane_was_changed = TRUE;
        status = read_ddi(DDI_BUF_CTL_A, &a);
        if (EFI_ERROR(status) || a != (original_port_a | DDI_A_4_LANES)) {
            Print(L"Port A lane allocation readback failed.\r\n");
            (void)restore_state();
            return EFI_DEVICE_ERROR;
        }
    }

    if (!gmux_write8(GMUX_DDC, GMUX_INTEL_DDC) ||
        !gmux_write8(GMUX_DISPLAY, GMUX_INTERNAL_PANEL)) {
        (void)restore_state();
        return EFI_DEVICE_ERROR;
    }

    if (!gmux_read8(GMUX_DDC, &ddc) ||
        !gmux_read8(GMUX_DISPLAY, &display) ||
        ddc != GMUX_INTEL_DDC || display != GMUX_INTERNAL_PANEL) {
        Print(L"GMUX readback mismatch; attempting restoration.\r\n");
        (void)restore_state();
        return EFI_DEVICE_ERROR;
    }

    Print(L"Intel DDC + internal panel selected; readback verified.\r\n");
    Print(L"Returning to the caller. The firmware GOP may no longer be visible.\r\n");
    return EFI_SUCCESS;
}

static EFI_INPUT_KEY wait_for_key(EFI_SYSTEM_TABLE *system_table)
{
    EFI_INPUT_KEY key;
    while (uefi_call_wrapper(system_table->ConIn->ReadKeyStroke, 2,
                             system_table->ConIn, &key) == EFI_NOT_READY)
        uefi_call_wrapper(BS->Stall, 1, 10000);
    return key;
}

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system_table)
{
    EFI_STATUS status;
    EFI_INPUT_KEY key;

    InitializeLib(image, system_table);
    Print(L"\r\nMacBookPro11,5 Indexed GMUX UEFI\r\n");
    Print(L"--------------------------------\r\n");

    if (!model_is_mbp115()) {
        Print(L"Wrong or unreadable SMBIOS model. No hardware changes made.\r\n");
        return EFI_UNSUPPORTED;
    }

    status = capture_hardware_state();
    if (EFI_ERROR(status)) {
        Print(L"Hardware validation failed: %r. No mux changes made.\r\n", status);
        return status;
    }

    Print(L"\r\n[I] Select Intel internal panel and return to caller\r\n");
    Print(L"[R] Restore startup GMUX/lane state and return\r\n");
    Print(L"[Esc] Exit without changes\r\n");
    Print(L"WARNING: selecting Intel can make the current GOP/boot picker invisible.\r\n");
    Print(L"Choose: ");

    for (;;) {
        key = wait_for_key(system_table);
        if (key.ScanCode == SCAN_ESC || key.UnicodeChar == 0x1b) {
            Print(L"Exit without changes.\r\n");
            return EFI_SUCCESS;
        }
        if (key.UnicodeChar == L'i' || key.UnicodeChar == L'I') {
            Print(L"I\r\n");
            status = select_intel();
            Print(L"Result: %r\r\n", status);
            return status;
        }
        if (key.UnicodeChar == L'r' || key.UnicodeChar == L'R') {
            Print(L"R\r\n");
            status = restore_state();
            Print(L"Result: %r\r\n", status);
            return status;
        }
    }
}
