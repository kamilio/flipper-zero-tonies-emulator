#include "../src/lib/file_browser/helpers/fbp_dates.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
  uint8_t *bytes;
  uint32_t count, reads;
  int fail;
} Disk;
static bool read_disk(void *p, uint32_t sector, uint8_t out[512]) {
  Disk *d = p;
  d->reads++;
  if (d->fail || sector >= d->count)
    return false;
  memcpy(out, d->bytes + (size_t)sector * 512, 512);
  return true;
}
int main(int argc, char **argv) {
  assert(argc == 4);
  for (int k = 1; k < argc; k++) {
    FILE *f = fopen(argv[k], "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    Disk disk = {.count = size / 512};
    disk.bytes = malloc(size);
    assert(fread(disk.bytes, 1, size, f) == (size_t)size);
    fclose(f);
    FbpDates *d = fbp_dates_open(read_disk, &disk, disk.count);
    assert(d);
    const uint32_t expected = (0x5d44u << 16) | 0x645c;
    assert(fbp_dates_get(d, "/ext/short.nfc") == expected);
    assert(fbp_dates_get(d, "/ext/Löng name 🦊.nfc") == expected + 1);
    assert(fbp_dates_get(d, "/ext/NESTED/inside.nfc") == expected + 32);
    assert(fbp_dates_get(d, "/ext/NESTED/INSIDE.NFC") == expected + 32);
    assert(fbp_dates_get(d, "/ext/short.nfc") == expected);
    assert(fbp_dates_get(d, "/ext/missing.nfc") == 0);
    assert(fbp_dates_get(d, "/ext/broken-long-name.nfc") == 0);
    assert(fbp_dates_get(d, "/ext/BROKEN~1.NFC") == expected);
    assert(fbp_dates_get(d, "/ext/unknown.nfc") == 0);
    assert(fbp_dates_get(d, "/ext/invalid.nfc") == 0);
    assert(fbp_dates_get(d, "/ext/seconds.nfc") == expected + 1);
    char maxname[260];
    memcpy(maxname, "/ext/", 5);
    memset(maxname + 5, 'x', 245);
    memcpy(maxname + 250, ".nfc", 5);
    assert(fbp_dates_get(d, maxname) == expected);
    assert(fbp_dates_get(d, "/ext/short.nfc/child") == 0);
    assert(fbp_dates_get(d, "/int/short.nfc") == 0);
    char longpath[1100];
    memset(longpath, 'x', sizeof(longpath));
    memcpy(longpath, "/ext/", 5);
    longpath[1099] = 0;
    assert(!fbp_dates_get(d, longpath));
    disk.fail = 1;
    assert(!fbp_dates_get(d, "/ext/short.nfc"));
    disk.fail = 0;
    fbp_dates_close(d);
    // Mutate boot, FAT and directory metadata under sanitizers. No writes ever
    // leave this in-memory fixture; invalid media must fail without unsafe
    // access.
    uint32_t random = 42;
    for (unsigned run = 0; run < 500; run++) {
      random = random * 1664525u + 1013904223u;
      uint32_t offset = random % ((size_t)size < 1048576 ? size : 1048576);
      uint8_t save = disk.bytes[offset];
      disk.bytes[offset] ^= 255;
      d = fbp_dates_open(read_disk, &disk, disk.count);
      if (d) {
        fbp_dates_get(d, "/ext/Löng name 🦊.nfc");
        fbp_dates_get(d, "/ext/NESTED/inside.nfc");
        fbp_dates_close(d);
      }
      disk.bytes[offset] = save;
    }
    if (k ==
        3) { // FAT32 cyclic root chain: no terminator, all entries deleted.
      uint32_t fat_size = disk.bytes[36] | (disk.bytes[37] << 8) |
                          (disk.bytes[38] << 16) | (disk.bytes[39] << 24);
      uint32_t root_sector = 1 + fat_size;
      for (unsigned i = 0; i < 16; i++)
        disk.bytes[root_sector * 512 + i * 32] = 0xe5;
      disk.bytes[512 + 8] = 2;
      disk.bytes[512 + 9] = disk.bytes[512 + 10] = disk.bytes[512 + 11] = 0;
      d = fbp_dates_open(read_disk, &disk, disk.count);
      assert(d);
      uint32_t before = disk.reads;
      assert(!fbp_dates_get(d, "/ext/missing"));
      assert(disk.reads - before <= 8192);
      fbp_dates_close(d);
    }
    memset(disk.bytes, 0, 512);
    assert(!fbp_dates_open(read_disk, &disk, disk.count));
    free(disk.bytes);
  }
  puts("PASS read-only FAT12/16/32 dates: MBR/superfloppy, short/Unicode LFN, "
       "nested directories, invalid names/checksum/date/path, I/O faults and "
       "1500 metadata mutations");
}
