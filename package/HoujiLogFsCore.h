// SPDX-License-Identifier: BSD-2-Clause-Patent
// Houji logfs diagnostic-log core: FAT12 validation, the HJXNU001 record header, the
// header-last commit transaction and the AOSP bootloader_message (BCB) page.
//
// Ported from lonzzi-houji kernel/overlay/iokit/Kernel/houji-ufs-root/ (HoujiLogFile.h,
// HoujiLogCommit.h, HoujiRecoveryRecord.h; BSD-2-Clause-Patent), adapted to partition-relative
// 4096-byte blocks and this device's rebuilt logfs geometry. No allocation, no I/O and no
// kernel headers: the same file compiles inside the kernel (via BootLog.c) and on the host
// (work/test_logfs_core.c), so the kernel's checks and the host tool's checks cannot drift.
//
// logfs layout (one 4096-byte "page" = one FAT sector):
//   page 0        boot sector (BPB), 0x55AA at 510..511
//   page 1..2     the two FAT12 copies (1 sector each, byte-identical)
//   page 3..6     root directory, 512 entries of 32 bytes
//   page 7..      data area; cluster c lives at page 7 + c - 2
//   XNULOG.BIN    64 KiB, 16 preallocated clusters HOUJI_LOG_CLUSTERS (69..84, consecutive),
//                 payload 60 KiB behind a 4 KiB HJXNU001 header. The FAT and the directory are
//                 written once by the host builder (work/logfs-fat.py); the kernel never writes them.
#ifndef HOUJI_LOG_FS_CORE_H
#define HOUJI_LOG_FS_CORE_H

#include <stddef.h>
#include <stdint.h>

#define HOUJI_LOG_PAGE        4096u
#define HOUJI_LOG_META_PAGES  7u      /* boot sector + 2 FAT copies + 4 root-directory pages */
#define HOUJI_LOG_FILE_BYTES  65536u  /* XNULOG.BIN size: 1 header page + 60 KiB payload */
#define HOUJI_LOG_HEADER_PAGE HOUJI_LOG_PAGE
#define HOUJI_LOG_PAYLOAD_CAP (HOUJI_LOG_FILE_BYTES - HOUJI_LOG_PAGE)
#define HOUJI_LOG_CLUSTER_FIRST 69u
#define HOUJI_LOG_CLUSTER_COUNT 16u
#define HOUJI_LOG_PAGE_OF_CLUSTER(c) (HOUJI_LOG_META_PAGES + (c) - 2u)
#define HOUJI_LOG_FILE_FIRST_PAGE HOUJI_LOG_PAGE_OF_CLUSTER(HOUJI_LOG_CLUSTER_FIRST) /* 74 */
#define HOUJI_LOG_INITIAL_CRC   0xd7978eebU /* pristine XNULOG.BIN: 64 KiB of zeros (builder-written) */

/* CRC32 (IEEE 802.3, reflected, poly 0xEDB88320), identical to zlib.crc32. */
static uint32_t houji_crc32(const void *data, size_t bytes)
{
  static uint32_t table[256];
  static int built;
  if (!built) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320U ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    built = 1;
  }
  const uint8_t *p = (const uint8_t *)data;
  uint32_t c = 0xFFFFFFFFU;
  for (size_t i = 0; i < bytes; ++i) c = table[(c ^ p[i]) & 0xFFU] ^ (c >> 8);
  return c ^ 0xFFFFFFFFU;
}

static unsigned houji_le16(const uint8_t *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static uint32_t houji_le32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void houji_store32(uint8_t *p, uint32_t v)
{
  for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (i * 8));
}
static unsigned houji_fat_next(const uint8_t *fat, unsigned c)
{
  const unsigned p = houji_le16(fat + c + c / 2);
  return (c & 1) ? p >> 4 : p & 0xFFFU;
}

/* The 16 preallocated clusters of XNULOG.BIN (lonzzi r21 allocation, bound to this partition). */
static unsigned houji_log_cluster(unsigned i) { return HOUJI_LOG_CLUSTER_FIRST + i; }

/*
 * Validate the 7 metadata pages of logfs (boot sector, FAT copies, root directory) against the
 * geometry the builder (work/logfs-fat.py) wrote, and prove the reserved allocation still holds:
 * the chain consecutive, no other file overlapping it, the directory entry exactly once.
 * Returns nullptr when logfs holds our file, or the reason it does not.
 */
static const char *houji_logfs_fat_error(const uint8_t *m, unsigned n)
{
  if (!m || n != HOUJI_LOG_META_PAGES * HOUJI_LOG_PAGE)
    return "logfs metadata size";
  if (m[510] != 0x55 || m[511] != 0xAA)
    return "logfs boot signature";
  if (houji_le16(m + 11) != 4096 || m[13] != 1 || houji_le16(m + 14) != 1 || m[16] != 2 ||
      houji_le16(m + 17) != 512 || houji_le16(m + 19) != 1280 || m[21] != 0xF8 ||
      houji_le16(m + 22) != 1 || houji_le32(m + 32) != 0)
    return "logfs FAT12 geometry";
  const uint8_t *fat = m + HOUJI_LOG_PAGE;
  for (unsigned i = 0; i < HOUJI_LOG_PAGE; ++i)
    if (fat[i] != fat[HOUJI_LOG_PAGE + i]) return "logfs FAT copies differ";
  for (unsigned i = 0; i < HOUJI_LOG_CLUSTER_COUNT; ++i) {
    const unsigned c = houji_log_cluster(i);
    if (c < 2 || c >= 1275) return "log allocation bounds";
    for (unsigned j = 0; j < i; ++j)
      if (houji_log_cluster(j) == c) return "log allocation duplicate";
    const unsigned next = houji_fat_next(fat, c);
    if (i + 1 < HOUJI_LOG_CLUSTER_COUNT ? next != houji_log_cluster(i + 1) : next < 0xFF8U)
      return "native log FAT chain changed";
  }
  static const uint8_t name[11] = {'X', 'N', 'U', 'L', 'O', 'G', ' ', ' ', 'B', 'I', 'N'};
  unsigned found = 0;
  for (unsigned at = 3 * HOUJI_LOG_PAGE; at < HOUJI_LOG_META_PAGES * HOUJI_LOG_PAGE; at += 32) {
    const uint8_t *e = m + at;
    if (!e[0]) break;
    if (e[0] == 0xE5 || e[11] == 15 || (e[11] & 8)) continue;
    if ((e[11] & 0x10) || houji_le16(e + 20)) return "unexpected FAT directory/cluster";
    int own = 1;
    for (unsigned i = 0; i < 11; ++i) if (e[i] != name[i]) own = 0;
    const unsigned size = houji_le32(e + 28), start = houji_le16(e + 26);
    if (own) {
      if (++found != 1 || size != HOUJI_LOG_FILE_BYTES ||
          start != HOUJI_LOG_CLUSTER_FIRST || (e[11] & 1))
        return "native log directory entry changed";
      continue;
    }
    /* OEM log names may alias each other; none may alias our reserved file. */
    if (size > 1280U * HOUJI_LOG_PAGE) return "other log size outside FAT";
    unsigned c = start, count = (size + HOUJI_LOG_PAGE - 1) / HOUJI_LOG_PAGE;
    for (unsigned i = 0; i < count; ++i) {
      if (c < 2 || c >= 1275) return "other file chain malformed";
      for (unsigned j = 0; j < HOUJI_LOG_CLUSTER_COUNT; ++j)
        if (c == houji_log_cluster(j)) return "native log overlaps another file";
      c = houji_fat_next(fat, c);
    }
    if (count && c < 0xFF8U) return "other file chain malformed";
  }
  return found == 1 ? (const char *)0 : "native log missing";
}

/* The pristine preallocated file: 64 KiB of zeros, before the first commit. */
static int houji_logfs_initial_valid(const uint8_t *p, unsigned n)
{
  return p && n == HOUJI_LOG_FILE_BYTES && houji_crc32(p, n) == HOUJI_LOG_INITIAL_CRC;
}

/*
 * The record header: magic HJXNU001, format 1/1, the 16-byte run id of this boot, the payload
 * length and its CRC32, then the CRC32 of the first 4096 header bytes with this field still zero.
 */
static void houji_logfs_header(uint8_t *h, const uint8_t *payload, unsigned bytes,
                               const uint8_t run_id[16])
{
  for (unsigned i = 0; i < HOUJI_LOG_PAGE; ++i) h[i] = 0;
  static const uint8_t magic[8] = {'H', 'J', 'X', 'N', 'U', '0', '0', '1'};
  for (unsigned i = 0; i < 8; ++i) h[i] = magic[i];
  houji_store32(h + 8, 1);
  houji_store32(h + 12, 1);
  for (unsigned i = 0; i < 16; ++i) h[16 + i] = run_id[i];
  houji_store32(h + 32, bytes);
  houji_store32(h + 36, houji_crc32(payload, bytes));
  houji_store32(h + 40, houji_crc32(h, HOUJI_LOG_PAGE));
}

/* Header fields of a committed record; returns nullptr when the image is one of ours. */
static const char *houji_logfs_header_check(const uint8_t *image, unsigned *bytes, uint32_t *crc)
{
  static const uint8_t magic[8] = {'H', 'J', 'X', 'N', 'U', '0', '0', '1'};
  if (!image) return "log image missing";
  for (unsigned i = 0; i < 8; ++i)
    if (image[i] != magic[i]) return "log header magic";
  if (houji_le32(image + 8) != 1 || houji_le32(image + 12) != 1) return "log header version";
  const unsigned n = houji_le32(image + 32);
  if (n > HOUJI_LOG_PAYLOAD_CAP) return "log payload length";
  const uint32_t want = houji_le32(image + 36);
  if (houji_crc32(image + HOUJI_LOG_PAGE, n) != want) return "log payload CRC";
  uint8_t header[HOUJI_LOG_PAGE];
  for (unsigned i = 0; i < HOUJI_LOG_PAGE; ++i) header[i] = image[i];
  houji_store32(header + 40, 0);
  if (houji_crc32(header, HOUJI_LOG_PAGE) != houji_le32(image + 40)) return "log header CRC";
  if (bytes) *bytes = n;
  if (crc) *crc = want;
  return (const char *)0;
}

/*
 * Header-last single-record transaction (lonzzi HoujiLogCommit.h): the 15 payload pages first,
 * one flush, then the header page, one flush, then a byte-exact readback of all 16 pages.
 * Page numbers are file pages 0..15 (partition pages HOUJI_LOG_FILE_FIRST_PAGE..+15).
 */
typedef int (*houji_logfs_write_fn)(unsigned page, const uint8_t *src);
typedef int (*houji_logfs_flush_fn)(void);
typedef int (*houji_logfs_read_fn)(unsigned page, uint8_t *dst);

static const char *houji_logfs_commit(const uint8_t *image, unsigned bytes,
                                      houji_logfs_write_fn write, houji_logfs_flush_fn flush,
                                      houji_logfs_read_fn read)
{
  if (!image || bytes != HOUJI_LOG_FILE_BYTES || !write || !flush || !read)
    return "log commit arguments";
  for (unsigned i = 1; i < HOUJI_LOG_CLUSTER_COUNT; ++i)
    if (!write(i, image + i * HOUJI_LOG_PAGE)) return "log payload write failed";
  if (!flush()) return "log payload flush failed";
  if (!write(0, image)) return "log header commit failed";
  if (!flush()) return "log header flush failed";
  static uint8_t block[HOUJI_LOG_PAGE];
  for (unsigned i = 0; i < HOUJI_LOG_CLUSTER_COUNT; ++i) {
    if (!read(i, block)) return "committed log readback failed";
    for (unsigned j = 0; j < HOUJI_LOG_PAGE; ++j)
      if (block[j] != image[i * HOUJI_LOG_PAGE + j]) return "committed log bytes differ";
  }
  return (const char *)0;
}

/*
 * AOSP bootloader_message page (BCB, misc partition page 0): zero the command/status/recovery/
 * stage fields, write "boot-recovery" and "recovery", preserve reserved bytes and the adjacent
 * A/B control data (lonzzi HoujiRecoveryRecord.h).
 */
static int houji_logfs_recovery_record(uint8_t *page, unsigned n)
{
  if (!page || n != HOUJI_LOG_PAGE) return 0;
  for (unsigned i = 0; i < 864; ++i) page[i] = 0;
  static const char command[] = "boot-recovery", args[] = "recovery\n";
  for (unsigned i = 0; i < sizeof(command); ++i) page[i] = (uint8_t)command[i];
  for (unsigned i = 0; i < sizeof(args); ++i) page[64 + i] = (uint8_t)args[i];
  return 1;
}

#endif /* HOUJI_LOG_FS_CORE_H */
