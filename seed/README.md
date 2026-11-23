# Pinned houji Data seed

`data.dbr` is the packed Data seed (DBRDISK1 / scratch-data.img) that the r60
provisioning boot (2026-11-23, data-reset=1) formatted the persistent store
window with. RamBlock binds the store overlay to the SEED CONTENT hash
(NativeRamBlock.cpp seedHash, not the pinned volume UUID), so every CI run that
regenerates a fresh seed invalidates the on-disk store (persist::Open::Kept and
the disk2 write fault ring returns). This file pins the seed bytes instead:

- sha256 803d9f6c53aacf0fe4a9b8de3abba6a37809e55acc62194953d20a3aae5a4e1c
- size 1101968 bytes (logical 8 GiB container, packed nonzero pages only)
- volume UUID 2A5E9027-D9E1-40F9-983C-41BD3F1994F5 (CONFIG_XNU_OWNER_DATA_VOLUME_UUID)
- provenance: extracted from build/payload-r60 (CI 45095246370) Payloads/scratch-data.img;
  identical bytes verified against that run's payload.json data_ramdisk_sha256

The CI pre_build copies this file instead of running make-houji-data-seed.py,
so the payload.json data_ramdisk_sha256 must always equal the sha above
(mk62.sh asserts it). To change the seed deliberately: replace this file,
re-provision the store on the device once (data-reset=1 boot), and update this
sha.
