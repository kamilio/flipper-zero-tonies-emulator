#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct BitBuffer BitBuffer;
BitBuffer* bit_buffer_alloc(size_t capacity);
void bit_buffer_free(BitBuffer* buffer);
void bit_buffer_reset(BitBuffer* buffer);
void bit_buffer_append_bytes(BitBuffer* buffer, const uint8_t* bytes, size_t size);
void bit_buffer_set_size(BitBuffer* buffer, size_t bits);
size_t bit_buffer_get_size_bytes(const BitBuffer* buffer);
uint8_t bit_buffer_get_byte(const BitBuffer* buffer, size_t index);
const uint8_t* bit_buffer_get_data(const BitBuffer* buffer);
