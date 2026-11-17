#!/usr/bin/env python3
"""Houji logfs capture from recovery (root ADB, strictly read-only on the phone).

Mirrors lonzzi-houji tools/capture-recovery-logs.py's safety stance, minimal version:
  * explicit --serial and --adb-port; never restarts the ADB server, never mounts or wipes anything,
  * reads the whole logfs partition twice over exec-out dd and refuses results that differ,
  * decodes with work/logfs-fat.py (the same rules the kernel's HoujiLogFsCore.h enforces),
  * writes logfs.bin, XnuLog.txt and capture.json into a NEW output directory.

Usage:
  python3 work/capture-logfs-recovery.py --serial 6e0f97c8 --adb-port 5047 \
      --adb adb --output .local/logs/logfs-r1
Offline re-decode of a kept raw image:
  python3 work/logfs-fat.py decode .local/logs/logfs-r1/logfs.bin .local/logs/logfs-r1-re
"""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import logfs_fat  # noqa: E402

NODES = ["/dev/block/by-name/logfs", "/dev/block/bootdevice/by-name/logfs",
         "/dev/block/platform/soc/1d84000.ufshc/by-name/logfs"]


def run(cmd, timeout=60):
    return subprocess.run(cmd, capture_output=True, timeout=timeout)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--serial", required=True, help="the phone's fastboot/adb serial")
    ap.add_argument("--adb-port", type=int, required=True, help="adb server port (client -P)")
    ap.add_argument("--adb", default="adb", help="path to adb")
    ap.add_argument("--output", required=True, help="output directory; must not exist")
    ap.add_argument("--decode/--no-decode", dest="decode", default=True)
    args = ap.parse_args()

    out = Path(args.output)
    if out.exists():
        print(f"refusing: {out} exists (outputs are never merged)")
        return 1
    base = [args.adb, "-P", str(args.adb_port), "-s", args.serial]

    r = run(base + ["shell", "id"])
    if r.returncode != 0 or b"uid=0" not in r.stdout:
        print(f"refusing: root adb required ({r.stdout[:120]!r} {r.stderr[:120]!r})")
        return 1
    node = None
    for cand in NODES:
        r = run(base + ["shell", f"ls -l {cand}"])
        if r.returncode == 0 and b"No such file" not in r.stdout:
            node = cand
            break
    if not node:
        print(f"refusing: logfs node not found among {NODES}")
        return 1
    r = run(base + ["shell", f"blockdev --getsize64 {node}"])
    size = int(r.stdout.strip() or 0) if r.returncode == 0 else 0
    if not (1024 * 1024 <= size <= 64 * 1024 * 1024):
        print(f"refusing: logfs size {size} outside 1..64 MiB")
        return 1

    reads = []
    for attempt in (1, 2):
        r = subprocess.run(base + ["exec-out", f"dd if={node} bs=4096 2>/dev/null"],
                           capture_output=True, timeout=120)
        if r.returncode != 0 or len(r.stdout) != size:
            print(f"refusing: read {attempt} returned {len(r.stdout)} of {size} bytes "
                  f"(rc={r.returncode})")
            return 1
        reads.append(r.stdout)
    if reads[0] != reads[1]:
        print("refusing: the two reads differ; the partition is not stable")
        return 1
    raw = reads[0]
    out.mkdir(parents=True)
    (out / "logfs.bin").write_bytes(raw)
    record = {"serial": args.serial, "node": node, "bytes": size,
              "sha256": hashlib.sha256(raw).hexdigest(),
              "reads_agree": True, "decode": None, "error": None}
    if args.decode:
        why = logfs_fat.fat_error(raw[:logfs_fat.META_PAGES * logfs_fat.PAGE])
        if why:
            record["error"] = why
            print(f"allocation invalid: {why} (raw image kept)")
        else:
            image = raw[logfs_fat.FILE_FIRST_PAGE * logfs_fat.PAGE:
                        (logfs_fat.FILE_FIRST_PAGE + logfs_fat.CLUSTER_COUNT) * logfs_fat.PAGE]
            hdr, err = logfs_fat.header_check(image)
            if hdr is None:
                record["error"] = err
                record["pristine"] = hashlib.sha256(image).hexdigest()
                print(f"no committed record: {err} (raw image kept)")
            else:
                text = hdr["payload"].decode("utf-8", "replace")
                (out / "XnuLog.txt").write_text(text)
                record["decode"] = {"run_id": hdr["run_id"], "bytes": hdr["bytes"],
                                    "crc": hdr["crc"]}
                print(f"decoded {hdr['bytes']} bytes (run {hdr['run_id']}) -> {out}/XnuLog.txt")
    (out / "capture.json").write_text(json.dumps(record, indent=2) + "\n")
    print(f"captured {size} bytes -> {out}/logfs.bin (sha256 {record['sha256'][:16]}…)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
