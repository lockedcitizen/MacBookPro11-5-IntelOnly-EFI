// SPDX-License-Identifier: GPL-2.0-only
/*
 * MacBookPro11,5 Intel-only EFI — development scaffold.
 *
 * This first milestone is deliberately read-only. It does not access GMUX,
 * PCI MMIO registers, NVRAM, or GPU power controls.
 */
#include <efi.h>
#include <efilib.h>

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system_table)
{
    EFI_INPUT_KEY key;
    EFI_STATUS status;

    InitializeLib(image, system_table);

    Print(L"MacBookPro11,5 IntelOnly EFI - diagnostic scaffold\r\n");
    Print(L"No hardware has been modified.\r\n");
    Print(L"Press any key to return to firmware boot manager.\r\n");

    status = uefi_call_wrapper(ST->ConIn->Reset, 2, ST->ConIn, FALSE);
    if (EFI_ERROR(status)) {
        Print(L"Warning: console input reset failed: %r\r\n", status);
    }

    for (;;) {
        status = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2,
                                   ST->ConIn, &key);
        if (status != EFI_NOT_READY)
            break;
        uefi_call_wrapper(BS->Stall, 1, 10000);
    }

    return EFI_SUCCESS;
}
