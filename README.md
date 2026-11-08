# MacBookPro11,5 Intel Display EFI

Experimental UEFI project for the Mid-2015 15-inch Retina MacBook Pro
(`MacBookPro11,5`, Intel Iris Pro 5200 + Radeon R9 M370X).

Goal: select Intel graphics for the internal Retina panel before an operating
system starts. HDMI/external-display support is out of scope.

## Current status

Three EFI binaries are built:

- `IntelOnly.efi`: read-only diagnostic scaffold.
- `IntelLinux.efi`: experimental Linux-only handoff prototype.
- `IntelPanel.efi`: experimental panel-switch utility intended to be launched
  from the OpenCore picker. It selects Intel DDC and the internal panel,
  assigns four lanes to Intel Haswell DDI Port A, verifies the register
  readbacks, and returns to OpenCore (`EFI_SUCCESS`) without launching an OS.

**IntelPanel.efi is the multi-OS handoff option:** after it returns, select
macOS, Linux or Windows in OpenCore. This is an experimental firmware-state
handoff, not a guarantee that every OS will preserve or correctly initialize
the Intel display path. It does not itself disable or power off the AMD GPU.

**IntelLinux.efi remains Linux-only.**
It expects a Linux Unified Kernel Image (UKI) at
`\\EFI\\Linux\\linux.efi` on the same EFI System Partition and forwards
the helper's EFI load options as the Linux image command line. Create the
`\\EFI\\IntelOnly\\` directory on the ESP for its checkpoint log.
These paths are currently compiled into the source.

Do not install either binary as your default boot path. Hardware testing has
not been performed on the repository owner's machine.

## Hardware checks and handoff

The Intel panel utility and Linux helper use the following model-specific sequence:

1. Require SMBIOS product `MacBookPro11,5`.
2. Locate and load the configured Linux UKI before making display changes.
3. Call Apple's Set OS protocol with the macOS 10.9 identity.
4. Require GMUX firmware 4.0.20 at I/O base `0x700` and validate initial
   display/DDC selector values.
5. Verify Intel GPU `8086:0d26` at PCI `0000:00:02.0`.
6. Read Intel DDI Port A and E registers. Refuse to reallocate lanes if either
   port is enabled or unreadable.
7. Allocate four lanes to Port A, verify readback, select Intel DDC and the
   internal panel through GMUX, and verify selector readback.
8. For `IntelPanel.efi`, return to OpenCore after successful readback. If an
   error occurs, attempt to restore the original lane allocation and GMUX
   selectors. For `IntelLinux.efi`, start the configured Linux UKI instead.

The helper does not write NVRAM, change the external-display mux, or power
off the AMD GPU. Intel panel routing is not proof that the AMD GPU is
electrically powered down.

## Linux requirements

The reference configuration loads `apple-gmux` before `i915` in the
initramfs. A Linux distribution may require corresponding early-KMS setup.
The upstream tested environment used Omarchy/Limine; this repository's helper
uses a generic UKI filename, but that does not make the rest of the tested
kernel configuration portable without validation.

The helper's checkpoint log is written to
`\\EFI\\IntelOnly\\intel-before-linux.log` on the same ESP.

## Build

On x86-64 Ubuntu/Debian with GCC, binutils, GNU-EFI and make:

```sh
sudo apt-get install build-essential binutils gnu-efi file
make
```

Outputs:

- `build/IntelOnly.efi`
- `build/IntelLinux.efi`
- `build/IntelPanel.efi`

GitHub Actions builds both and uploads them as a workflow artifact. CI
compilation is not equivalent to firmware or hardware validation.

## Windows and macOS status

Windows Boot Manager and macOS/OpenCore handoff are **not implemented or
tested**. Do not assume the Linux-specific UKI loading and EFI load-option
handling works for them. A cross-OS design needs separate bootloader handoff
and validation for each OS.

## Safety and recovery

- Keep a known-good direct OpenCore/Windows/Linux boot path.
- Test from a USB EFI System Partition first.
- Keep Secure Boot disabled for this unsigned development binary.
- Never replace `EFI/BOOT/BOOTX64.EFI`.
- Use a separate test boot entry; do not change the default until tested.
- If the helper fails or the child image returns, it attempts to restore the
  original GMUX selectors and Port A lane register. Restoration cannot be
  guaranteed after a firmware/hardware fault.
- No AMD power-off operation is implemented.

## Attribution and license

The Linux helper is adapted from
[bbatchelder/macbookpro11-5-intel-efi](https://github.com/bbatchelder/macbookpro11-5-intel-efi).
See [ATTRIBUTION.md](ATTRIBUTION.md) and [LICENSE](LICENSE).
