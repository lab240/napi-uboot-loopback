#!/bin/bash
# NAPI SD-side boot selector (periodic, low-impact).
# Periodically probes the loop interface(s) for a single-port L2 loopback using a
# non-invasive raw-frame echo (see napi-loop-probe.py; no IP changes). On a
# confirmed loopback -> write the U-Boot one-shot NAND flag and reboot to eMMC.
# Runs only when booted from the SD (root LABEL armbi_root).
set -u

LOG(){ echo "napi-loop-detect: $*"; command -v logger >/dev/null 2>&1 && logger -t napi-loop-detect "$*"; }
VLOG(){ [ "${VERBOSE:-0}" = "1" ] && LOG "$*"; return 0; }

# ---- defaults (override in /etc/default/napi-loop-detect) ----
LOOP_IFS="end0"
FLAG_SECTOR=30000
EMMC_LABEL="armbi_emmc"
PROBE="/usr/local/sbin/napi-loop-probe.py"
CHECK_INTERVALS="60 60 60 60 60 300 300 300 900"   # seconds between checks; last repeats
PROBE_LISTEN="1.0"
CONFIRM=2                                           # consecutive positive probes before acting
VERBOSE=0
[ -r /etc/default/napi-loop-detect ] && . /etc/default/napi-loop-detect
export PROBE_LISTEN

# ---- guard: only act when booted from the SD ----
ROOTLBL=$(findmnt -no LABEL / 2>/dev/null)
[ "$ROOTLBL" = "armbi_root" ] || { LOG "not on SD (root label='$ROOTLBL'); exiting"; exit 0; }

ROOTSRC=$(findmnt -no SOURCE /)
SDDISK=$(lsblk -no PKNAME "$ROOTSRC" 2>/dev/null | head -1)
[ -b "/dev/$SDDISK" ] || { LOG "cannot determine SD disk from '$ROOTSRC'; exiting"; exit 0; }
FLAGDEV="/dev/$SDDISK"

command -v python3 >/dev/null 2>&1 || { LOG "python3 missing; exiting"; exit 0; }
[ -r "$PROBE" ] || { LOG "probe missing ($PROBE); exiting"; exit 0; }

emmc_bootable(){
  local dev mp=/run/napi-emmc-chk rc=1
  dev=$(blkid -L "$EMMC_LABEL" 2>/dev/null)
  [ -z "$dev" ] && { LOG "eMMC ($EMMC_LABEL) not found"; return 1; }
  mkdir -p "$mp"
  mount -o ro "$dev" "$mp" 2>/dev/null && { [ -f "$mp/boot/boot.scr" ] && rc=0; umount "$mp" 2>/dev/null; }
  rmdir "$mp" 2>/dev/null
  [ $rc -eq 0 ] || LOG "eMMC present but no /boot/boot.scr (not bootable)"
  return $rc
}

act(){
  if ! emmc_bootable; then LOG "loopback on $1 but eMMC not bootable; staying on SD (no reboot)"; return 1; fi
  LOG "loopback CONFIRMED on $1; one-shot NAND -> $FLAGDEV sector $FLAG_SECTOR; rebooting to eMMC"
  printf NAND | dd of="$FLAGDEV" bs=512 seek="$FLAG_SECTOR" conv=sync,notrunc 2>/dev/null
  sync
  systemctl reboot
  exit 0
}

read -r -a IVALS <<< "$CHECK_INTERVALS"
LOG "started (ifaces='$LOOP_IFS' intervals='$CHECK_INTERVALS' confirm=$CONFIRM verbose=$VERBOSE)"
idx=0; hits=0; hitif=""
while :; do
  loop_if=""
  for IF in $LOOP_IFS; do
    [ -e "/sys/class/net/$IF" ] || { VLOG "$IF: absent"; continue; }
    if [ "$(cat /sys/class/net/$IF/carrier 2>/dev/null)" != "1" ]; then VLOG "$IF: no carrier"; continue; fi
    if python3 "$PROBE" "$IF"; then loop_if="$IF"; break; fi
    VLOG "$IF: no loopback"
  done
  if [ -n "$loop_if" ]; then
    if [ "$loop_if" = "$hitif" ]; then hits=$((hits+1)); else hits=1; hitif="$loop_if"; fi
    LOG "$loop_if: loopback echo (streak $hits/$CONFIRM)"
    [ "$hits" -ge "$CONFIRM" ] && act "$loop_if"
  else
    hits=0; hitif=""
  fi
  if [ "$idx" -lt "${#IVALS[@]}" ]; then slp="${IVALS[$idx]}"; else slp="${IVALS[$((${#IVALS[@]}-1))]}"; fi
  idx=$((idx+1))
  sleep "$slp"
done
