# Attribution

The project contains an adapted version of the Linux EFI helper from:

- **Bryan Batchelder, macbookpro11-5-intel-efi**:
  https://github.com/bbatchelder/macbookpro11-5-intel-efi

The file `src/linux/intel-before-linux.c` is derived from upstream
`intel-before-linux.c`. Changes in this repository: the Linux UKI path was
changed from the Omarchy-specific filename to
`\\EFI\\Linux\\linux.efi`; the checkpoint log path was changed to
`\\EFI\\IntelOnly\\intel-before-linux.log`; and the startup log text
was adjusted. The hardware-specific register logic and upstream GPL-2.0-only
license are retained. See the source header and [LICENSE](LICENSE).

Related technical references:

- **0xbb/apple_set_os.efi** — Apple Set OS protocol reference:
  https://github.com/0xbb/apple_set_os.efi
- **Linux kernel apple-gmux driver** — indexed GMUX behavior:
  https://github.com/torvalds/linux/blob/master/drivers/platform/x86/apple-gmux.c
- **Linux kernel Intel display register definitions**:
  https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/i915/display/intel_display_regs.h

This project is independent and is not an official Apple, Omarchy, or Linux
kernel component.
