# macOS / OpenCore Intel-panel and AMD-off experiment

Target: 15-inch Mid-2015 MacBookPro11,5 (Intel Iris Pro 5200, PCI ID
8086:0d26; Radeon R9 M370X, PCI ID 1002:6821; indexed GMUX 4.0.20 at
I/O base 0x700).

## EFI applications

- `IntelGOPProbe.efi`: read-only diagnostic. Run first and save a
  photograph of its output. It does not write GMUX, DDI, NVRAM, or GPU power.
- `IntelPanel.efi`: assigns four lanes to Intel DDI Port A, selects Intel DDC
  and the internal panel, verifies readback, leaves AMD powered, and returns
  to OpenCore.
- `IntelPanelAMDOff.efi`: performs the same checks, requires Radeon PCI
  function 1002:6821, switches and verifies the internal panel on Intel,
  then requests Radeon rail-off using indexed GMUX discrete-power port 0x50
  (write 1, then 0). It polls the GMUX power event when available and checks
  whether the Radeon PCI function still responds. It never requests power-on
  and does not write NVRAM.

## Build artifact

GitHub Actions builds both variants and uploads `IntelPanelAMDOff.efi` as
part of the `MacBookPro11-5-IntelOnly-EFI` artifact. The build runs on a
GitHub-hosted Ubuntu runner; no local compiler or Docker installation is
needed. CI compilation only proves that the EFI image links; it does not
validate firmware behavior.

## OpenCore installation

1. Keep a working direct OpenCore entry that does not run either helper.
   Test from a USB EFI System Partition first.
2. Run `IntelGOPProbe.efi` from OpenCore Tools and inspect whether firmware
   exposes an Intel-associated GOP. If Intel GOP is absent, do not assume the
   OpenCore picker will remain visible after switching the panel to Intel.
3. Copy the selected EFI app to `EFI/OC/Tools/` on the test EFI partition.
4. Add it under `Misc -> Tools` in `config.plist`, for example:

   ```text
   Path: EFI/OC/Tools/IntelPanel.efi
   Name: Intel panel (AMD remains on)
   ```

   For the experimental rail-off test, use
   `EFI/OC/Tools/IntelPanelAMDOff.efi` and label it clearly as experimental.
   Leave Arguments empty and make the tool visible in the picker.
5. Launch the helper from OpenCore. It should return to its caller after
   displaying the result. Then select macOS. Keep the direct recovery entry
   available throughout testing.

## Limitations and recovery

- The app changes volatile hardware state for the current boot only; it does
  not write `gpu-power-prefs` or other NVRAM variables.
- A black screen after mux switching is possible if Apple's firmware GOP
  does not expose a usable Intel framebuffer. The display may not return
  until macOS's Intel graphics driver initializes. Do not blindly select a
  boot entry during the first test.
- macOS graphics drivers and power management may reinitialize the Radeon.
  This EFI command is not a persistent macOS GPU-disable mechanism.
- A GMUX power event or disappearance of PCI config reads is not an
  electrical power measurement. Verify power draw, temperature and macOS
  device behavior on the target before relying on AMD-off mode.
- HDMI/external displays are not supported; the external-display mux is not
  changed.
- Do not use this on other Mac models. Do not replace
  `EFI/BOOT/BOOTX64.EFI`.
- If the screen is blank, force a shutdown and use the direct recovery entry.
  Keep the Radeon-powered `IntelPanel.efi` route available.

This is an experimental UEFI utility, not an Apple-supported feature. The
AMD-off binary and macOS operation have not been hardware-tested by this
repository's maintainers.
