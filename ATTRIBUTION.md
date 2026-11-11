# Attribution and licensing

This project is an independent MacBookPro11,5-specific implementation.

- GMUX transaction sequencing, register offsets and selector values are based on the Linux kernel `apple-gmux` driver by its contributors. See https://github.com/torvalds/linux/blob/master/drivers/platform/x86/apple-gmux.c and https://github.com/torvalds/linux/blob/master/include/linux/apple-gmux.h.
- Hardware identification, indexed GMUX version guard, Haswell Port A/E register locations and lane-allocation preconditions are informed by https://github.com/bbatchelder/macbookpro11-5-intel-efi.
- The menu-oriented UEFI application concept was inspired by https://github.com/Osaul4ik/t2-gmux-uefi. This project does not port its T2 SMC/MMIO code.

Source files carry SPDX-License-Identifier: GPL-2.0-only. GNU-EFI is a separate build dependency under its own license.
