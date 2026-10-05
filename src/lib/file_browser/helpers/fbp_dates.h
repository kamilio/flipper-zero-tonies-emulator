#pragma once
#include <stdbool.h>
#include <stdint.h>
// Read-only FAT metadata. Date keys are packed FAT modification date/time,
// comparable as unsigned integers; zero means unavailable, never volume time.
typedef struct FbpDates FbpDates;
typedef bool (*FbpDateRead)(void *context, uint32_t sector, uint8_t data[512]);
FbpDates *fbp_dates_open(FbpDateRead read, void *context, uint32_t sectors);
uint32_t fbp_dates_get(FbpDates *dates, const char *path);
void fbp_dates_close(FbpDates *dates);
// Device adapter borrows the cancellation flag and checks cancellation on every read.
FbpDates *fbp_dates_device_open(void *cancel);
void fbp_dates_device_close(FbpDates *dates);
