#pragma once
#include <assert.h>
#include <stdint.h>
#define furi_check(x) assert(x)
#define FURI_LOG_I(...) ((void)0)
#define FURI_LOG_E(...) ((void)0)
static inline void furi_delay_ms(uint32_t ms) { (void)ms; }
