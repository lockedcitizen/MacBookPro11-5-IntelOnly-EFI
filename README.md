# MacBookPro11,5 Intel Display EFI

Experimental UEFI project for the Mid-2015 15-inch Retina MacBook Pro
(`MacBookPro11,5`, Intel Iris Pro 5200 + Radeon R9 M370X).

Goal: select Intel graphics for the internal Retina panel before an operating
system starts. HDMI/external-display support is out of scope.

## Status

**Early development scaffold — not a bootable Intel-switching release.**

The first milestone is a diagnostic EFI application and reproducible CI build.
It deliberately does not write GMUX, PCI/MMIO registers, NVRAM, or GPU power
controls. Do not install it as the default boot path.

The reference project
[bbatchelder/macbookpro11-5-intel-efi](https://github.com/bbatchelder/macbookpro11-5-intel-efi)
documents a model-specific Linux handoff. It is not proof of universal
Windows/macOS/Linux compatibility, and it does not electrically power off the
AMD GPU.

## Target hardware

- Model: `MacBookPro11,5`
- Intel iGPU: Iris Pro 5200 (expected PCI ID `8086:0d26`)
- AMD dGPU: Radeon R9 M370X (expected PCI ID `1002:6821`)
- Apple GMUX: indexed I/O interface; firmware/version must be detected
- Internal display: Retina panel

All hardware-changing code must first validate the exact model, GPU IDs,
GMUX version, register preconditions, and display state. A failed readback
must prevent handoff or restore the previous state.

## Planned boot flow

1. Identify MacBookPro11,5 and enumerate both GPUs.
2. Locate Apple's Set OS protocol where required to expose Intel IGD.
3. Read and validate GMUX and Intel display registers.
4. Select Intel DDC and internal-panel routing.
5. Configure Intel internal-panel lane allocation only after precondition
   checks; verify every write.
6. Provide ESC bypass and a recovery path.
7. Chainload the selected OS bootloader without changing NVRAM.

## OS support plan

- **Linux:** first validation target, based on the model-specific reference.
  Early `apple-gmux` and `i915` initialization may still be required.
- **Windows:** separate Boot Manager/Boot Camp chainload and graphics testing.
- **macOS/OpenCore:** separate OpenCore handoff and OCLP graphics testing.

The hardware handoff may be shared, but OS chainloading and post-handoff
configuration are not automatically interchangeable. No Windows or macOS
compatibility is claimed until tested on the actual machine.

Selecting Intel display routing does not itself power off the AMD GPU.
Discrete-GPU power-down is a separate, currently unimplemented task.

## Build

On x86-64 Ubuntu/Debian with GCC, binutils, GNU-EFI and make:

```sh
sudo apt-get install build-essential binutils gnu-efi file
make
```

Output: `build/IntelOnly.efi`.

The initial binary is diagnostic only. Test from a USB EFI System Partition;
keep your existing OpenCore and OS boot entries untouched.

## Safety rules

- Keep a known-good direct OpenCore/Windows/Linux boot path.
- Test from USB before considering internal-disk installation.
- Never replace `EFI/BOOT/BOOTX64.EFI` during development.
- Do not enable automatic default boot until hardware writes, rollback, and
  recovery have been validated.
- Do not attempt AMD power-off until thermal, wake, sleep, and display behavior
  have been tested.

## Attribution and license

See [ATTRIBUTION.md](ATTRIBUTION.md) and [LICENSE](LICENSE).
