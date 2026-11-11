# MacBookPro11,5 Intel-only EFI — GOP diagnostic stage

This branch is a **safe diagnostic build**, not yet the Intel-only switcher.

## Why this branch exists

The previous GMUX utility reportedly produced a black screen immediately after it was selected in OpenCore, before its menu appeared. That timing means we must isolate EFI application startup and console/GOP compatibility before touching the display mux.

## What this diagnostic does

- Enters as a UEFI application and writes directly through the firmware-provided ConOut protocol.
- Waits for a key and returns to OpenCore.
- Does not initialize GNU-EFI's library console.
- Does not access GMUX ports, Intel DDI registers, PCI configuration, or SMBIOS.
- Does not change AMD power, GPU routing, or NVRAM.

## Test

Build using the GitHub Actions workflow on this branch and download the `MacBookPro11-5-GMUX` artifact.

In OpenCore, add this EFI as a temporary Tools entry and launch it.

Expected result: a text screen titled **EFI console/GOP diagnostic build** appears. Press any key to return to OpenCore.

- If it still goes black before the text appears, the failure is below the GMUX code: investigate OpenCore's EFI tool launch, image format/ABI, and GOP console handoff.
- If the text appears and returns normally, the previous app's startup/library or pre-menu path needs investigation. Only then should we reintroduce model checks and GMUX access in separate stages.

## Important limitation

This diagnostic does **not** select Intel, disable the AMD Radeon R9 M370X, or boot macOS in Intel-only mode. It is deliberately non-destructive. A successful display test is a prerequisite, not proof that a macOS Intel-only handoff works.

For MacBookPro11,5, GMUX display routing and AMD GPU power control are separate operations. The known MacBookPro11,5 EFI handoff research documents Intel panel selection but explicitly does not power off the AMD GPU; its tested operating-system flow is Linux, not macOS. Do not treat it as a proven macOS fix.

## Build

On Debian/Ubuntu:

```sh
sudo apt-get update
sudo apt-get install -y build-essential gnu-efi binutils
make clean all
```

Output: `build/MacBookPro11-5-GMUX.efi`.
