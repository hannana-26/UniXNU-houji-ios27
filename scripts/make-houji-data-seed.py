#!/usr/bin/env python3
"""Build the houji Data seed for the CI payload step (macOS runner only).

The workflow pre_build step runs this before `unixnu build kernel` (cwd is the
UniXNU checkout, so ./unixnu and scripts/ resolve):

    git submodule update --init --depth 1 drivers/DarwinBoot
    python3 "$DEVICE_DIR/scripts/make-houji-data-seed.py" --out "$RUNNER_TEMP/data.dbr"

The Build payload step then passes $RUNNER_TEMP/data.dbr as --data-ramdisk. The
storage defconfig compiles CONFIG_XNU_OWNER_DATA_MOUNT=y, and the payload step
refuses to build without the seed; the kernel publishes its persistent Data
volume from it (RamBlock carries it as initrd, packed_data_ramdisk = false).

Steps, all official procedures: stream the pinned iPhone17,2 24A446 OS member
through `./unixnu ipsw fetch OS` (the cache is shared with the later payload
step), decrypt AEA exactly as scripts/unixnu/ipsw.py decrypt_aea does, attach
the OS volume read-only, build a first-install APFS Data seed with the official
scripts/make-data-seed.py, then pin the packed seed's volume UUID to the value
the kernel compiles (CONFIG_XNU_OWNER_DATA_VOLUME_UUID) with the set_volume_uuid
procedure of the official make-macos-data-seed.py. Because the UUID is pinned,
a seed regenerated in a later run stays the installation the kernel expects and
no kernel rebuild is ever triggered by a seed change.
"""
import argparse
import json
import os
import plistlib
import shutil
import struct
import subprocess
import sys
import tempfile
import uuid
from pathlib import Path

TREE = Path.cwd()
DATA_UUID = uuid.UUID('2A5E9027-D9E1-40F9-983C-41BD3F1994F5')
GIB = 8
BUILD = '24A446'


def log(message):
    print(f'houji-data-seed: {message}', flush=True)


def sha256_file(path):
    import hashlib
    digest = hashlib.sha256()
    with open(path, 'rb') as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b''):
            digest.update(chunk)
    return digest.hexdigest()


def decrypt_aea(path):
    """Official scripts/unixnu/ipsw.py decrypt_aea, same outputs and records.

    The key is fetched by `ipsw fw aea --key` from the key URL in the archive
    header and passed to macOS /usr/bin/aea; it is not written to disk or the log.
    """
    out = path.with_suffix('')
    record = out.with_suffix(out.suffix + '.json')
    if out.is_file() and record.is_file():
        data = json.loads(record.read_text())
        if data.get('aea_sha256') == json.loads(path.with_suffix(path.suffix + '.json').read_text()).get('sha256') \
                and data.get('size') == out.stat().st_size:
            return out
    if not Path('/usr/bin/aea').is_file():
        raise SystemExit('AEA decryption needs macOS /usr/bin/aea')
    ipsw = shutil.which('ipsw')
    if not ipsw:
        raise SystemExit('the ipsw tool is not on PATH (the workflow installs it before the pre-build steps)')
    log('fetching the AEA key')
    result = subprocess.run([ipsw, 'fw', 'aea', '--key', str(path)], capture_output=True, text=True)
    keys = [line.strip() for line in result.stdout.splitlines() if line.strip().startswith('base64:')]
    if result.returncode or len(keys) != 1:
        raise SystemExit(f'could not fetch the AEA key of {path.name}: {result.stderr.strip()[-300:]}')
    partial = out.with_suffix(out.suffix + '.partial')
    log(f'decrypting {path.name}')
    result = subprocess.run(['/usr/bin/aea', 'decrypt', '-i', str(path), '-o', str(partial), '-key-value', keys[0]],
                            capture_output=True, text=True)
    if result.returncode:
        partial.unlink(missing_ok=True)
        raise SystemExit(f'aea decrypt of {path.name} failed: {result.stderr.strip()[-300:]}')
    partial.rename(out)
    source = json.loads(path.with_suffix(path.suffix + '.json').read_text())
    record.write_text(json.dumps({'member': source.get('member'), 'aea_sha256': source.get('sha256'),
                                  'size': out.stat().st_size, 'sha256': sha256_file(out)}, indent=2) + '\n')
    return out


def fletcher64(data):
    """Official make-macos-data-seed.py fletcher64, verbatim."""
    lo = hi = 0
    for (word,) in struct.iter_unpack('<I', data):
        lo = (lo + word) % 0xFFFFFFFF
        hi = (hi + lo) % 0xFFFFFFFF
    c0 = 0xFFFFFFFF - ((lo + hi) % 0xFFFFFFFF)
    c1 = 0xFFFFFFFF - ((lo + c0) % 0xFFFFFFFF)
    return struct.pack('<II', c0, c1)


def pin_packed_volume_uuid(path, new):
    """Official set_volume_uuid procedure applied to the packed (DBRDISK1) seed.

    The packed format stores only nonzero pages (packed_ramdisk.regions), and
    the volume superblocks live in them, so the patch edits the extents in place
    and refreshes every checksummed superblock copy; header and extent table stay
    untouched and the file is never re-packed.
    """
    sys.path.insert(0, str(TREE / 'drivers/DarwinBoot/Test'))
    import packed_ramdisk
    data = bytearray(path.read_bytes())
    _, extents = packed_ramdisk.regions(bytes(data))
    cursor = 32 + 16 * len(extents)
    found = set()
    patched = 0
    for _, view in extents:
        base = cursor
        cursor += len(view)
        for at in range(0, len(view), 4096):
            start = base + at
            block = data[start:start + 4096]
            if bytes(block[32:36]) != b'APSB':
                continue
            size = struct.unpack_from('<I', block, 36)[0]
            if not 64 <= size <= 4096 or fletcher64(bytes(block[8:8 + size])) != bytes(block[0:8]):
                continue
            found.add(bytes(block[240:256]))
            if bytes(block[240:256]) != new.bytes:
                data[start + 240:start + 256] = new.bytes
                data[start:start + 8] = fletcher64(bytes(data[start + 8:start + size]))
                patched += 1
    if len(found) != 1:
        raise SystemExit(f'{path}: expected one seed volume UUID, found {len(found)}')
    path.write_bytes(data)
    log(f'pinned the seed volume UUID to {new} ({patched} volume superblock copies patched)')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True, help='packed seed to write ($RUNNER_TEMP/data.dbr)')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        raise SystemExit('the Data seed needs macOS (hdiutil, /usr/bin/aea)')
    work = Path(tempfile.mkdtemp(prefix='houji-seed-'))
    log('fetching the pinned OS member through ./unixnu ipsw fetch OS')
    extracted = subprocess.run(['./unixnu', 'ipsw', 'fetch', 'OS'], cwd=TREE, check=True,
                               capture_output=True, text=True).stdout.strip().splitlines()[-1].strip()
    image = Path(extracted)
    if image.suffix == '.aea':
        image = decrypt_aea(image)
    log(f'OS image: {image}')
    mount = work / 'os'
    mount.mkdir()
    attached = plistlib.loads(subprocess.check_output(
        ['hdiutil', 'attach', '-readonly', '-nobrowse', '-noverify', '-mountpoint', str(mount), '-plist', str(image)]))
    try:
        if not (mount / 'sbin/launchd').is_file() or not (mount / 'private/var').is_dir():
            raise SystemExit('the attached OS image is not an IPSW OS volume')
        seed = work / 'data.dbr'
        # seed-data-grow.py keeps its scratch image in a tempfile under the tree's build/
        # directory (the workflow's own seed path creates it the same way).
        (TREE / 'build').mkdir(exist_ok=True)
        log(f'building the first-install seed with the official make-data-seed.py (--gib {GIB})')
        subprocess.run([sys.executable, str(TREE / 'scripts/make-data-seed.py'), str(mount), str(seed),
                        '--gib', str(GIB), '--build', BUILD], check=True)
    finally:
        subprocess.run(['hdiutil', 'detach', attached['system-entities'][0]['dev-entry']], check=False)
    pin_packed_volume_uuid(seed, DATA_UUID)
    sys.path.insert(0, str(TREE / 'drivers/DarwinBoot/Test'))
    import packed_ramdisk
    metadata, _ = packed_ramdisk.inspect(seed)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.unlink(missing_ok=True)
    seed.replace(args.out)
    log(f"seed {args.out}: logical {metadata['bytes'] >> 20} MiB, encoded {metadata['encoded_bytes']} bytes, "
        f"sha256 {metadata['sha256'][:16]}..., volume UUID {DATA_UUID}")


if __name__ == '__main__':
    main()
