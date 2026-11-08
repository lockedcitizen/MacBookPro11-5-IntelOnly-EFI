// SPDX-License-Identifier: GPL-2.0-only
/*
 * IntelGOPProbe.efi
 *
 * Read-only firmware diagnostic for MacBookPro11,5.
 * Enumerates EFI GOP handles and Intel PCI display devices without touching
 * GMUX, DDI registers, NVRAM, or GPU power state. This determines whether
 * firmware already exposes a GOP that can be associated with the Intel iGPU.
 */
#include <efi.h>
#include <efilib.h>
#include <efipciio.h>

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

typedef struct {
    UINT32 Version;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    UINT32 PixelFormat;
    UINT32 PixelInformation[4];
    UINT32 PixelsPerScanLine;
} GOP_MODE_INFO_LOCAL;

typedef struct {
    UINT32 MaxMode;
    UINT32 Mode;
    GOP_MODE_INFO_LOCAL *Info;
    UINTN SizeOfInfo;
    UINT64 FrameBufferBase;
    UINTN FrameBufferSize;
} GOP_MODE_LOCAL;

typedef struct {
    EFI_STATUS (EFIAPI *QueryMode)(VOID *, UINT32, UINTN *, GOP_MODE_INFO_LOCAL **);
    EFI_STATUS (EFIAPI *SetMode)(VOID *, UINT32);
    VOID *Blt;
    GOP_MODE_LOCAL *Mode;
} GOP_LOCAL;

static EFI_GUID gop_guid =
    {0x9042a9de,0x23dc,0x4a38,{0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};

static BOOLEAN is_target_model(VOID) {
    SMBIOS_TABLE_ENTRY_POINT_LOCAL *ep = NULL;
    SMBIOS_TABLE_3_0_ENTRY_POINT_LOCAL *ep3 = NULL;
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

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st) {
    EFI_STATUS status;
    EFI_HANDLE *handles=NULL;
    UINTN count=0, intel_count=0;
    InitializeLib(image,st);
    Print(L"IntelGOPProbe: read-only MacBookPro11,5 firmware inspection\r\n");
    if (!is_target_model()) {
        Print(L"Refusing: SMBIOS model is not MacBookPro11,5.\r\n");
        return EFI_UNSUPPORTED;
    }

    status=uefi_call_wrapper(BS->LocateHandleBuffer,5,ByProtocol,
        &gop_guid,NULL,&count,&handles);
    if (EFI_ERROR(status)) {
        Print(L"No EFI Graphics Output Protocol handles found (%r).\r\n",status);
        count=0;
    } else {
        Print(L"GOP handles found: %u\r\n",count);
        for (UINTN i=0;i<count;++i) {
            GOP_LOCAL *gop=NULL;
            EFI_STATUS s=uefi_call_wrapper(BS->HandleProtocol,3,handles[i],
                &gop_guid,(VOID **)&gop);
            if (EFI_ERROR(s) || !gop || !gop->Mode || !gop->Mode->Info) {
                Print(L"GOP[%u]: handle=%p, mode data unavailable (%r)\r\n",i,handles[i],s);
                continue;
            }
            Print(L"GOP[%u]: handle=%p mode=%u/%u %ux%u format=%u pitch=%u FB=%lx size=%lx\r\n",
                i,handles[i],gop->Mode->Mode,gop->Mode->MaxMode,
                gop->Mode->Info->HorizontalResolution,
                gop->Mode->Info->VerticalResolution,
                gop->Mode->Info->PixelFormat,
                gop->Mode->Info->PixelsPerScanLine,
                gop->Mode->FrameBufferBase,
                (UINT64)gop->Mode->FrameBufferSize);

            EFI_PCI_IO_PROTOCOL *pci=NULL;
            s=uefi_call_wrapper(BS->HandleProtocol,3,handles[i],
                &PciIoProtocol,(VOID **)&pci);
            if (!EFI_ERROR(s) && pci) {
                UINT32 id=0; UINTN seg=0,bus=0,dev=0,fn=0;
                EFI_STATUS ps=uefi_call_wrapper(pci->GetLocation,5,pci,&seg,&bus,&dev,&fn);
                if (!EFI_ERROR(ps))
                    ps=uefi_call_wrapper(pci->Pci.Read,5,pci,EfiPciIoWidthUint32,0,1,&id);
                if (!EFI_ERROR(ps))
                    Print(L"       same-handle PCI %04x:%04x at %04x:%02x:%02x.%x\r\n",
                        id&0xffff,id>>16,seg,bus,dev,fn);
                else Print(L"       same-handle PCI query failed (%r)\r\n",ps);
            } else {
                Print(L"       no PCI I/O protocol on GOP handle\r\n");
            }
        }
        FreePool(handles);
    }

    handles=NULL; count=0;
    status=uefi_call_wrapper(BS->LocateHandleBuffer,5,ByProtocol,
        &PciIoProtocol,NULL,&count,&handles);
    if (!EFI_ERROR(status)) {
        for (UINTN i=0;i<count;++i) {
            EFI_PCI_IO_PROTOCOL *pci=NULL;
            UINT32 id=0; UINTN seg=0,bus=0,dev=0,fn=0;
            EFI_STATUS s=uefi_call_wrapper(BS->HandleProtocol,3,handles[i],
                &PciIoProtocol,(VOID **)&pci);
            if (EFI_ERROR(s) || !pci) continue;
            s=uefi_call_wrapper(pci->GetLocation,5,pci,&seg,&bus,&dev,&fn);
            if (EFI_ERROR(s) || seg || bus || dev!=2 || fn) continue;
            s=uefi_call_wrapper(pci->Pci.Read,5,pci,EfiPciIoWidthUint32,0,1,&id);
            if (!EFI_ERROR(s) && id==0x0d268086) {
                ++intel_count;
                Print(L"Intel Iris Pro PCI function: handle=%p at %04x:%02x:%02x.%x ID=%08x\r\n",
                    handles[i],seg,bus,dev,fn,id);
            }
        }
        FreePool(handles);
    }
    Print(L"Intel PCI functions found: %u\r\n",intel_count);
    Print(L"Probe complete. No GMUX/DDI/NVRAM/GPU-power settings were changed.\r\n");
    Print(L"Photograph this screen before rebooting.\r\n");
    uefi_call_wrapper(BS->Stall,1,10000000);
    return EFI_SUCCESS;
}
