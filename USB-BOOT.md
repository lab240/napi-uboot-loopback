# USB boot (hardware-verified)

`patches/1136-loopdetect-usb-first-on-loop.patch` extends the `loopdetect`
preboot to boot from a USB mass-storage device when the loopback plug is
present. It is a strict superset of the base set (patches 1130–1135): with no
bootable USB device it behaves exactly like the plain build.

## Behaviour

When `napi_loop=1` (plug present) the preboot runs `usb start` and sets
`boot_targets="usb mmc0 mmc1 nvme scsi pxe dhcp spi"`, so `bootflow scan` tries,
in order, **USB → internal flash → SD**, booting the first target that holds a
valid bootable image. When no plug is present the default SD-first order is
unchanged.

| plug | USB device | boots |
|------|------------|-------|
| present | bootable USB | **USB** |
| present | no USB / USB without a bootable image | **internal flash** (eMMC/NAND) |
| absent  | — | **SD** |

Task-1 is preserved: with the plug but **no** bootable USB device, `bootflow
scan` falls through USB to `mmc0` (internal flash) — `usb` is simply skipped, no
hang. This is why this build can be the single universal firmware; the only cost
is the ~1–2 s `usb start` bus enumeration on every plug-present boot.

## Requirements / caveats

* Only **EHCI** is enabled in U-Boot (no OHCI) → use a **USB 2.0** (high-speed)
  device; a USB 1.1 stick may not enumerate. `CONFIG_PHY_ROCKCHIP_INNO_USB2` is
  present, so the USB2 PHY is driven.
* The USB device must be on a **host** port (the board's EHCI, e.g. via the
  onboard hub), not the dwc2 OTG port (that port is the gadget/maskrom port).
* The device needs a bootable layout BOOTSTD/distro can boot (an Armbian image
  written to the USB device, same as the SD).
* **The USB device must have a unique filesystem UUID.** An Armbian image
  written to a stick keeps the image's root UUID, which can collide with the
  installed SD/eMMC root — the kernel may then mount the wrong root. Give the
  stick a fresh UUID (`tune2fs -U $(cat /proc/sys/kernel/random/uuid) /dev/sdX1`)
  and update its `/boot/armbianEnv.txt` `rootdev=UUID=...` to match.

## Status

**VERIFIED on hardware (2026-10-24).** Built as U-Boot `P1f56`
(`u-boot-rockchip.bin` sha256
`db52888f480ec274354c11ebb34222a5e4934ee96e2c114da1893eb107911a7e`). Flashed to
a board's internal eMMC and confirmed on cold boot: plug + bootable USB → booted
USB (`root=UUID=…` of the stick, `TRAN=usb`); plug + stick without a bootable
image → booted internal eMMC (fall-through); no plug → booted SD.
