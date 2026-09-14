#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Validate lengths before any SDK block getter (which asserts on bad indices). */
static inline bool tonie_read_shape(uint16_t blocks, uint8_t block_size, size_t bytes) {
    return blocks > 0 && blocks <= 256 && block_size == 4 && bytes == (size_t)blocks * 4;
}

/* A blank chip is valid hardware, but not a usable Tonie scan. This is a warning,
 * not a claim that arbitrary non-blank bytes are valid Tonie credentials. */
static inline bool tonie_read_blank(const uint8_t* data, size_t size) {
    if(!data || !size) return true;
    bool zero = true, erased = true;
    for(size_t i = 0; i < size; ++i) {
        zero &= data[i] == 0;
        erased &= data[i] == 0xff;
    }
    return zero || erased;
}
