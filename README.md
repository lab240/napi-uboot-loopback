# napi-uboot-loopback

Boot-source selection for **NAPI-C** boards (Rockchip RK3308, Armbian) driven by a
**single-port Ethernet loopback plug** on the onboard GMAC port (`end0`):

* **loopback plug present → boot the internal flash** (eMMC / NAND, `armbi_nand`)
* **no loopback → boot the SD card** (`armbi_root`)

The decision is made **inside U-Boot on the internal flash, live on every boot** —
there is no on-disk flag and no dependency on what firmware the SD card happens to
carry. The internal bootloader always runs first (see *Boot order* below), probes
`end0` for the loop, and picks the boot device accordingly.

## How it works

```
BootROM ──> internal-flash U-Boot (mmc0)
                    │
                    ├─ preboot: run `loopdetect`
                    │     sends raw L2 frames on end0, listens for the echo
                    │     sets env napi_loop = 1 (loop) / 0 (no loop)
                    │
                    └─ bootcmd: `bootflow scan`  (stock BOOTSTD)
                          napi_loop=1 → boot_targets reordered "mmc0 mmc1 ..."  → internal first
                          napi_loop=0 → default boot_targets "mmc1 mmc0 ..."    → SD first
```

* **`loopdetect`** (`cmd/napi_loopdetect.c`): a U-Boot command. It `eth_init()`s the
  active device, sends a few broadcast frames (ethertype `0x88B5`, magic `NAPILOOP`
  + an 8-byte nonce, padded to 60 B) via the driver's `->send`, then polls `->recv`
  for ~1 s for the same frame to return on RX. A passive TX↔RX loopback plug echoes
  the frame; a real switch never reflects a frame to its source port. Matching is by
  **ethertype + nonce**, so it does not depend on the source MAC being correct.
  The result is stored in env **`napi_loop`** (`1`/`0`). It always sets `napi_loop`
  (0 on any failure / no-link path), so the boot branch is safe.

* **Wiring** (`patches/1133-…`): the board defconfig enables `CONFIG_USE_PREBOOT`
  and sets `CONFIG_PREBOOT` to run `loopdetect` and, when `napi_loop=1`, reorder the
  `boot_targets` env to internal-first. The stock BOOTSTD `bootcmd` (`bootflow scan`)
  then boots the first valid bootdev in that order. Because `CONFIG_BOOTSTD_FULL` is
  off, `bootflow scan` takes no device argument and simply follows `boot_targets`
  (which is read live from the environment), so reordering that env is enough.
  Falls back to the SD (`mmc1`) if the internal flash is not bootable.

### The loopback plug

A passive RJ45 plug that loops the 10/100 TX pair back to the RX pair:

| loop | pins |
|------|------|
| white-orange ↔ white-green | 1 ↔ 3  (TX+ ↔ RX+) |
| orange ↔ green             | 2 ↔ 6  (TX− ↔ RX−) |

## Build

Patches target **U-Boot v2024.10** as built by the Armbian build system for board
`napi-c` (`BOOTCONFIG=napi-c-rk3308_defconfig`, `BOOTPATCHDIR=v2024.10`).

Drop the patches into `patch/u-boot/v2024.10/board_napi-c/` of the Armbian build
tree and build the bootloader:

```sh
./compile.sh uboot BOARD=napi-c BRANCH=current
# if the rkbin fetch fails on the network, build offline from cache:
OFFLINE_WORK=yes ARTIFACT_IGNORE_CACHE=yes ./compile.sh uboot BOARD=napi-c BRANCH=current
```

Output: `output/debs/linux-u-boot-napi-c-current_*.deb`, containing the flashable
`usr/lib/linux-u-boot-current-napi-c/u-boot-rockchip.bin`.

* `patches/1130-add-board-napi-c.patch` — the NAPI-C board itself (DT, defconfig). Baseline, included for a self-contained set.
* `patches/1132-napi-loopdetect.patch` — adds the `loopdetect` command (`cmd/napi_loopdetect.c`, Kconfig, Makefile) and enables it in the defconfig.
* `patches/1133-napi-loopdetect-wire-boot.patch` — wires it into the boot flow via `CONFIG_PREBOOT` / `boot_targets`.

## Deploy

Flash **only the bootloader** to the **internal** flash (`/dev/mmcblk0` on NAPI-C =
U-Boot `mmc0`). Do **not** touch the rootfs. The combined image goes at sector 64
(offset 32 KiB): it carries `idbloader` at its start and the U-Boot FIT at an offset
that lands on sector 16384.

```sh
# back up the current bootloader first
sudo dd if=/dev/mmcblk0 of=/root/mmcblk0-bootloader-backup.img bs=1M count=16

# flash
sudo dd if=u-boot-rockchip.bin of=/dev/mmcblk0 bs=32k seek=1 conv=notrunc
sync

# read back and verify against the source image
sudo dd if=/dev/mmcblk0 bs=512 skip=64 count=$(( $(stat -c%s u-boot-rockchip.bin) / 512 )) | sha256sum
sha256sum u-boot-rockchip.bin
```

Reboot, then confirm the boot source:

```sh
findmnt -no SOURCE,LABEL /     # armbi_nand = internal (loop), armbi_root = SD (no loop)
```

## Boot order & recovery

RK3308 BootROM device order is **NAND → eMMC → SPI → SD → USB**; it advances to the
next device only if the current loader is **damaged/unreadable**.

* A **botched / truncated** write to the internal flash → BootROM falls through to the
  SD card → the board stays reachable, reflash from there. The backup above restores
  the previous loader.
* A loader that **loads but hangs** has no fall-through → recover via **maskrom**
  (USB-OTG to a host + `rkdeveloptool`). A plain power-cycle will not fix that case,
  so flash with physical access to the board.

Tip: flash while booted from the SD card — it keeps the SD as a live, reachable
recovery system independent of the internal flash.

## Status

Verified end-to-end on real NAPI-C hardware (internal flash + SD, loopback on `end0`):

| condition            | boots            | result |
|----------------------|------------------|--------|
| loopback on `end0`   | internal (`armbi_nand`) | ✅ |
| no loopback          | SD (`armbi_root`)       | ✅ |

The same `loopdetect`-enabled U-Boot on the internal flash makes the choice on every
boot; switching the plug and rebooting flips the boot source repeatably.

## `legacy-sd-detector/` (superseded)

An earlier, SD-side approach, kept for reference. A Linux service on the SD polled
`end0` for the same loopback (`napi-loop-probe.py`, raw AF_PACKET, non-invasive) and,
on a confirmed loop, wrote a one-shot `NAND` flag to a raw SD sector that the SD's
`boot.scr` read to chainload the internal flash. This was replaced by the U-Boot
approach because it required a cooperating SD image; the internal-flash U-Boot now
decides on its own regardless of the SD's contents.
