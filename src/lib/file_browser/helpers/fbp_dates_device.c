#include "fbp_dates.h"
#include <furi_hal.h>
#include <stdatomic.h>
// Two-sector reads bypass the firmware's unguarded single-sector cache.
// HAL serializes the physical SD transaction with its SPI bus mutex.
static bool read_sd(void *context, uint32_t sector, uint8_t data[512]) {
  if (atomic_load((atomic_bool *)context) || !furi_hal_sd_is_present())
    return false;
  uint32_t buffer[256];
  if (furi_hal_sd_read_blocks(buffer, sector & ~1u, 2) != FuriStatusOk)
    return false;
  memcpy(data, (uint8_t *)buffer + (sector & 1u) * 512, 512);
  return true;
}
FbpDates *fbp_dates_device_open(void *cancel) {
  FuriHalSdInfo info;
  if (!furi_hal_sd_is_present() || furi_hal_sd_info(&info) != FuriStatusOk ||
      info.logical_block_size != 512)
    return NULL;
  return fbp_dates_open(read_sd, cancel, info.logical_block_count & ~1u);
}
void fbp_dates_device_close(FbpDates *dates) { fbp_dates_close(dates); }
