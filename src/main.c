// SPDX-License-Identifier: GPL-2.0-only
/*
 * MacBookPro11,5 EFI console/GOP diagnostic.
 * No GMUX I/O, Intel DDI writes, AMD GPU changes, or NVRAM writes.
 * Uses the UEFI system table directly to isolate EFI console startup.
 */
#include <efi.h>
#include <efilib.h>

static VOID output(EFI_SYSTEM_TABLE *st, CHAR16 *message)
{
    if (st && st->ConOut && st->ConOut->OutputString)
        uefi_call_wrapper(st->ConOut->OutputString, 2, st->ConOut, message);
}

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    EFI_INPUT_KEY key;
    EFI_STATUS status;
    (void)image;

    output(st, L"\r\nMacBookPro11,5 Intel-only project\r\n");
    output(st, L"EFI console/GOP diagnostic build\r\n");
    output(st, L"-----------------------------------\r\n");
    output(st, L"PASS: EFI application entry reached.\r\n");
    output(st, L"PASS: UEFI ConOut is available.\r\n");
    output(st, L"SAFE MODE: no GMUX, DDI, GPU, or NVRAM changes.\r\n");
    output(st, L"\r\nPress any key to return to OpenCore.\r\n");

    if (!st || !st->ConIn || !st->ConIn->ReadKeyStroke)
        return EFI_UNSUPPORTED;

    for (;;) {
        status = uefi_call_wrapper(st->ConIn->ReadKeyStroke, 2, st->ConIn, &key);
        if (!EFI_ERROR(status))
            break;
        if (status != EFI_NOT_READY)
            return status;
        uefi_call_wrapper(st->BootServices->Stall, 1, 10000);
    }

    output(st, L"Returning without making hardware changes.\r\n");
    return EFI_SUCCESS;
}
