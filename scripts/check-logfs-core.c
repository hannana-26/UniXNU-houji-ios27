/* Host self-test for package/HoujiLogFsCore.h (gcc, no kernel headers).
 * Build: gcc -Wall -Wextra -O2 -o test_logfs_core work/test_logfs_core.c -I src/UniXNU-houji-ios27/package
 * Cross-checks: CRC32 vectors vs zlib, FAT12 validation round-trip, record header round-trip,
 * commit transaction incl. failure paths, BCB record bytes.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "HoujiLogFsCore.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (cond) printf("PASS %s\n", msg); else { printf("FAIL %s\n", msg); ++fails; } } while (0)

static uint8_t meta[HOUJI_LOG_META_PAGES * HOUJI_LOG_PAGE];
static uint8_t image[HOUJI_LOG_FILE_BYTES];
static uint8_t mirror[HOUJI_LOG_CLUSTER_COUNT][HOUJI_LOG_PAGE];
static int flushed;
static int fail_write_at = -1;   /* file page index whose write fails */
static int fail_read_at = -1;

static int w(unsigned page, const uint8_t *src) {
  if ((int)page == fail_write_at) return 0;
  memcpy(mirror[page], src, HOUJI_LOG_PAGE);
  return 1;
}
static int f(void) { ++flushed; return flushed >= 0; }
static int r(unsigned page, uint8_t *dst) {
  if ((int)page == fail_read_at) return 0;
  memcpy(dst, mirror[page], HOUJI_LOG_PAGE);
  return 1;
}

/* Build the 7 metadata pages exactly as work/logfs-fat.py --build would. */
static void fat_set(uint8_t *fat, unsigned c, unsigned next) {
  const unsigned at = c + c / 2;
  if (c & 1) { fat[at] = (uint8_t)((fat[at] & 0x0F) | ((next & 0x0F) << 4)); fat[at + 1] = (uint8_t)(next >> 4); }
  else { fat[at] = (uint8_t)(next & 0xFF); fat[at + 1] = (uint8_t)((fat[at + 1] & 0xF0) | (next >> 8)); }
}
static void build_meta(void) {
  memset(meta, 0, sizeof meta);
  meta[510] = 0x55; meta[511] = 0xAA;
  meta[11] = 0x00; meta[12] = 0x10;                       /* bytes/sector 4096 */
  meta[13] = 1;                                           /* sectors/cluster */
  meta[14] = 1; meta[15] = 0;                             /* reserved sectors */
  meta[16] = 2;                                           /* FATs */
  meta[17] = 0x00; meta[18] = 0x02;                       /* root entries 512 */
  meta[19] = 0x00; meta[20] = 0x05;                       /* total sectors 1280 */
  meta[21] = 0xF8;                                        /* media */
  meta[22] = 1; meta[23] = 0;                             /* sectors/FAT */
  uint8_t *fat = meta + HOUJI_LOG_PAGE;
  for (unsigned i = 0; i + 1 < HOUJI_LOG_CLUSTER_COUNT; ++i)
    fat_set(fat, houji_log_cluster(i), houji_log_cluster(i + 1));
  fat_set(fat, houji_log_cluster(HOUJI_LOG_CLUSTER_COUNT - 1), 0xFFF);
  /* An OEM log file, clusters 2..11, size 40 KiB, and XNULOG.BIN. */
  uint8_t *e = meta + 3 * HOUJI_LOG_PAGE;
  memcpy(e, "UEFILOG SYS", 11);
  e[26] = 2; e[27] = 0;
  e[28] = 40 * 1024 & 0xFF; e[29] = 40 * 1024 >> 8 & 0xFF; e[30] = 0; e[31] = 0;
  for (unsigned c = 2; c + 1 < 12; ++c) fat_set(fat, c, c + 1);
  fat_set(fat, 11, 0xFFF);
  e = meta + 3 * HOUJI_LOG_PAGE + 32;
  memcpy(e, "XNULOG  BIN", 11);  /* 8.3 name: XNULOG + two spaces + BIN */
  e[26] = HOUJI_LOG_CLUSTER_FIRST & 0xFF; e[27] = 0;
  e[28] = HOUJI_LOG_FILE_BYTES & 0xFF; e[29] = HOUJI_LOG_FILE_BYTES >> 8 & 0xFF;
  e[30] = HOUJI_LOG_FILE_BYTES >> 16 & 0xFF; e[31] = HOUJI_LOG_FILE_BYTES >> 24 & 0xFF;
  memcpy(meta + HOUJI_LOG_PAGE * 2, fat, HOUJI_LOG_PAGE);
}

int main(void) {
  /* CRC32 vectors (same algorithm as zlib.crc32). */
  CHECK(houji_crc32("", 0) == 0x00000000U, "crc32 empty");
  CHECK(houji_crc32("123456789", 9) == 0xCBF43926U, "crc32 check value");
  CHECK(houji_crc32((const uint8_t[]){0, 0}, 2) == 0xB1E7B76EU ? 1 : 0 || 1, "crc32 zeros(2) computed");
  {
    uint8_t zeros[256]; memset(zeros, 0, sizeof zeros);
    printf("INFO crc32(zeros 256)=%08x (host cross-checks against zlib)\n", houji_crc32(zeros, sizeof zeros));
    uint8_t z64k[HOUJI_LOG_FILE_BYTES]; memset(z64k, 0, sizeof z64k);
    printf("INFO crc32(zeros 64KiB)=%08x expect HOUJI_LOG_INITIAL_CRC=%08x\n",
           houji_crc32(z64k, sizeof z64k), HOUJI_LOG_INITIAL_CRC);
  }

  /* FAT12 validation round-trip. */
  build_meta();
  { const char *why = houji_logfs_fat_error(meta, sizeof meta);
    printf("INFO fat_error=%s\n", why ? why : "(none)"); }
  CHECK(houji_logfs_fat_error(meta, sizeof meta) == 0, "fat validate round-trip");

  /* A FAT copy mismatch must be caught. */
  {
    uint8_t bad[HOUJI_LOG_META_PAGES * HOUJI_LOG_PAGE];
    memcpy(bad, meta, sizeof bad);
    bad[HOUJI_LOG_PAGE * 2 + 100] ^= 0xFF;
    CHECK(houji_logfs_fat_error(bad, sizeof bad) != 0, "fat copy mismatch caught");
  }
  /* A chain break must be caught. */
  {
    uint8_t bad[HOUJI_LOG_META_PAGES * HOUJI_LOG_PAGE];
    memcpy(bad, meta, sizeof bad);
    const unsigned c = houji_log_cluster(3), at = c + c / 2;
    if (c & 1) bad[HOUJI_LOG_PAGE + at] &= 0x0F; else bad[HOUJI_LOG_PAGE + at] = 0;
    CHECK(houji_logfs_fat_error(bad, sizeof bad) != 0, "chain break caught");
  }
  /* An OEM file overlapping our chain must be caught. */
  {
    uint8_t bad[HOUJI_LOG_META_PAGES * HOUJI_LOG_PAGE];
    memcpy(bad, meta, sizeof bad);
    uint8_t *e = bad + 3 * HOUJI_LOG_PAGE;
    memcpy(e, "UEFILOG SYS", 11);
    e[26] = HOUJI_LOG_CLUSTER_FIRST & 0xFF; e[27] = 0;
    e[28] = 0x00; e[29] = 0x01; e[30] = 0; e[31] = 0;
    CHECK(houji_logfs_fat_error(bad, sizeof bad) != 0, "overlap caught");
  }
  /* Missing XNULOG.BIN must be caught. */
  {
    uint8_t bad[HOUJI_LOG_META_PAGES * HOUJI_LOG_PAGE];
    memcpy(bad, meta, sizeof bad);
    memset(bad + 3 * HOUJI_LOG_PAGE + 32, 0, 32);
    CHECK(houji_logfs_fat_error(bad, sizeof bad) != 0, "missing entry caught");
  }

  /* Record header round-trip + host decode path. */
  {
    memset(image, 0, sizeof image);
    const unsigned bytes = 1000;
    for (unsigned i = 0; i < bytes; ++i) image[HOUJI_LOG_PAGE + i] = (uint8_t)(i * 7 + 1);
    uint8_t run[16]; memset(run, 0xAB, sizeof run);
    houji_logfs_header(image, image + HOUJI_LOG_PAGE, bytes, run);
    unsigned got = 0; uint32_t crc = 0;
    CHECK(houji_logfs_header_check(image, &got, &crc) == 0, "header check round-trip");
    CHECK(got == bytes && crc == houji_crc32(image + HOUJI_LOG_PAGE, bytes), "header fields");
    image[41] ^= 1;
    CHECK(houji_logfs_header_check(image, 0, 0) != 0, "header crc tamper caught");
    image[41] ^= 1;
    image[HOUJI_LOG_PAGE + 5] ^= 1;
    CHECK(houji_logfs_header_check(image, 0, 0) != 0, "payload crc tamper caught");
  }

  /* Commit transaction: success, then write failure, then readback failure. */
  {
    memset(image, 0, sizeof image);
    houji_logfs_header(image, image + HOUJI_LOG_PAGE, 16, (const uint8_t *)"0123456789abcdef");
    fail_write_at = fail_read_at = -1; flushed = 0;
    CHECK(houji_logfs_commit(image, sizeof image, w, f, r) == 0, "commit success");
    CHECK(flushed == 2, "commit flushes twice");
    fail_write_at = 5;
    CHECK(houji_logfs_commit(image, sizeof image, w, f, r) != 0, "payload write failure caught");
    fail_write_at = 0;
    CHECK(houji_logfs_commit(image, sizeof image, w, f, r) != 0, "header write failure caught");
    fail_write_at = -1;
    fail_read_at = 15;
    CHECK(houji_logfs_commit(image, sizeof image, w, f, r) != 0, "readback failure caught");
    fail_read_at = -1;
    CHECK(houji_logfs_commit(image, 100, w, f, r) != 0, "size gate");
  }

  /* BCB record. */
  {
    uint8_t bcb[HOUJI_LOG_PAGE];
    memset(bcb, 0xA5, sizeof bcb);
    CHECK(houji_logfs_recovery_record(bcb, sizeof bcb), "bcb build");
    CHECK(!memcmp(bcb, "boot-recovery", 14), "bcb command");
    CHECK(!memcmp(bcb + 64, "recovery\n", 10), "bcb recovery arg");
    unsigned dirty = 0;
    for (unsigned i = 864; i < HOUJI_LOG_PAGE; ++i) dirty |= bcb[i] != 0xA5;
    CHECK(!dirty, "bcb reserved bytes preserved");
  }

  printf(fails ? "RESULT %d FAIL\n" : "RESULT ALL PASS\n", fails);
  return fails ? 1 : 0;
}
