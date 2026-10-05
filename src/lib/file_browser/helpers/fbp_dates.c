#include "fbp_dates.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

struct FbpDates {
  FbpDateRead read;
  void *context;
  uint32_t sectors, start, end, fat, data, root, root_sectors, clusters;
  uint8_t bits, spc;
  uint8_t sector[512];
  uint16_t lfn[260];
  char name[781];
  char parent[1024];
  uint32_t parent_cluster;
};
static uint16_t u16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
static uint32_t u32(const uint8_t *p) {
  return u16(p) | ((uint32_t)u16(p + 2) << 16);
}
static bool read_sector(FbpDates *d, uint32_t s) {
  return s < d->sectors && d->read(d->context, s, d->sector);
}
static bool boot(FbpDates *d, uint32_t start, uint32_t limit) {
  if (!read_sector(d, start))
    return false;
  const uint8_t *b = d->sector;
  uint32_t total = u16(b + 19);
  if (!total)
    total = u32(b + 32);
  uint32_t fat_size = u16(b + 22);
  if (!fat_size)
    fat_size = u32(b + 36);
  uint32_t roots = (u16(b + 17) * 32u + 511) / 512;
  uint32_t reserved = u16(b + 14), fats = b[16], spc = b[13];
  if (u16(b + 510) != 0xaa55 || u16(b + 11) != 512 || !spc ||
      (spc & (spc - 1)) || spc > 128 || !reserved || !fats || fats > 2 ||
      !fat_size || !total || total > limit ||
      (uint64_t)start + total > d->sectors)
    return false;
  uint64_t overhead = reserved + (uint64_t)fats * fat_size + roots;
  if (overhead >= total)
    return false;
  uint32_t clusters = (total - overhead) / spc;
  uint8_t bits = clusters < 4085 ? 12 : clusters < 65525 ? 16 : 32;
  if (!clusters || clusters >= 0x0ffffff5 ||
      ((uint64_t)(clusters + 2) * bits + 7) / 8 > (uint64_t)fat_size * 512)
    return false;
  if ((bits == 32) != (u16(b + 17) == 0) || (bits == 32 && (u16(b + 42) != 0)))
    return false;
  uint32_t active = bits == 32 && (u16(b + 40) & 0x80) ? u16(b + 40) & 15 : 0;
  if (active >= fats)
    return false;
  d->start = start;
  d->end = start + total;
  d->fat = start + reserved + active * fat_size;
  d->data = start + overhead;
  d->root_sectors = roots;
  d->root = bits == 32 ? u32(b + 44) & 0x0fffffff
                       : start + reserved + fats * fat_size;
  if (bits == 32 && (d->root < 2 || d->root >= clusters + 2))
    return false;
  d->clusters = clusters;
  d->bits = bits;
  d->spc = spc;
  return true;
}
FbpDates *fbp_dates_open(FbpDateRead read, void *context, uint32_t sectors) {
  if (!read || !sectors)
    return NULL;
  FbpDates *d = calloc(1, sizeof(*d));
  if (!d)
    return NULL;
  d->read = read;
  d->context = context;
  d->sectors = sectors;
  if (boot(d, 0, sectors))
    return d;
  if (!read_sector(d, 0) || u16(d->sector + 510) != 0xaa55) {
    free(d);
    return NULL;
  }
  uint8_t partitions[64];
  memcpy(partitions, d->sector + 446, sizeof(partitions));
  for (unsigned i = 0; i < 4; ++i) {
    const uint8_t *p = partitions + i * 16;
    if (p[4] != 1 && p[4] != 4 && p[4] != 6 && p[4] != 11 && p[4] != 12 &&
        p[4] != 14)
      continue;
    uint32_t start = u32(p + 8), size = u32(p + 12);
    if (start && size && (uint64_t)start + size <= sectors &&
        boot(d, start, size))
      return d;
  }
  free(d);
  return NULL;
}
static bool cluster_valid(FbpDates *d, uint32_t c) {
  return c >= 2 && c < d->clusters + 2;
}
static uint32_t next_cluster(FbpDates *d, uint32_t c) {
  if (!cluster_valid(d, c))
    return 0;
  uint32_t offset = d->bits == 12 ? c + c / 2 : c * (d->bits / 8);
  if (!read_sector(d, d->fat + offset / 512))
    return 0;
  uint32_t n;
  if (d->bits == 12) {
    uint8_t lo = d->sector[offset % 512], hi;
    if (offset % 512 == 511) {
      if (!read_sector(d, d->fat + offset / 512 + 1))
        return 0;
      hi = d->sector[0];
    } else
      hi = d->sector[offset % 512 + 1];
    n = lo | ((uint32_t)hi << 8);
    n = c & 1 ? n >> 4 : n & 4095;
  } else
    n = d->bits == 16 ? u16(d->sector + offset % 512)
                      : u32(d->sector + offset % 512) & 0x0fffffff;
  return cluster_valid(d, n) ? n : 0;
}
static uint8_t checksum(const uint8_t *e) {
  uint8_t s = 0;
  for (unsigned i = 0; i < 11; ++i)
    s = ((s & 1) << 7) + (s >> 1) + e[i];
  return s;
}
static bool long_name(FbpDates *d) {
  size_t out = 0;
  for (unsigned i = 0; i < 260; ++i) {
    uint32_t c = d->lfn[i];
    if (!c || c == 0xffff) {
      d->name[out] = 0;
      return true;
    }
    if (i >= 255)
      return false;
    if (c >= 0xd800 && c <= 0xdbff) {
      if (i + 1 >= 255 || d->lfn[i + 1] < 0xdc00 || d->lfn[i + 1] > 0xdfff)
        return false;
      c = 0x10000 + ((c - 0xd800) << 10) + d->lfn[++i] - 0xdc00;
    } else if (c >= 0xdc00 && c <= 0xdfff)
      return false;
    if (c < 128)
      d->name[out++] = c;
    else if (c < 2048) {
      d->name[out++] = 0xc0 | (c >> 6);
      d->name[out++] = 0x80 | (c & 63);
    } else if (c < 65536) {
      d->name[out++] = 0xe0 | (c >> 12);
      d->name[out++] = 0x80 | ((c >> 6) & 63);
      d->name[out++] = 0x80 | (c & 63);
    } else {
      d->name[out++] = 0xf0 | (c >> 18);
      d->name[out++] = 0x80 | ((c >> 12) & 63);
      d->name[out++] = 0x80 | ((c >> 6) & 63);
      d->name[out++] = 0x80 | (c & 63);
    }
  }
  return false;
}
static void short_name(FbpDates *d, const uint8_t *e) {
  size_t n = 0;
  for (unsigned i = 0; i < 8 && e[i] != ' '; ++i)
    d->name[n++] = e[i];
  if (e[8] != ' ') {
    d->name[n++] = '.';
    for (unsigned i = 8; i < 11 && e[i] != ' '; ++i)
      d->name[n++] = e[i];
  }
  d->name[n] = 0;
}
static bool lookup(FbpDates *d, uint32_t directory, const char *name,
                   size_t length, uint32_t *cluster, uint32_t *date,
                   bool *folder) {
  uint32_t c = directory, visited = 0;
  unsigned remaining = 0;
  uint8_t sum = 0;
  bool lfn_valid = false;
  do {
    bool fixed = !c && d->bits != 32;
    if (!fixed && !cluster_valid(d, c))
      return false;
    uint32_t first = fixed ? d->root : d->data + (c - 2) * d->spc;
    uint32_t count = fixed ? d->root_sectors : d->spc;
    for (uint32_t s = 0; s < count; ++s) {
      if (first + s >= d->end || !read_sector(d, first + s))
        return false;
      for (unsigned pos = 0; pos < 512; pos += 32) {
        const uint8_t *e = d->sector + pos;
        if (!e[0])
          return false;
        if (e[0] == 0xe5) {
          lfn_valid = false;
          continue;
        }
        if (e[11] == 15) {
          unsigned ordinal = e[0] & 31;
          if (e[0] & 0x40) {
            memset(d->lfn, 0xff, sizeof(d->lfn));
            remaining = ordinal;
            sum = e[13];
            lfn_valid = true;
          }
          if (!ordinal || ordinal > 20 || !lfn_valid || ordinal != remaining ||
              sum != e[13] || e[12] || u16(e + 26) || (e[0] & 0xa0)) {
            lfn_valid = false;
            continue;
          }
          const uint8_t offsets[] = {1,  3,  5,  7,  9,  14, 16,
                                     18, 20, 22, 24, 28, 30};
          for (unsigned j = 0; j < 13; ++j)
            d->lfn[(ordinal - 1) * 13 + j] = u16(e + offsets[j]);
          --remaining;
          continue;
        }
        bool matched = false;
        if (!(e[11] & 8)) {
          if (lfn_valid && !remaining && sum == checksum(e) && long_name(d))
            matched = strlen(d->name) == length &&
                      !strncasecmp(d->name, name, length);
          if (!matched) {
            short_name(d, e);
            matched = strlen(d->name) == length &&
                      !strncasecmp(d->name, name, length);
          }
        }
        lfn_valid = false;
        if (matched) {
          *cluster =
              u16(e + 26) | (d->bits == 32 ? ((uint32_t)u16(e + 20) << 16) : 0);
          *folder = (e[11] & 16) != 0;
          uint16_t day = u16(e + 24), time = u16(e + 22);
          unsigned month = (day >> 5) & 15, dom = day & 31;
          // Existing Flipper-written entries can encode seconds 60 or 62.
          // Preserve their valid date/minute and clamp seconds to FAT's 58.
          // This only normalizes the sort key; the SD card is never modified.
          if ((time & 31) > 29)
            time = (time & ~31u) | 29;
          static const uint8_t days[] = {31, 28, 31, 30, 31, 30,
                                         31, 31, 30, 31, 30, 31};
          unsigned year = 1980 + (day >> 9);
          unsigned max_day = month && month <= 12 ? days[month - 1] : 0;
          if (month == 2 && !(year % 4) && ((year % 100) || !(year % 400)))
            max_day++;
          *date = month && month <= 12 && dom && dom <= max_day &&
                          (time >> 11) < 24 && ((time >> 5) & 63) < 60
                      ? ((uint32_t)day << 16) | time
                      : 0;
          return true;
        }
      }
    }
    if (fixed)
      return false;
    // Bound corrupt/cyclic chains even on a huge card; cancellation is checked
    // by the read callback. At most 65,536 entries are inspected per lookup.
    visited += count;
    if (visited >= 4096)
      return false;
    c = next_cluster(d, c);
  } while (c);
  return false;
}
uint32_t fbp_dates_get(FbpDates *d, const char *path) {
  if (!d || !path || strncmp(path, "/ext/", 5) || strlen(path) >= 1024)
    return 0;
  const char *part = path + 5;
  const char *last = strrchr(path, '/');
  uint32_t directory = d->bits == 32 ? d->root : 0, cluster = 0, date = 0;
  size_t parent_len = last - path;
  if (strlen(d->parent) == parent_len && !memcmp(d->parent, path, parent_len)) {
    directory = d->parent_cluster;
    part = last + 1;
  }
  while (*part) {
    const char *slash = strchr(part, '/');
    size_t n = slash ? (size_t)(slash - part) : strlen(part);
    bool folder;
    if (!n || !lookup(d, directory, part, n, &cluster, &date, &folder))
      return 0;
    if (!slash) {
      memcpy(d->parent, path, parent_len);
      d->parent[parent_len] = 0;
      d->parent_cluster = directory;
      return date;
    }
    if (!folder || !cluster_valid(d, cluster))
      return 0;
    directory = cluster;
    part = slash + 1;
  }
  return 0;
}
void fbp_dates_close(FbpDates *d) { free(d); }
