#!/usr/bin/env python3
"""Houji logfs FAT12 builder / validator / decoder (host side of package/HoujiLogFsCore.h).

The kernel (BootLog.c houji_logfs section) validates the same geometry with the same rules from
HoujiLogFsCore.h; this tool prepares the partition once from a read-only backup and decodes the
records the kernel commits. Never run against a partition that has not been backed up first.

Subcommands:
  selftest                  synthetic-image round-trip (no device input needed)
  scan FILE                 report geometry, directory entries and who owns clusters 69..84
  build IN OUT              add the preallocated XNULOG.BIN to a logfs backup image (FAT chain
                            69..84, one directory entry, 64 KiB of zeros); refuses anything else
  decode FILE OUTDIR        validate + extract the newest committed record (XnuLog.txt + json)
"""
import json
import sys
import zlib
from pathlib import Path

PAGE = 4096
META_PAGES = 7
FILE_BYTES = 65536
FILE_FIRST_PAGE = 74          # 7 + 69 - 2
CLUSTER_FIRST = 69
CLUSTER_COUNT = 16
INITIAL_CRC = 0xD7978EEB      # CRC32 of 64 KiB of zeros
MAGIC = b"HJXNU001"
NAME = b"XNULOG  BIN"         # 8.3: XNULOG + two spaces + BIN


def fat_next(fat: bytes, c: int) -> int:
    w = fat[c + c // 2] | (fat[c + c // 2 + 1] << 8)
    return (w >> 4) if (c & 1) else (w & 0xFFF)


def le16(b, o): return b[o] | (b[o + 1] << 8)


def le32(b, o): return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)


def fat_set(img: bytearray, c: int, nxt: int) -> None:
    """Write one FAT12 entry in both copies (offset PAGE and 2*PAGE)."""
    at = c + c // 2
    for base in (PAGE, 2 * PAGE):
        if c & 1:
            img[base + at] = (img[base + at] & 0x0F) | ((nxt & 0x0F) << 4)
            img[base + at + 1] = (nxt >> 4) & 0xFF
        else:
            img[base + at] = nxt & 0xFF
            img[base + at + 1] = (img[base + at + 1] & 0xF0) | ((nxt >> 8) & 0x0F)


def fat_error(meta: bytes) -> "str | None":
    """Mirror of houji_logfs_fat_error() over the 7 metadata pages."""
    if len(meta) != META_PAGES * PAGE:
        return "logfs metadata size"
    if meta[510] != 0x55 or meta[511] != 0xAA:
        return "logfs boot signature"
    if (le16(meta, 11) != 4096 or meta[13] != 1 or le16(meta, 14) != 1 or meta[16] != 2 or
            le16(meta, 17) != 512 or le16(meta, 19) != 1280 or meta[21] != 0xF8 or
            le16(meta, 22) != 1 or le32(meta, 32) != 0):
        return "logfs FAT12 geometry"
    fat = meta[PAGE:2 * PAGE]
    if fat != meta[2 * PAGE:3 * PAGE]:
        return "logfs FAT copies differ"
    for i in range(CLUSTER_COUNT):
        c = CLUSTER_FIRST + i
        if c < 2 or c >= 1275:
            return "log allocation bounds"
        nxt = fat_next(fat, c)
        want = CLUSTER_FIRST + i + 1 if i + 1 < CLUSTER_COUNT else None
        if want is None and nxt < 0xFF8:
            return "native log FAT chain changed"
        if want is not None and nxt != want:
            return "native log FAT chain changed"
    found = 0
    for at in range(3 * PAGE, META_PAGES * PAGE, 32):
        e = meta[at:at + 32]
        if e[0] == 0:
            break
        if e[0] == 0xE5 or e[11] == 15 or (e[11] & 8):
            continue
        if (e[11] & 0x10) or le16(e, 20):
            return "unexpected FAT directory/cluster"
        own = bytes(e[:11]) == NAME
        size, start = le32(e, 28), le16(e, 26)
        if own:
            found += 1
            if found != 1 or size != FILE_BYTES or start != CLUSTER_FIRST or (e[11] & 1):
                return "native log directory entry changed"
            continue
        if size > 1280 * PAGE:
            return "other log size outside FAT"
        c, count = start, (size + PAGE - 1) // PAGE
        for _ in range(count):
            if c < 2 or c >= 1275:
                return "other file chain malformed"
            if CLUSTER_FIRST <= c < CLUSTER_FIRST + CLUSTER_COUNT:
                return "native log overlaps another file"
            c = fat_next(fat, c)
        if count and c < 0xFF8:
            return "other file chain malformed"
    return None if found == 1 else "native log missing"


def header_check(image: bytes):
    """Mirror of houji_logfs_header_check(). Returns (dict, None) or (None, reason)."""
    if image[:8] != MAGIC:
        return None, "log header magic"
    if le32(image, 8) != 1 or le32(image, 12) != 1:
        return None, "log header version"
    n = le32(image, 32)
    if n > FILE_BYTES - PAGE:
        return None, "log payload length"
    crc = le32(image, 36)
    if zlib.crc32(image[PAGE:PAGE + n]) != crc:
        return None, "log payload CRC"
    hdr = bytearray(image[:PAGE])
    hdr[40:44] = b"\0\0\0\0"
    if zlib.crc32(bytes(hdr)) != le32(image, 40):
        return None, "log header CRC"
    return {"bytes": n, "crc": f"{crc:08x}",
            "run_id": image[16:32].hex(), "payload": image[PAGE:PAGE + n]}, None


def dir_entries(meta: bytes):
    rows = []
    for at in range(3 * PAGE, META_PAGES * PAGE, 32):
        e = meta[at:at + 32]
        if e[0] == 0:
            break
        if e[0] == 0xE5 or e[11] == 15:
            continue
        rows.append({"name": bytes(e[:11]).decode("ascii", "replace").rstrip(),
                     "attr": e[11], "start": le16(e, 26), "size": le32(e, 28)})
    return rows


def cluster_owners(meta: bytes):
    fat = meta[PAGE:2 * PAGE]
    owners = {c: [] for c in range(CLUSTER_FIRST, CLUSTER_FIRST + CLUSTER_COUNT)}
    for row in dir_entries(meta):
        if row["name"] == "XNULOG BIN":
            continue
        c, count = row["start"], (row["size"] + PAGE - 1) // PAGE
        for _ in range(count):
            if c in owners:
                owners[c].append(row["name"])
            if not (2 <= c < 1275):
                break
            c = fat_next(fat, c)
    return owners


def synth_image() -> bytearray:
    """A minimal valid logfs: geometry boot sector, empty FAT copies, one OEM file, full 1280 pages."""
    img = bytearray(1280 * PAGE)
    img[510], img[511] = 0x55, 0xAA
    img[11:13] = (4096).to_bytes(2, "little")
    img[13] = 1
    img[14:16] = (1).to_bytes(2, "little")
    img[16] = 2
    img[17:19] = (512).to_bytes(2, "little")
    img[19:21] = (1280).to_bytes(2, "little")
    img[21] = 0xF8
    img[22:24] = (1).to_bytes(2, "little")
    fat_set(img, 2, 3)
    fat_set(img, 3, 4)
    fat_set(img, 4, 0xFFF)
    e = bytearray(img[3 * PAGE:3 * PAGE + 32])
    e[:11] = b"UEFILOG SYS"
    e[26:28] = (2).to_bytes(2, "little")
    e[28:32] = (3 * PAGE).to_bytes(4, "little")
    img[3 * PAGE:3 * PAGE + 32] = e
    return img


def cmd_scan(path: Path) -> int:
    raw = path.read_bytes()
    meta = raw[:META_PAGES * PAGE]
    print(f"image: {len(raw)} bytes ({len(raw) // PAGE} pages)")
    why = fat_error(meta)
    print("allocation:", why or "valid (XNULOG.BIN present)")
    if why and "native log" not in why and why != "native log missing":
        print("geometry check failed above; not touching this image")
        return 1
    for row in dir_entries(meta):
        print(f"  {row['name']!r:24} attr={row['attr']:#04x} start={row['start']} size={row['size']}")
    owners = cluster_owners(meta)
    busy = {c: v for c, v in owners.items() if v}
    print("reserved clusters 69..84:", "free" if not busy else f"BUSY {busy}")
    return 0


def cmd_build(src: Path, dst: Path) -> int:
    img = bytearray(src.read_bytes())
    meta = bytes(img[:META_PAGES * PAGE])
    why = fat_error(meta)
    if why in ("native log missing", "native log FAT chain changed"):
        pass  # our allocation absent (no chain yet, or chain left for us to write): build it
    elif why is None:
        print("already built (XNULOG.BIN present and valid); nothing to do")
        return 0
    else:
        print(f"refusing: {why}")
        return 1
    owners = cluster_owners(meta)
    busy = {c: v for c, v in owners.items() if v}
    if busy:
        print(f"refusing: reserved clusters 69..84 busy: {busy}")
        return 1
    slot = next((off for off in range(3 * PAGE, META_PAGES * PAGE, 32)
                 if img[off] == 0 or img[off] == 0xE5), None)
    if slot is None:
        print("refusing: no free root directory slot")
        return 1
    for i in range(CLUSTER_COUNT):
        fat_set(img, CLUSTER_FIRST + i,
                CLUSTER_FIRST + i + 1 if i + 1 < CLUSTER_COUNT else 0xFFF)
    e = bytearray(32)
    e[:11] = NAME
    e[11] = 0x20                                   # archive; must not be readonly
    e[26:28] = (CLUSTER_FIRST).to_bytes(2, "little")
    e[28:32] = FILE_BYTES.to_bytes(4, "little")
    img[slot:slot + 32] = e
    first = (FILE_FIRST_PAGE - META_PAGES + 2 - 2 + 1)  # informational only
    del first
    dst.write_bytes(bytes(img))
    import hashlib
    rec = {"source": str(src), "sha256_before": hashlib.sha256(src.read_bytes()).hexdigest(),
           "sha256_after": hashlib.sha256(bytes(img)).hexdigest(),
           "file": "XNULOG.BIN", "size": FILE_BYTES,
           "clusters": list(range(CLUSTER_FIRST, CLUSTER_FIRST + CLUSTER_COUNT)),
           "pages": list(range(FILE_FIRST_PAGE, FILE_FIRST_PAGE + CLUSTER_COUNT))}
    dst.with_suffix(".build.json").write_text(json.dumps(rec, indent=2) + "\n")
    check = fat_error(bytes(img[:META_PAGES * PAGE]))
    print(f"built {dst} (cluster {CLUSTER_FIRST}..{CLUSTER_FIRST + CLUSTER_COUNT - 1}, "
          f"pages {FILE_FIRST_PAGE}..{FILE_FIRST_PAGE + CLUSTER_COUNT - 1}); validate: {check or 'ok'}")
    return 0 if check is None else 1


def cmd_decode(src: Path, outdir: Path) -> int:
    raw = src.read_bytes()
    meta = raw[:META_PAGES * PAGE]
    why = fat_error(meta)
    if why:
        print(f"refusing: {why}")
        return 1
    image = raw[FILE_FIRST_PAGE * PAGE:(FILE_FIRST_PAGE + CLUSTER_COUNT) * PAGE]
    hdr, err = header_check(image)
    if hdr is None:
        print(f"no committed record: {err}")
        print(f"pristine check: crc32(file)={zlib.crc32(image):08x} "
              f"(initial {INITIAL_CRC:08x})")
        return 2
    outdir.mkdir(parents=True, exist_ok=False)
    text = hdr["payload"].decode("utf-8", "replace")
    (outdir / "XnuLog.txt").write_text(text)
    (outdir / "capture.json").write_text(json.dumps(
        {"source": str(src), "run_id": hdr["run_id"], "bytes": hdr["bytes"],
         "crc": hdr["crc"], "sha256": __import__("hashlib").sha256(raw).hexdigest()}, indent=2) + "\n")
    print(f"decoded {hdr['bytes']} bytes (run {hdr['run_id']}, crc {hdr['crc']}) -> {outdir}/XnuLog.txt")
    return 0


def cmd_selftest() -> int:
    img = synth_image()
    p = Path("/tmp/logfs-selftest.img")
    p.write_bytes(bytes(img))
    rc = cmd_build(p, Path("/tmp/logfs-built.img"))
    if rc:
        return rc
    built = Path("/tmp/logfs-built.img").read_bytes()
    if fat_error(bytes(built[:META_PAGES * PAGE])) is not None:
        print("selftest: built image rejected")
        return 1
    # Kernel-side first-commit image: banner + payload, header, then decode round-trip.
    image = bytearray(FILE_BYTES)
    payload = b"#HJXLOG t=42s tag=selftest rounds=5 slots=1 state=2 msg=11\nhello world"
    image[PAGE:PAGE + len(payload)] = payload
    hdr = bytearray(PAGE)
    hdr[:8] = MAGIC
    hdr[8:12] = (1).to_bytes(4, "little")
    hdr[12:16] = (1).to_bytes(4, "little")
    hdr[16:32] = b"\x11" * 16
    hdr[32:36] = len(payload).to_bytes(4, "little")
    hdr[36:40] = zlib.crc32(payload).to_bytes(4, "little")
    hdr[40:44] = b"\0\0\0\0"
    hdr[40:44] = zlib.crc32(bytes(hdr)).to_bytes(4, "little")
    image[:PAGE] = hdr
    full = bytearray(built)
    full[FILE_FIRST_PAGE * PAGE:(FILE_FIRST_PAGE + CLUSTER_COUNT) * PAGE] = image
    p2 = Path("/tmp/logfs-committed.img")
    p2.write_bytes(bytes(full))
    out = Path("/tmp/logfs-decoded")
    import shutil
    shutil.rmtree(out, ignore_errors=True)
    rc = cmd_decode(p2, out)
    if rc == 0:
        got = (out / "XnuLog.txt").read_bytes()
        print("selftest decode text:", got[:80])
        print("SELFTEST PASS" if got == payload else "SELFTEST MISMATCH")
        return 0 if got == payload else 1
    return rc


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd, rest = argv[1], argv[2:]
    if cmd == "selftest":
        return cmd_selftest()
    if cmd == "scan" and len(rest) == 1:
        return cmd_scan(Path(rest[0]))
    if cmd == "build" and len(rest) == 2:
        return cmd_build(Path(rest[0]), Path(rest[1]))
    if cmd == "decode" and len(rest) == 2:
        return cmd_decode(Path(rest[0]), Path(rest[1]))
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
