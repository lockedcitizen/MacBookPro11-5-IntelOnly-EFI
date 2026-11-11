# MacBookPro11,5 Indexed GMUX UEFI

A clean-room, model-specific UEFI utility for the 2015 15-inch Retina MacBook Pro (MacBookPro11,5).

This project uses the GMUX *indexed I/O* interface found on pre-T2 Retina MacBook Pros. It is not a T2 SMC/MMIO implementation.

## Current scope

- Refuses to operate unless SMBIOS identifies `MacBookPro11,5`.
- Requires Intel Iris Pro 5200 PCI identity `8086:0d26` at `0000:00:02.0`.
- Requires the indexed GMUX version signature documented by the MacBookPro11,5 reference helper (4.0.20).
- Reads and verifies Haswell DDI Port A and Port E before changing the Port A four-lane allocation bit.
- Can select Intel DDC and the internal display through GMUX and verify both selectors.
- Can restore the values observed at startup.
- Makes no NVRAM writes and does not power off the Radeon.
- Returns to the caller (for example, OpenCore) when an action completes.

**This is an experimental bring-up project. It has not yet been validated on physical hardware.** A successful build is not proof that the display will remain visible after changing the mux.

## Keys

| Key | Action |
|---|---|
| `I` | Select Intel DDC + internal panel, allocate four lanes on Haswell Port A if both DDI ports are disabled, verify readback, then return to the caller |
| `R` | Restore the GMUX selectors and Port A lane allocation captured at startup, then return |
| `Esc` | Exit without making changes |

The app deliberately does not offer an AMD power-off operation. GPU rail control is separate from display mux selection and must not be guessed.

## Hardware facts used

- Model: `MacBookPro11,5`
- Intel GPU: Iris Pro 5200, PCI ID `8086:0d26`
- AMD GPU: Radeon R9 M370X, PCI ID `1002:6821`
- Indexed GMUX base used by the tested reference: I/O `0x700`
- GMUX selectors: DDC `0x28`, display `0x10`
- Intel DDI registers: Port A `0x64000`, Port E `0x64400`
- Four-lane allocation bit: Port A bit 4
- Intel selector values: DDC `1`, internal display `2`

The GMUX transaction protocol and selector values follow the Linux `apple-gmux` driver. The model-specific device IDs, GMUX version guard, and DDI preconditions follow the MacBookPro11,5 Intel EFI research project.

## Build

GitHub Actions builds `MacBookPro11-5-GMUX.efi` with GNU-EFI on Ubuntu. Push a commit to this branch or run the workflow manually; download the artifact from the Actions run.

Local build on Debian/Ubuntu:

```sh
sudo apt-get update
sudo apt-get install -y build-essential gnu-efi binutils
make clean all
```

Output: `build/MacBookPro11-5-GMUX.efi`.

## Testing and recovery

1. Keep a known-good OpenCore entry that bypasses this utility.
2. First test from a removable EFI partition or a separate OpenCore Tools entry.
3. Do not overwrite your normal OpenCore loader.
4. Start with `Esc` to confirm that model/GPU/GMUX detection works.
5. Only then test `I`. The display can go black after the mux changes because the firmware GOP may still describe the AMD framebuffer.
6. If the screen becomes unusable, force power off and boot through the untouched recovery entry. This utility does not write NVRAM, so it does not intentionally persist the selection across a cold boot.

The utility restores settings only if you invoke `R` or if an internal operation fails before it returns. It cannot restore the display after the operating system has taken over.

## References and attribution

- GMUX menu/UEFI design inspiration: https://github.com/Osaul4ik/t2-gmux-uefi
- MacBookPro11,5 hardware and Intel handoff reference: https://github.com/bbatchelder/macbookpro11-5-intel-efi
- Indexed GMUX protocol and selector definitions: https://github.com/torvalds/linux/blob/master/drivers/platform/x86/apple-gmux.c and https://github.com/torvalds/linux/blob/master/include/linux/apple-gmux.h
- Intel Haswell display register definitions: Linux i915 display register headers.

This repository is an independent, model-specific implementation. It does not copy the T2 project's MMIO/SMC control path. Source is licensed GPL-2.0-only; see SPDX notices in the source.
