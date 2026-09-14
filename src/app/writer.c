#include "writer.h"

#include <furi.h>
#include <toolbox/bit_buffer.h>

/* Session logging lives in tonie_app.c; declared here to avoid a circular include. */
void tonie_app_writer_log(void* context, const char* format, ...);

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "TonieWriter"

#define ISO15693_CMD_WRITE_BLOCK 0x21

/* Gen2 magic UID commands (Proxmark "hf 15 csetuid --v2" sequence):
 *   HIGH: 02 E0 09 40 uid[7..4]
 *   LOW:  02 E0 09 41 uid[3..0]
 * The chip stores the UID LSB-first, so each half is sent reversed. */
#define MAGIC_CMD_SET_UID_HIGH 0x40
#define MAGIC_CMD_SET_UID_LOW 0x41

#define MAGIC_MAX_BLOCKS 128
#define MAGIC_FWT_FC 500000 /* ~37 ms; write blocks need a long EOF window */
#define MAGIC_UID_RETRIES 3
#define MAGIC_BLOCK_RETRIES 5
#define MAGIC_BLOCK_SETTLE_MS 20

struct TonieWriter {
    uint8_t uid[8]; /* MSB-first, as in the .nfc file */
    uint8_t data[MAGIC_MAX_BLOCKS * 4];
    uint16_t block_count;
    uint8_t block_size;
    uint16_t last_block; /* failing block, or blocks written */
    uint8_t last_error;  /* last poller error / card error flag */
    void* log_context;
    BitBuffer* tx;
    BitBuffer* rx;
    atomic_bool cancelled;
    atomic_uint progress;
    atomic_int phase;
    atomic_bool may_have_changed;
    bool clear_only;
    bool committing; /* NFC worker only; finish both UID halves after cancel. */
};

#define writer_log(w, ...) \
    do { \
        if((w)->log_context) tonie_app_writer_log((w)->log_context, __VA_ARGS__); \
    } while(0)

void tonie_writer_set_logger(TonieWriter* writer, void* log_context) {
    if(writer) writer->log_context = log_context;
}

TonieWriter* tonie_writer_alloc(void) {
    TonieWriter* writer = malloc(sizeof(TonieWriter));
    furi_check(writer);
    memset(writer, 0, sizeof(*writer));
    writer->tx = bit_buffer_alloc(16);
    /* The SDK copies the whole response before callers can inspect its length. */
    writer->rx = bit_buffer_alloc(64);
    atomic_init(&writer->cancelled, false);
    atomic_init(&writer->progress, 0);
    atomic_init(&writer->phase, TonieWriteWaiting);
    atomic_init(&writer->may_have_changed, false);
    return writer;
}

void tonie_writer_free(TonieWriter* writer) {
    if(!writer) return;
    bit_buffer_free(writer->tx);
    bit_buffer_free(writer->rx);
    free(writer);
}

bool tonie_writer_set_dump(
    TonieWriter* writer,
    const uint8_t uid[8],
    const uint8_t* blocks,
    uint16_t block_count,
    uint8_t block_size) {
    if(!writer) return false;
    writer->block_count = 0;
    atomic_store(&writer->cancelled, false);
    atomic_store(&writer->progress, 0);
    atomic_store(&writer->phase, TonieWriteWaiting);
    atomic_store(&writer->may_have_changed, false);
    writer->committing = false;
    writer->clear_only = false;
    /* uid is MSB-first, as in the .nfc file. */
    if(!uid || uid[0] != 0xe0 || !blocks || !block_count || block_count > MAGIC_MAX_BLOCKS || block_size != 4)
        return false;
    memcpy(writer->uid, uid, 8);
    memcpy(writer->data, blocks, (size_t)block_count * block_size);
    writer->block_count = block_count;
    writer->block_size = block_size;
    return true;
}

bool tonie_writer_set_clear(TonieWriter* writer, const uint8_t uid[8], uint16_t blocks) {
    const uint8_t zeroes[MAGIC_MAX_BLOCKS * 4] = {0};
    if(!tonie_writer_set_dump(writer, uid, zeroes, blocks, 4)) return false;
    writer->clear_only = true;
    return true;
}

void tonie_writer_cancel(TonieWriter* writer) {
    atomic_store(&writer->cancelled, true);
}

unsigned tonie_writer_progress(const TonieWriter* writer) {
    return atomic_load(&writer->progress);
}

TonieWritePhase tonie_writer_phase(const TonieWriter* writer) {
    return atomic_load(&writer->phase);
}

bool tonie_writer_may_have_changed(const TonieWriter* writer) {
    return atomic_load(&writer->may_have_changed);
}

/* --------------------------------------------------------------------------
 * Raw frame helpers (NfcModePoller transparent exchange)
 * ------------------------------------------------------------------------ */

static bool reply_ok(const BitBuffer* rx) {
    if(bit_buffer_get_size_bytes(rx) < 1) return false;
    return bit_buffer_get_byte(rx, 0) == 0;
}

/* Send a raw frame; *rx_bytes receives the response length (0 on timeout).
 * Returns true when the exchange succeeded and the card reported no error. */
static bool send_raw(
    TonieWriter* writer,
    const TonieWriterTransport* iso,
    const uint8_t* frame,
    size_t frame_len,
    size_t* rx_bytes) {
    if(rx_bytes) *rx_bytes = 0;
    if(atomic_load(&writer->cancelled) && !writer->committing) return false;
    BitBuffer* tx = writer->tx;
    BitBuffer* rx = writer->rx;
    bit_buffer_reset(tx);
    bit_buffer_append_bytes(tx, frame, frame_len);
    bit_buffer_set_size(rx, 0);

    const unsigned err = iso->exchange(iso->context, tx, rx, MAGIC_FWT_FC);
    if(rx_bytes && err == Iso15693_3ErrorNone) *rx_bytes = bit_buffer_get_size_bytes(rx);
    const bool ok = (err == Iso15693_3ErrorNone) && reply_ok(rx);
    writer->last_error = (uint8_t)err;
    if(err == Iso15693_3ErrorNone && !ok)
        writer->last_error = bit_buffer_get_size_bytes(rx) >= 2 ?
                                 bit_buffer_get_byte(rx, 1) : 0xff;

    return ok;
}

static bool magic_write_uid_msb(TonieWriter* writer, const TonieWriterTransport* iso, const uint8_t uid_msb[8]) {
    const uint8_t frame_high[8] = {
        0x02, 0xE0, 0x09, MAGIC_CMD_SET_UID_HIGH, uid_msb[7], uid_msb[6], uid_msb[5], uid_msb[4]};
    const uint8_t frame_low[8] = {
        0x02, 0xE0, 0x09, MAGIC_CMD_SET_UID_LOW, uid_msb[3], uid_msb[2], uid_msb[1], uid_msb[0]};
    size_t rx_bytes = 0;
    writer_log(writer, "UID HIGH %02X%02X%02X%02X", uid_msb[0], uid_msb[1], uid_msb[2], uid_msb[3]);
    for(int attempt = 0; attempt < MAGIC_UID_RETRIES; ++attempt) {
        if(send_raw(writer, iso, frame_high, sizeof(frame_high), &rx_bytes) && rx_bytes == 1) break;
        writer_log(writer, "UID HIGH retry %d err=%u", attempt + 1, (unsigned)writer->last_error);
        if(attempt == MAGIC_UID_RETRIES - 1) {
            FURI_LOG_E(TAG, "SET_UID_HIGH unacknowledged");
            /* The command may have landed. Finish the other half, then let
             * inventory determine the actual UID instead of leaving a half UID. */
            break;
        }
        furi_delay_ms(10);
    }
    furi_delay_ms(20);
    writer_log(writer, "UID LOW %02X%02X%02X%02X", uid_msb[4], uid_msb[5], uid_msb[6], uid_msb[7]);
    for(int attempt = 0; attempt < MAGIC_UID_RETRIES; ++attempt) {
        if(send_raw(writer, iso, frame_low, sizeof(frame_low), &rx_bytes) && rx_bytes == 1) {
            writer_log(writer, "UID write ok");
            return true;
        }
        writer_log(writer, "UID LOW retry %d err=%u", attempt + 1, (unsigned)writer->last_error);
        if(attempt == MAGIC_UID_RETRIES - 1) {
            FURI_LOG_E(TAG, "SET_UID_LOW failed");
            return false;
        }
        furi_delay_ms(10);
    }
    return true;
}

/* Normal mode, as documented by Julienbxl/SLI-Writer at
 * 59c8689d84922c5e55fda2c24ac9d5fc61a73de8 (Flipper/sli_writer.c).
 * Keep the successful write sequence intact; verify after committing the UID.
 * There is no factory-UID restore, UID-family filter or layout command. */
static bool read_block_matches(TonieWriter* writer, const TonieWriterTransport* iso, uint16_t block) {
    uint8_t frame[11] = {0x02, 0x20, (uint8_t)block};
    size_t length = 3;
    if(writer->clear_only) {
        frame[0] = 0x22;
        for(size_t i = 0; i < 8; ++i) frame[2+i] = writer->uid[7-i];
        frame[10] = (uint8_t)block;
        length = sizeof(frame);
    }
    size_t size = 0;
    if(!send_raw(writer, iso, frame, length, &size) || size != 5) return false;
    return memcmp(bit_buffer_get_data(writer->rx) + 1, writer->data + block * 4U, 4) == 0;
}

static bool write_blocks(TonieWriter* writer, const TonieWriterTransport* iso) {
    atomic_store(&writer->phase, TonieWriteBlocks);
    for(uint16_t block = 0; block < writer->block_count; ++block) {
        writer->last_block = block;
        uint8_t frame[15] = {0x02, ISO15693_CMD_WRITE_BLOCK, (uint8_t)block};
        size_t length = 7;
        memcpy(frame + 3, writer->data + block * 4U, 4);
        if(writer->clear_only) {
            frame[0] = 0x22;
            for(size_t i = 0; i < 8; ++i) frame[2+i] = writer->uid[7-i];
            frame[10] = (uint8_t)block;
            memset(frame + 11, 0, 4);
            length = sizeof(frame);
        }
        bool wrote = false;
        for(unsigned attempt = 0; attempt < MAGIC_BLOCK_RETRIES; ++attempt) {
            if(atomic_load(&writer->cancelled)) return false;
            size_t size = 0;
            atomic_store(&writer->may_have_changed, true);
            wrote = send_raw(writer, iso, frame, length, &size) && size == 1;
            furi_delay_ms(MAGIC_BLOCK_SETTLE_MS);
            if(wrote) break;
            /* Clear stays addressed to the confirmed chip. Normal writes keep
             * SLI-Writer's non-addressed Option fallback. */
            if(!writer->clear_only) frame[0] = 0x42;
        }
        /* Recover a lost ACK only if reading proves that the write landed. */
        if(!wrote && !read_block_matches(writer, iso, block)) return false;
        if(atomic_load(&writer->cancelled)) return false;
        atomic_store(&writer->progress, block + 1U);
    }
    return true;
}

bool tonie_writer_write(
    TonieWriter* writer,
    const TonieWriterTransport* iso,
    char* result,
    size_t result_size) {
    if(!writer || !iso || !iso->context || !iso->exchange || !result || !result_size) return false;
    if(!writer->block_count) {
        snprintf(result, result_size, "Unsupported dump geometry");
        return false;
    }
    writer->last_block = 0;
    writer->last_error = 0;

    const uint8_t inventory[] = {0x26, 0x01, 0x00};
    size_t size = 0;
    if(!send_raw(writer, iso, inventory, sizeof(inventory), &size) || size != 10) {
        snprintf(result, result_size, "Chip inventory failed");
        return false;
    }
    uint8_t card_uid_msb[8];
    uint8_t select[10] = {0x22, 0x25};
    /* Raw inventory is LSB-first; unlike the SDK's parsed UID. */
    for(size_t i = 0; i < 8; ++i) {
        select[i + 2] = bit_buffer_get_byte(writer->rx, i + 2);
        card_uid_msb[7 - i] = select[i + 2];
    }
    if(writer->clear_only && memcmp(writer->uid, card_uid_msb, 8) != 0) {
        snprintf(result, result_size, "Different chip; cancelled");
        return false;
    }
    furi_delay_ms(20);
    send_raw(writer, iso, select, sizeof(select), &size); /* SELECT failure is non-fatal. */
    furi_delay_ms(10);

    if(!write_blocks(writer, iso)) {
        snprintf(
            result,
            result_size,
            "Block %u failed (err %u)",
            (unsigned)writer->last_block,
            (unsigned)writer->last_error);
        return false;
    }

    if(atomic_load(&writer->cancelled)) {
        snprintf(result, result_size, "Cancelled; chip incomplete");
        return false;
    }
    /* Once started, complete this bounded commit despite a Cancel request.
     * Physical removal may still fail; never report success without verification. */
    writer->committing = true;
    atomic_store(&writer->phase, TonieWriteUid);
    if(!writer->clear_only && memcmp(writer->uid, card_uid_msb, 8) != 0) {
        if(!magic_write_uid_msb(writer, iso, writer->uid)) {
            writer_log(writer, "UID command unacknowledged; checking actual UID");
        }
    }

    atomic_store(&writer->phase, TonieWriteVerify);
    bool uid_verified = false;
    for(unsigned attempt = 0; attempt < MAGIC_UID_RETRIES; ++attempt) {
        furi_delay_ms(MAGIC_BLOCK_SETTLE_MS);
        size_t size = 0;
        if(!send_raw(writer, iso, inventory, sizeof(inventory), &size) || size != 10) continue;
        bool matches = true;
        for(size_t i = 0; i < 8; ++i)
            if(bit_buffer_get_byte(writer->rx, i + 2) != writer->uid[7 - i]) matches = false;
        if(matches) { uid_verified = true; break; }
    }
    writer->committing = false;
    if(!uid_verified) {
        snprintf(result, result_size, "Written; UID unverified");
        return false;
    }
    /* Verification is read-only and follows the complete normal-mode sequence.
     * Cancellation is deferred only for the short UID commit, not this scan. */
    writer->committing = false;
    for(uint16_t block = 0; block < writer->block_count; ++block) {
        if(atomic_load(&writer->cancelled)) {
            snprintf(result, result_size, "Written; verify cancelled");
            return false;
        }
        bool verified = false;
        for(unsigned attempt = 0; attempt < 3; ++attempt) {
            if(read_block_matches(writer, iso, block)) { verified = true; break; }
            furi_delay_ms(MAGIC_BLOCK_SETTLE_MS);
        }
        if(!verified) {
            snprintf(result, result_size, "Written; block %u unverified", (unsigned)block);
            return false;
        }
    }
    snprintf(result, result_size, "%s", writer->clear_only ? "Chip cleared and verified" : "Chip written and verified");
    return true;
}
