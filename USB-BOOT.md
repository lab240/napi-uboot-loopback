# USB boot (experimental — NOT yet hardware-verified)

This branch (`usb-boot`) adds `patches/1136-loopdetect-usb-first-on-loop.patch`
on top of the verified `main` set, to boot from a USB mass-storage device when
the loopback plug is present.

## Behaviour

Extends the `loopdetect` preboot: when `napi_loop=1` (plug present) it runs
`usb start` and sets `boot_targets="usb mmc0 mmc1 nvme scsi pxe dhcp spi"`, so
`bootflow scan` tries, in order, **USB → internal flash → SD**. When no plug is
present the default SD-first order is unchanged.

Task-1 is preserved: with the plug but **no** bootable USB device, `bootflow
scan` falls through USB to `mmc0` (internal flash), exactly as before.

## Requirements / caveats

* Only **EHCI** is enabled in U-Boot (no OHCI) → use a **USB 2.0** (high-speed)
  device; a USB 1.1 stick may not enumerate. `CONFIG_PHY_ROCKCHIP_INNO_USB2` is
  present, so the USB2 PHY is driven.
* The USB device must be on a **host** port (the board's EHCI, e.g. via the
  onboard hub), not the dwc2 OTG port (that port is the gadget/maskrom port).
* The device needs a bootable layout BOOTSTD/distro can boot (an Armbian image
  written to the USB device, same as the SD).

## Status

**UNVERIFIED on hardware.** Built OK (U-Boot `P1f56`). Not flashed. To test:
flash this build to the internal flash (see the main README's *Deploy*), insert
a bootable USB 2.0 device, fit the loopback plug, and cold-boot; it should boot
the USB device. Keep off `main` until confirmed on hardware.
