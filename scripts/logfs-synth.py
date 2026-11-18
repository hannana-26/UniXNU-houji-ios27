#!/usr/bin/env python3
"""Synthesize a whole-partition logfs image (route W: full rebuild).

The device logfs partition ships as a factory FAT12 whose rotating UefiLog files occupy
the reserved XNULOG.BIN window (clusters 69..84; confirmed on device 2026-11-18, see
porting.toml diagnostics r26 evidence). scripts/logfs-fat.py build refuses that state
("reserved clusters 69..84 busy") because it edits a read-only backup in place. This
tool instead produces a whole-partition image from constants alone -- no device input,
no backup: factory FAT12 geometry, both FAT copies carrying the 69..84 chain, one
XNULOG.BIN directory entry, 64 KiB of zeroed record, everything else free/zero.

Flashing it REPLACES the partition contents: factory files are intentionally not
preserved and cannot be recovered (no backup channel exists on this device). Only run
after the user has approved the red-line extension and seen the exact flash command.
Validation here mirrors HoujiLogFsCore.h / logfs-fat.py fat_error(), so the kernel
(houji-logfs=1) and the UEFI reader accept the image without any code change.

Usage: logfs-synth.py OUT [PARTITION_BYTES]
  PARTITION_BYTES defaults to 8388608 (8 MiB, by-name logfs on houji; verify with
  fastboot getvar partition-size:logfs). The FAT volume stays 1280 pages
  (TotSec16=1280); pages beyond it are zero padding so the whole partition is
  deterministic. Verify the result with:
    logfs-fat.py scan OUT          -> allocation: valid (XNULOG.BIN present)
    logfs-fat.py decode OUT DIR    -> no committed record (pristine, CRC32 d7978eeb)
"""
import hashlib
import importlib.util
import json
import sys
from pathlib import Path

_spec = importlib.util.spec_from_file_location(
    "logfs_fat", Path(__file__).resolve().parent / "logfs-fat.py")
_lf = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_lf)

PAGE = _lf.PAGE
META_PAGES = _lf.META_PAGES
CLUSTER_FIRST = _lf.CLUSTER_FIRST
CLUSTER_COUNT = _lf.CLUSTER_COUNT
NAME = _lf.NAME
FILE_BYTES = _lf.FILE_BYTES
fat_set = _lf.fat_set
fat_error = _lf.fat_error


def synth_partition(total_bytes: int) -> bytearray:
    if total_bytes % PAGE or total_bytes < 1280 * PAGE:
        raise SystemExit(f"partition size must be a multiple of {PAGE} and cover the 1280-page volume")
    img = bytearray(total_bytes)
    img[510], img[511] = 0x55, 0xAA
    img[11:13] = (4096).to_bytes(2, "little")
    img[13] = 1
    img[14:16] = (1).to_bytes(2, "little")
    img[16] = 2
    img[17:19] = (512).to_bytes(2, "little")
    img[19:21] = (1280).to_bytes(2, "little")
    img[21] = 0xF8
    img[22:24] = (1).to_bytes(2, "little")
    for i in range(CLUSTER_COUNT):
        fat_set(img, CLUSTER_FIRST + i,
                CLUSTER_FIRST + i + 1 if i + 1 < CLUSTER_COUNT else 0xFFF)
    e = bytearray(32)
    e[:11] = NAME
    e[11] = 0x20                                   # archive; must not be readonly
    e[26:28] = CLUSTER_FIRST.to_bytes(2, "little")
    e[28:32] = FILE_BYTES.to_bytes(4, "little")
    img[3 * PAGE:3 * PAGE + 32] = e
    return img


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    out = Path(argv[1])
    total = int(argv[2], 0) if len(argv) > 2 else 8388608
    img = synth_partition(total)
    why = fat_error(bytes(img[:META_PAGES * PAGE]))
    if why is not None:
        print(f"refusing: synthesized metadata rejected: {why}")
        return 1
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(bytes(img))
    rec = {
        "file": "XNULOG.BIN", "size": FILE_BYTES,
        "clusters": list(range(CLUSTER_FIRST, CLUSTER_FIRST + CLUSTER_COUNT)),
        "volume_pages": 1280, "partition_bytes": total,
        "pristine_record_crc32": "d7978eeb",
        "sha256": hashlib.sha256(bytes(img)).hexdigest(),
        "note": "whole-volume rebuild; factory UefiLog files intentionally not preserved",
    }
    out.with_suffix(".synth.json").write_text(json.dumps(rec, indent=2) + "\n")
    print(f"synth {out} ({total} bytes, volume 1280 pages, XNULOG.BIN at cluster "
          f"{CLUSTER_FIRST}..{CLUSTER_FIRST + CLUSTER_COUNT - 1}); validate: ok")
    print(f"sha256 {rec['sha256']}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
