// SPDX-License-Identifier: GPL-2.0-only
/*
 * IntelPanel.efi — MacBookPro11,5 internal-panel GMUX handoff.
 *
 * Sets Apple Set OS identity to Mac OS X 10.9, selects Intel DDC and the
 * internal panel, assigns four DDI lanes to Haswell Port A, verifies readback,
 * then returns to the caller (OpenCore).
 * It deliberately does not load an operating system, write NVRAM, change
 * GPU power, or touch the external-display mux.
 *
 * Hardware register sequence is derived from the GPL-2.0-only
 * bbatchelder/macbookpro11-5-intel-efi project. See ATTRIBUTION.md.
 */
#include <efi.h>
#include <efilib.h>
#include <efipciio.h>

/* GNU-EFI does not expose these SMBIOS entry-point layouts. */
#pragma pack(push, 1)
typedef struct {
    CHAR8 Anchor[4]; UINT8 Checksum, Length, MajorVersion, MinorVersion;
    UINT16 MaximumStructureSize; UINT8 EntryPointRevision, FormattedArea[5];
    CHAR8 IntermediateAnchor[5]; UINT8 IntermediateChecksum;
    UINT16 TableLength; UINT32 TableAddress; UINT16 NumberOfStructures;
    UINT8 BcdRevision;
} SMBIOS_TABLE_ENTRY_POINT_LOCAL;
typedef struct {
    CHAR8 Anchor[5]; UINT8 Checksum, Length, MajorVersion, MinorVersion;
    UINT8 DocRevision, EntryPointRevision, Reserved;
    UINT32 TableMaximumSize; UINT64 TableAddress;
} SMBIOS_TABLE_3_0_ENTRY_POINT_LOCAL;
#pragma pack(pop)

#define GMUX_BASE       0x700
#define GMUX_VALUE      0xc2
#define GMUX_READ       0xd0
#define GMUX_WRITE      0xd4
#define GMUX_DISPLAY    0x10
#define GMUX_DDC        0x28
#define DDI_BUF_CTL_A   0x64000
#define DDI_BUF_CTL_E   0x64400
#define DDI_ENABLE      (1U << 31)
#define DDI_A_4_LANES   (1U << 4)

#ifdef INTEL_PANEL_DISABLE_AMD
#define AMD_VENDOR_ID           0x1002
#define AMD_DEVICE_ID           0x6821
#define GMUX_INTERRUPT_ENABLE   0x14
#define GMUX_INTERRUPT_STATUS   0x16
#define GMUX_DISCRETE_POWER     0x50
#define GMUX_POWER_EVENT        0x04
#endif

/* Apple Set OS protocol: advertise the requested Apple OS identity to firmware. */
typedef struct {
    UINT64 version;
    EFI_STATUS (EFIAPI *set_os_version)(CHAR8 *);
    EFI_STATUS (EFIAPI *set_os_vendor)(CHAR8 *);
} APPLE_SET_OS;
static EFI_GUID apple_set_os_guid =
    {0xc5c5da95,0x7d5c,0x45e6,{0xb2,0xf1,0x3f,0xd5,0x2b,0xb1,0x00,0x77}};

static inline UINT8 io_in8(UINT16 port) {
    UINT8 v; __asm__ volatile("inb %w1,%0" : "=a"(v) : "Nd"(port)); return v;
}
static inline VOID io_out8(UINT16 port, UINT8 v) {
    __asm__ volatile("outb %0,%w1" : : "a"(v), "Nd"(port));
}
static inline UINT32 io_in32(UINT16 port) {
    UINT32 v; __asm__ volatile("inl %w1,%0" : "=a"(v) : "Nd"(port)); return v;
}
static BOOLEAN gmux_ready(VOID) {
    for (UINTN i=0; i<200; ++i) {
        if (!(io_in8(GMUX_BASE+GMUX_WRITE)&1)) return TRUE;
        (void)io_in8(GMUX_BASE+GMUX_READ);
        uefi_call_wrapper(BS->Stall,1,100);
    }
    return FALSE;
}
static BOOLEAN gmux_complete(VOID) {
    for (UINTN i=0; i<200; ++i) {
        if (io_in8(GMUX_BASE+GMUX_WRITE)&1) {
            (void)io_in8(GMUX_BASE+GMUX_READ); return TRUE;
        }
        uefi_call_wrapper(BS->Stall,1,100);
    }
    return FALSE;
}
static BOOLEAN gmux_read8(UINT8 reg, UINT8 *v) {
    if (!gmux_ready()) return FALSE;
    io_out8(GMUX_BASE+GMUX_READ,reg);
    if (!gmux_complete()) return FALSE;
    *v=io_in8(GMUX_BASE+GMUX_VALUE); return TRUE;
}
static BOOLEAN gmux_write8(UINT8 reg, UINT8 v) {
    io_out8(GMUX_BASE+GMUX_VALUE,v);
    if (!gmux_ready()) return FALSE;
    io_out8(GMUX_BASE+GMUX_WRITE,reg);
    return gmux_complete();
}
static BOOLEAN gmux_read32(UINT8 reg, UINT32 *v) {
    if (!gmux_ready()) return FALSE;
    io_out8(GMUX_BASE+GMUX_READ,reg);
    if (!gmux_complete()) return FALSE;
    *v=io_in32(GMUX_BASE+GMUX_VALUE); return TRUE;
}

static BOOLEAN is_target_model(VOID) {
    SMBIOS_TABLE_ENTRY_POINT_LOCAL *ep=NULL;
    SMBIOS_TABLE_3_0_ENTRY_POINT_LOCAL *ep3=NULL;
    UINT8 *p, *end;
    if (!EFI_ERROR(LibGetSystemConfigurationTable(&SMBIOSTableGuid,(VOID **)&ep))) {
        p=(UINT8 *)(UINTN)ep->TableAddress; end=p+ep->TableLength;
    } else if (!EFI_ERROR(LibGetSystemConfigurationTable(&SMBIOS3TableGuid,(VOID **)&ep3))) {
        p=(UINT8 *)(UINTN)ep3->TableAddress; end=p+ep3->TableMaximumSize;
    } else return FALSE;

    while (p+4<=end && p[1]>=4 && p+p[1]<end) {
        UINT8 *strings=p+p[1], *next=strings;
        while (next+1<end && (next[0] || next[1])) ++next;
        if (next+1>=end) return FALSE;
        if (p[0]==127) break;
        if (p[0]==1 && p[1]>=6 && p[5]) {
            UINT8 *s=strings;
            for (UINTN i=1; i<p[5] && s<next; ++i) {
                while (s<next && *s) ++s;
                ++s;
            }
            static const CHAR8 model[]="MacBookPro11,5";
            return s+sizeof(model)<=next+1 &&
                CompareMem(s,model,sizeof(model))==0;
        }
        p=next+2;
    }
    return FALSE;
}

static EFI_STATUS find_intel(EFI_PCI_IO_PROTOCOL **out) {
    EFI_HANDLE *handles=NULL; UINTN count=0;
    EFI_STATUS st=uefi_call_wrapper(BS->LocateHandleBuffer,5,ByProtocol,
        &PciIoProtocol,NULL,&count,&handles);
    if (EFI_ERROR(st)) return st;
    st=EFI_NOT_FOUND;
    for (UINTN i=0;i<count;++i) {
        EFI_PCI_IO_PROTOCOL *pci=NULL; UINTN seg,bus,dev,fn; UINT32 id=0;
        EFI_STATUS x=uefi_call_wrapper(BS->HandleProtocol,3,handles[i],
            &PciIoProtocol,(VOID **)&pci);
        if (EFI_ERROR(x)) continue;
        x=uefi_call_wrapper(pci->GetLocation,5,pci,&seg,&bus,&dev,&fn);
        if (EFI_ERROR(x)||seg||bus||dev!=2||fn) continue;
        x=uefi_call_wrapper(pci->Pci.Read,5,pci,EfiPciIoWidthUint32,0,1,&id);
        if (!EFI_ERROR(x)&&id==0x0d268086) { *out=pci; st=EFI_SUCCESS; break; }
    }
    FreePool(handles); return st;
}

#ifdef INTEL_PANEL_DISABLE_AMD
/* Require the exact Radeon fitted to the MacBookPro11,5 before rail control. */
static EFI_STATUS find_amd(EFI_PCI_IO_PROTOCOL **out) {
    EFI_HANDLE *handles=NULL; UINTN count=0;
    EFI_STATUS st=uefi_call_wrapper(BS->LocateHandleBuffer,5,ByProtocol,
        &PciIoProtocol,NULL,&count,&handles);
    if (EFI_ERROR(st)) return st;
    st=EFI_NOT_FOUND;
    for (UINTN i=0;i<count;++i) {
        EFI_PCI_IO_PROTOCOL *pci=NULL;
        UINT32 id=0;
        EFI_STATUS x=uefi_call_wrapper(BS->HandleProtocol,3,handles[i],
            &PciIoProtocol,(VOID **)&pci);
        if (EFI_ERROR(x) || !pci) continue;
        x=uefi_call_wrapper(pci->Pci.Read,5,pci,EfiPciIoWidthUint32,0,1,&id);
        if (!EFI_ERROR(x) && id==((AMD_DEVICE_ID<<16)|AMD_VENDOR_ID)) {
            *out=pci; st=EFI_SUCCESS; break;
        }
    }
    FreePool(handles);
    return st;
}

/*
 * Indexed GMUX discrete-power sequence from apple-gmux:
 * write 1, then 0 = request dGPU OFF. Poll the power event if available,
 * with a bounded delay fallback. This function never requests power ON.
 */
static BOOLEAN gmux_power_off(BOOLEAN *event_seen) {
    UINT8 old_mask=0, bits=0;
    BOOLEAN mask_valid=gmux_read8(GMUX_INTERRUPT_ENABLE,&old_mask);
    if (event_seen) *event_seen=FALSE;
    if (!mask_valid) return FALSE;
    if (!gmux_write8(GMUX_INTERRUPT_ENABLE,0xff)) {
        gmux_write8(GMUX_INTERRUPT_ENABLE,old_mask);
        return FALSE;
    }
    if (gmux_read8(GMUX_INTERRUPT_STATUS,&bits) && bits)
        gmux_write8(GMUX_INTERRUPT_STATUS,bits);

    if (!gmux_write8(GMUX_DISCRETE_POWER,1) ||
        !gmux_write8(GMUX_DISCRETE_POWER,0)) {
        gmux_write8(GMUX_INTERRUPT_ENABLE,old_mask);
        return FALSE;
    }

    for (UINTN i=0;i<200;++i) {
        if (gmux_read8(GMUX_INTERRUPT_STATUS,&bits) &&
            (bits&GMUX_POWER_EVENT)) {
            if (event_seen) *event_seen=TRUE;
            gmux_write8(GMUX_INTERRUPT_STATUS,bits);
            break;
        }
        uefi_call_wrapper(BS->Stall,1,1000);
    }
    if (event_seen && *event_seen)
        uefi_call_wrapper(BS->Stall,1,20000);
    else
        uefi_call_wrapper(BS->Stall,1,250000);

    if (gmux_read8(GMUX_INTERRUPT_STATUS,&bits) && bits)
        gmux_write8(GMUX_INTERRUPT_STATUS,bits);
    gmux_write8(GMUX_INTERRUPT_ENABLE,old_mask);
    return TRUE;
}
#endif

static EFI_STATUS ddi_read(EFI_PCI_IO_PROTOCOL *pci, UINT64 reg, UINT32 *v) {
    return uefi_call_wrapper(pci->Mem.Read,6,pci,EfiPciIoWidthUint32,0,reg,1,v);
}
static EFI_STATUS ddi_write_a(EFI_PCI_IO_PROTOCOL *pci, UINT32 v) {
    return uefi_call_wrapper(pci->Mem.Write,6,pci,EfiPciIoWidthUint32,0,
        DDI_BUF_CTL_A,1,&v);
}

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st) {
    EFI_STATUS status=EFI_UNSUPPORTED;
    EFI_PCI_IO_PROTOCOL *intel=NULL;
#ifdef INTEL_PANEL_DISABLE_AMD
    EFI_PCI_IO_PROTOCOL *amd=NULL;
    UINT32 amd_identity=0xffffffff;
    BOOLEAN power_event=FALSE;
#endif
    APPLE_SET_OS *apple=NULL;
    UINT32 gmux_version=0, old_a=0, port_e=0, check_a=0;
    UINT8 old_display=0, old_ddc=0, check=0;
    BOOLEAN lanes_changed=FALSE, mux_touched=FALSE;
    InitializeLib(image,st);
    Print(L"IntelPanel: MacBookPro11,5 Intel internal display handoff\r\n");

    if (!is_target_model()) { Print(L"Refusing: SMBIOS model is not MacBookPro11,5.\r\n"); return EFI_UNSUPPORTED; }
    status=uefi_call_wrapper(BS->LocateProtocol,3,&apple_set_os_guid,NULL,(VOID **)&apple);
    if (EFI_ERROR(status) || !apple || !apple->set_os_version) {
        Print(L"Refusing: Apple Set OS protocol is unavailable.\r\n");
        return EFI_UNSUPPORTED;
    }
    status=uefi_call_wrapper(apple->set_os_version,1,(CHAR8 *)"Mac OS X 10.9");
    if (EFI_ERROR(status)) {
        Print(L"Refusing: Apple Set OS version call failed (%r).\r\n",status);
        return status;
    }
    if (apple->version >= 2 && apple->set_os_vendor) {
        status=uefi_call_wrapper(apple->set_os_vendor,1,(CHAR8 *)"Apple Inc.");
        if (EFI_ERROR(status)) {
            Print(L"Warning: Apple Set OS vendor call failed (%r).\r\n",status);
        }
    }
    Print(L"Apple Set OS: Mac OS X 10.9\r\n");
    if (!gmux_read32(4,&gmux_version) ||
        (gmux_version&0xffffff00)!=0x04001400 ||
        !gmux_read8(GMUX_DISPLAY,&old_display) ||
        !gmux_read8(GMUX_DDC,&old_ddc) ||
        (old_display!=2 && old_display!=3) ||
        (old_ddc!=1 && old_ddc!=2)) {
        Print(L"Refusing: unsupported GMUX version or selector state.\r\n");
        return EFI_UNSUPPORTED;
    }
    status=find_intel(&intel);
    if (EFI_ERROR(status)) { Print(L"Intel GPU 8086:0d26 not found.\r\n"); return status; }
#ifdef INTEL_PANEL_DISABLE_AMD
    status=find_amd(&amd);
    if (EFI_ERROR(status)) {
        Print(L"Refusing AMD-off mode: expected Radeon 1002:6821 not found.\r\n");
        return status;
    }
    Print(L"Radeon 1002:6821 detected; rail-off will be attempted after panel verification.\r\n");
#endif

    status=ddi_read(intel,DDI_BUF_CTL_A,&old_a);
    if (EFI_ERROR(status)) return status;
    status=ddi_read(intel,DDI_BUF_CTL_E,&port_e);
    if (EFI_ERROR(status)) return status;
    if (old_a==0xffffffff || port_e==0xffffffff ||
        (old_a&DDI_ENABLE) || (port_e&DDI_ENABLE)) {
        Print(L"Refusing: DDI Port A/E is enabled or unreadable.\r\n");
        return EFI_UNSUPPORTED;
    }

    if (!(old_a&DDI_A_4_LANES)) {
        status=ddi_write_a(intel,old_a|DDI_A_4_LANES);
        if (EFI_ERROR(status)) goto rollback;
        lanes_changed=TRUE;
    }
    status=ddi_read(intel,DDI_BUF_CTL_A,&check_a);
    if (EFI_ERROR(status) || check_a!=(old_a|DDI_A_4_LANES)) {
        if (!EFI_ERROR(status)) status=EFI_DEVICE_ERROR;
        goto rollback;
    }

    mux_touched=TRUE;
    if (!gmux_write8(GMUX_DDC,1) || !gmux_write8(GMUX_DISPLAY,2) ||
        !gmux_read8(GMUX_DDC,&check) || check!=1 ||
        !gmux_read8(GMUX_DISPLAY,&check) || check!=2) {
        status=EFI_DEVICE_ERROR; goto rollback;
    }

    Print(L"SUCCESS: Intel DDI Port A allocated four lanes.\r\n");
    Print(L"SUCCESS: GMUX DDC=Intel (1), internal panel=Intel (2).\r\n");
#ifdef INTEL_PANEL_DISABLE_AMD
    Print(L"Requesting Radeon rail OFF through indexed GMUX port 0x50...\r\n");
    if (!gmux_power_off(&power_event)) {
        Print(L"ERROR: GMUX AMD power-off transaction failed or timed out.\r\n");
        Print(L"Intel panel routing is retained; Radeon power state is unknown.\r\n");
        uefi_call_wrapper(BS->Stall,1,3000000);
        return EFI_DEVICE_ERROR;
    }
    status=uefi_call_wrapper(amd->Pci.Read,5,amd,EfiPciIoWidthUint32,0,1,&amd_identity);
    if (EFI_ERROR(status) || amd_identity==0xffffffff ||
        amd_identity!=((AMD_DEVICE_ID<<16)|AMD_VENDOR_ID)) {
        Print(L"AMD-off request accepted; PCI config no longer reports Radeon 1002:6821.\r\n");
        Print(L"GMUX power event: %s. PCI readback is not an electrical power measurement.\r\n",
              power_event ? L"seen" : L"not seen (settle delay used)");
    } else {
        Print(L"WARNING: GMUX accepted the command, but Radeon PCI config still responds.\r\n");
        Print(L"AMD power-off is NOT confirmed; Intel panel routing is retained.\r\n");
        uefi_call_wrapper(BS->Stall,1,3000000);
        return EFI_DEVICE_ERROR;
    }
#else
    Print(L"Radeon left powered; use IntelPanelAMDOff.efi only for experimental rail-off.\r\n");
#endif
    Print(L"Returning to OpenCore. Keep a direct recovery boot entry.\r\n");
    uefi_call_wrapper(BS->Stall,1,1500000);
    return EFI_SUCCESS;

rollback:
    Print(L"Switch failed (%r); attempting to restore original settings.\r\n",status);
    if (mux_touched) {
        BOOLEAN a=gmux_write8(GMUX_DDC,old_ddc);
        BOOLEAN b=gmux_write8(GMUX_DISPLAY,old_display);
        if (!a || !b) Print(L"WARNING: GMUX rollback was incomplete.\r\n");
    }
    if (lanes_changed) {
        EFI_STATUS r=ddi_read(intel,DDI_BUF_CTL_A,&check_a);
        if (!EFI_ERROR(r) && !(check_a&DDI_ENABLE)) {
            r=ddi_write_a(intel,old_a);
            if (EFI_ERROR(r)) Print(L"WARNING: DDI lane rollback failed.\r\n");
        } else Print(L"WARNING: skipped DDI rollback; register unavailable/enabled.\r\n");
    }
    return status;
}
