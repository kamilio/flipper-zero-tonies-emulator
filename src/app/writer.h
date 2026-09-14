#pragma once

/* Poller-side writer for rewritable ISO15693 "magic" SLI/SLIX chips.
 *
 * tonie_writer_write() is called from the NFC poller callback. It inventories
 * the chip and writes the loaded dump to the
 * chip in the field, then sets the dump UID via the Gen2 magic commands.
 * Returns true when every block (and the UID change, if needed) succeeded.
 * The result string receives a short status or error message for the UI/log. */

#include <nfc/protocols/iso15693_3/iso15693_3_poller.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TonieWriter TonieWriter;
/* The callback owns the correctly typed poller. Never cast a SLIX poller to ISO. */
typedef struct {
    void* context;
    unsigned (*exchange)(void* context, const BitBuffer* tx, BitBuffer* rx, uint32_t timeout);
} TonieWriterTransport;
typedef enum {
    TonieWriteWaiting,
    TonieWriteBlocks,
    TonieWriteUid,
    TonieWriteVerify,
} TonieWritePhase;

TonieWriter* tonie_writer_alloc(void);
void tonie_writer_free(TonieWriter* writer);
void tonie_writer_cancel(TonieWriter* writer);
unsigned tonie_writer_progress(const TonieWriter* writer);
TonieWritePhase tonie_writer_phase(const TonieWriter* writer);
bool tonie_writer_may_have_changed(const TonieWriter* writer);

/* Copies a dump with 1..128 four-byte blocks. Invalid input clears the old dump.
 * UID is MSB-first, as parsed from the .nfc device. Call only while stopped. */
bool tonie_writer_set_dump(
    TonieWriter* writer,
    const uint8_t uid[8],
    const uint8_t* blocks,
    uint16_t block_count,
    uint8_t block_size);

/* Prepare zeroing only; caller must back up and confirm the detected chip first. */
bool tonie_writer_set_clear(TonieWriter* writer, const uint8_t uid[8], uint16_t blocks);

/* App context used for per-step session logging (log_line). */
void tonie_writer_set_logger(TonieWriter* writer, void* log_context);

/* SLI-Writer normal mode: inventory, SELECT, non-addressed blocks, target UID.
 * Call only from the ISO15693 callback. Cancellation is deferred during the bounded
 * two-part UID commit and its verification, to avoid deliberately splitting it. */
bool tonie_writer_write(
    TonieWriter* writer,
    const TonieWriterTransport* iso,
    char* result,
    size_t result_size);

#ifdef __cplusplus
}
#endif
