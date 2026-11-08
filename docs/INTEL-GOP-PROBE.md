# Intel GOP probe (MacBookPro11,5)

This is a **read-only diagnostic**, not the Intel GMUX switcher.

## Purpose

The MacBookPro11,5 firmware may expose one or more EFI Graphics Output Protocol (GOP) handles. Before attempting an Intel internal-panel handoff that returns to OpenCore, we need to determine whether a GOP is already available for the Intel Iris Pro 5200.

`IntelGOPProbe.efi` enumerates GOP handles and Intel PCI display functions. It does not:
- write GMUX registers;
- change DDI lane allocation;
- change NVRAM;
- disable or power down either GPU;
- change display routing.

## Build

The normal GitHub Actions **Build EFI** workflow produces `build/IntelGOPProbe.efi` alongside the other EFI files.

## Run

1. Keep your known-good OpenCore boot entry and recovery USB available.
2. Copy `IntelGOPProbe.efi` to the EFI System Partition, for example `EFI/Tools/IntelGOPProbe.efi`.
3. Add it as an OpenCore tool entry using the same method you use for other EFI tools, or launch it from the OpenCore picker if your configuration exposes EFI tools.
4. Run it without running `IntelPanel.efi`.
5. Photograph the complete output and reboot normally.

The output includes each GOP handle's resolution, pixel format and framebuffer address, plus the Intel PCI function handle. A GOP handle that exposes PCI I/O on the same handle is especially useful evidence, although a child GOP handle may not expose PCI I/O directly.

**Do not infer that Intel GOP is available merely because the Intel PCI function is detected.** If the firmware only exposes an AMD-backed GOP, switching GMUX to Intel can blank the OpenCore picker. The probe establishes the next engineering step without risking that switch.
