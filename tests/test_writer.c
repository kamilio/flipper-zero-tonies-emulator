/* Exercise the production writer with bounded SDK buffers and a synthetic chip. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../src/app/writer.c"

struct BitBuffer { size_t capacity, size; uint8_t bytes[64]; };
struct Iso15693_3Poller { unsigned marker; };
static Iso15693_3Poller poller = {0x534c4958};
static unsigned exchange(void* ctx, const BitBuffer* tx, BitBuffer* rx, uint32_t timeout) {
    return iso15693_3_poller_send_frame(ctx, tx, rx, timeout);
}
static const TonieWriterTransport transport = {.context=&poller,.exchange=exchange};
static const uint8_t clone_uid[8] = {0xe0, 0x04, 0x01, 0x09, 1, 2, 3, 4};
static const uint8_t target_uid[8] = {0xe0, 0x04, 0x03, 0x50, 5, 6, 7, 8};
static uint8_t chip_uid[8], memory[128][4];
static unsigned frames, writes, uid_writes;
static int fail_mode;
static unsigned reads, inventories;
static TonieWriter* active;
static bool testing_clear;
static int malformed_command = -1;
static size_t malformed_length;
static uint8_t malformed_flags;

BitBuffer* bit_buffer_alloc(size_t capacity) {
    assert(capacity <= 64);
    BitBuffer* b = calloc(1, sizeof(*b));
    assert(b); b->capacity = capacity; return b;
}
void bit_buffer_free(BitBuffer* b) { free(b); }
void bit_buffer_reset(BitBuffer* b) { b->size = 0; }
void bit_buffer_set_size(BitBuffer* b, size_t bits) {
    assert(bits % 8 == 0 && bits / 8 <= b->capacity); b->size = bits / 8;
}
void bit_buffer_append_bytes(BitBuffer* b, const uint8_t* p, size_t n) {
    assert(b->size + n <= b->capacity); memcpy(b->bytes + b->size, p, n); b->size += n;
}
size_t bit_buffer_get_size_bytes(const BitBuffer* b) { return b->size; }
uint8_t bit_buffer_get_byte(const BitBuffer* b, size_t i) { assert(i < b->size); return b->bytes[i]; }
const uint8_t* bit_buffer_get_data(const BitBuffer* b) { return b->bytes; }
void tonie_app_writer_log(void* context, const char* format, ...) { (void)context; (void)format; }

Iso15693_3Error iso15693_3_poller_send_frame(Iso15693_3Poller* instance, const BitBuffer* tx, BitBuffer* rx, uint32_t fwt) {
    assert(instance == &poller && instance->marker == 0x534c4958 && fwt > 0);
    ++frames;
    const uint8_t* p = tx->bytes;
    uint8_t reply[64] = {0}; size_t n = 1;
    switch(p[1]) {
    case 0x25:
        assert(tx->size == 10 && p[0] == 0x22 && writes == 0 && uid_writes == 0);
        for(size_t i = 0; i < 8; ++i) assert(p[2+i] == chip_uid[7-i]);
        if(fail_mode == 14) return Iso15693_3ErrorTimeout;
        break;
    case 0x21:
        if(testing_clear) {
            assert(tx->size == 15 && p[0] == 0x22);
            for(size_t j=0;j<8;++j) assert(p[2+j] == clone_uid[7-j]);
            for(size_t j=11;j<15;++j) assert(p[j] == 0);
        } else assert(tx->size == 7 && (p[0] == 0x02 || p[0] == 0x42));
        assert(uid_writes == 0); /* No factory UID restore before the data. */
        if(writes == 0 && !testing_clear) assert(p[0] == 0x02);
        ++writes;
        if(fail_mode == 1) return Iso15693_3ErrorTimeout;
        if(fail_mode == 2) { reply[0] = 1; reply[1] = 0x12; n = 2; break; }
        if(fail_mode == 3) { n = 64; break; }
        if(fail_mode == 13 && p[0] == 0x02) {
            reply[0] = 1; reply[1] = 0x0f; n = 2; break;
        }
        memcpy(memory[p[testing_clear ? 10 : 2]], p + (testing_clear ? 11 : 3), 4);
        if(fail_mode == 7) return Iso15693_3ErrorTimeout; /* Landed, but ACK lost. */
        if(fail_mode == 4) tonie_writer_cancel(active);
        break;
    case 0x20:
        if(testing_clear) {
            assert(tx->size == 11 && p[0] == 0x22);
            for(size_t j=0;j<8;++j) assert(p[2+j] == clone_uid[7-j]);
        } else assert(tx->size == 3 && p[0] == 0x02);
        if(malformed_command == -1 && (fail_mode == 0 || fail_mode == 8 || fail_mode == 14))
            assert(writes > 0 && inventories >= 2); /* Verify after UID. */
        ++reads;
        if(fail_mode == 8 && reads == 1) return Iso15693_3ErrorTimeout;
        if(fail_mode == 10) return Iso15693_3ErrorTimeout; /* Removed after write. */
        memcpy(reply + 1, memory[p[testing_clear ? 10 : 2]], 4); n = 5;
        if(fail_mode == 5) reply[1] ^= 1;
        break;
    case 0xe0:
        assert(!testing_clear);
        assert(tx->size == 8 && p[0] == 0x02 && p[2] == 0x09);
        assert(p[3] == 0x40 || p[3] == 0x41); /* Never layout command 0x47. */
        assert(writes > 0);
        for(size_t j = 0; j < 4; ++j)
            assert(p[4+j] == target_uid[(p[3] == 0x40 ? 7 : 3) - j]);
        ++uid_writes;
        if(fail_mode == 11 && p[3] == 0x41) return Iso15693_3ErrorTimeout;
        if(fail_mode == 9 && p[3] == 0x40) tonie_writer_cancel(active);
        if(fail_mode == 17 && p[3] == 0x40) return Iso15693_3ErrorTimeout;
        for(size_t i = 0; i < 4; ++i) chip_uid[(p[3] == 0x40 ? 7 : 3) - i] = p[4 + i];
        if((fail_mode == 15 && p[3] == 0x40) || (fail_mode == 16 && p[3] == 0x41))
            return Iso15693_3ErrorTimeout; /* UID half landed but ACK was lost. */
        break;
    case 0x01:
        ++inventories;
        if(fail_mode == 12 && inventories == 2) return Iso15693_3ErrorTimeout;
        n = 10;
        for(size_t i = 0; i < 8; ++i) reply[2 + i] = chip_uid[7 - i];
        if(fail_mode == 6 && inventories > 1) reply[2] ^= 1;
        break;
    default: assert(0);
    }
    if(p[1] == malformed_command) {n=malformed_length;reply[0]=malformed_flags;}
    bit_buffer_append_bytes(rx, reply, n);
    return Iso15693_3ErrorNone;
}

static void reset(int mode) {
    frames = writes = uid_writes = reads = inventories = 0; fail_mode = mode;
    memcpy(chip_uid, clone_uid, 8); memset(memory, 0, sizeof(memory));
}
int main(void) {
    uint8_t dump[512]; for(size_t i = 0; i < sizeof(dump); ++i) dump[i] = (uint8_t)i;
    char result[64]; active = tonie_writer_alloc();
    for(int mode = 0; mode <= 17; ++mode) {
        reset(mode);
        assert(tonie_writer_set_dump(active, target_uid, dump, 128, 4));
        bool ok = tonie_writer_write(active, &transport, result, sizeof(result));
        bool expected = mode == 0 || mode == 7 || mode == 8 || mode == 12 || mode == 13 || mode == 14 || mode == 15 || mode == 16;
        assert(ok == expected);
        if(expected) {
            assert(writes == (mode == 7 ? 640U : mode == 13 ? 256U : 128U) && uid_writes == (mode == 15 || mode == 16 ? 4U : 2U));
            assert(tonie_writer_progress(active) == 128);
            assert(memcmp(memory, dump, 512) == 0);
            assert(memcmp(chip_uid, target_uid, 8) == 0);
        } else if(mode == 1 || mode == 2 || mode == 3 || mode == 4) assert(uid_writes == 0);
        if(mode == 1 || mode == 2 || mode == 3) assert(writes == MAGIC_BLOCK_RETRIES);
        if(mode == 9) {
            assert(uid_writes == 2 && memcmp(chip_uid,target_uid,8) == 0);
            assert(strstr(result, "verify cancelled"));
        }
        if(mode == 4) {
            assert(tonie_writer_progress(active) == 0);
            assert(tonie_writer_may_have_changed(active));
        }
        if(mode == 11) assert(uid_writes == 1 + MAGIC_UID_RETRIES);
        if(mode == 17) assert(uid_writes == 4 && strstr(result,"UID unverified"));
        assert(!active->committing);
    }
    reset(0);
    assert(!tonie_writer_set_dump(active, target_uid, dump, 129, 4));
    assert(!tonie_writer_write(active, &transport, result, sizeof(result)) && frames == 0);
    assert(!tonie_writer_set_dump(active, target_uid, dump, 1, 5));
    assert(!tonie_writer_set_dump(active, target_uid, dump, 0, 4));
    uint8_t bad_uid[8] = {0};
    assert(!tonie_writer_set_dump(active, bad_uid, dump, 1, 4));
    assert(tonie_writer_set_dump(active, target_uid, dump, 128, 4));
    tonie_writer_cancel(active);
    assert(!tonie_writer_write(active, &transport, result, sizeof(result)) && frames == 0);
    assert(!tonie_writer_may_have_changed(active));
    /* Current UID is not a reliable chip-type identifier: support already-written
     * clones and non-NXP families just as SLI-Writer normal mode does. */
    const uint8_t families[][8] = {
        {0xe0,0x04,0x03,0x50,5,6,7,8},
        {0xe0,0x07,0x81,0x2b,1,2,3,4},
        {0xe0,0x04,0x01,0x04,1,2,3,4},
    };
    for(size_t family = 0; family < sizeof(families)/sizeof(families[0]); ++family) {
        reset(0); memcpy(chip_uid,families[family],8);
        assert(tonie_writer_set_dump(active,target_uid,dump,1,4));
        assert(tonie_writer_write(active,&transport,result,sizeof(result)));
        assert(uid_writes == (family == 0 ? 0U : 2U));
    }
    /* Repeated failures/retries must clear cancellation, progress and dirty state. */
    for(unsigned cycle = 0; cycle < 500; ++cycle) {
        reset(cycle % 2 ? 4 : 0);
        assert(tonie_writer_set_dump(active, target_uid, dump, 1, 4));
        assert(!tonie_writer_may_have_changed(active) && tonie_writer_progress(active) == 0);
        assert(tonie_writer_write(active, &transport, result, sizeof(result)) == (cycle % 2 == 0));
    }
    testing_clear = true;
    reset(0); memset(memory, 0xa5, sizeof(memory));
    assert(tonie_writer_set_clear(active,clone_uid,32));
    assert(tonie_writer_write(active,&transport,result,sizeof(result)));
    assert(writes==32 && uid_writes==0 && !memcmp(chip_uid,clone_uid,8));
    for(size_t block=0;block<128;++block) for(size_t j=0;j<4;++j)
        assert(memory[block][j] == (block<32 ? 0 : 0xa5));
    reset(0);
    assert(tonie_writer_set_clear(active,clone_uid,32));
    chip_uid[7] ^= 1;
    assert(!tonie_writer_write(active,&transport,result,sizeof(result)));
    assert(writes==0 && uid_writes==0 && !tonie_writer_may_have_changed(active));
    reset(4);
    assert(tonie_writer_set_clear(active,clone_uid,32));
    assert(!tonie_writer_write(active,&transport,result,sizeof(result)));
    assert(writes==1 && uid_writes==0 && tonie_writer_may_have_changed(active));
    testing_clear=false;
    /* Exhaust every 16-bit geometry count without transmitting for invalid dumps. */
    for(unsigned count=0;count<=UINT16_MAX;++count) {
        reset(0);
        bool valid=count>=1 && count<=128;
        assert(tonie_writer_set_dump(active,target_uid,dump,(uint16_t)count,4)==valid);
        if(!valid) assert(!tonie_writer_write(active,&transport,result,sizeof(result)) && !frames);
    }
    for(unsigned size=0;size<=UINT8_MAX;++size)
        assert(tonie_writer_set_dump(active,target_uid,dump,1,(uint8_t)size)==(size==4));
    /* Every possible received length in the SDK buffer, plus every response flag.
     * Bad inventory must never reach a write. Other malformed responses must
     * terminate or recover by actual readback, with bounded exchanges. */
    const int commands[]={0x01,0x21,0x20,0xe0};
    for(unsigned command=0;command<sizeof(commands)/sizeof(*commands);++command) {
        malformed_command=commands[command];
        for(malformed_length=0;malformed_length<=64;++malformed_length) {
            malformed_flags=0;reset(0);
            assert(tonie_writer_set_dump(active,target_uid,dump,1,4));
            bool ok=tonie_writer_write(active,&transport,result,sizeof(result));
            assert(frames<40 && !active->committing);
            if(malformed_command==0x01 && malformed_length!=10) assert(!ok && !writes);
            if(malformed_command==0x20 && malformed_length!=5) assert(!ok);
        }
    }
    malformed_command=0x01;malformed_length=10;
    for(unsigned flags=0;flags<=UINT8_MAX;++flags) {
        malformed_flags=(uint8_t)flags;reset(0);
        assert(tonie_writer_set_dump(active,target_uid,dump,1,4));
        bool ok=tonie_writer_write(active,&transport,result,sizeof(result));
        assert(ok==(flags==0));if(flags) assert(!writes);
    }
    malformed_command=-1;
    reset(11);assert(tonie_writer_set_dump(active,target_uid,dump,1,4));
    assert(!tonie_writer_write(active,&transport,result,sizeof(result)));
    unsigned before=frames;tonie_writer_cancel(active);
    assert(!tonie_writer_write(active,&transport,result,sizeof(result)) && frames==before);
    /* Small result buffers must always remain in bounds and NUL-terminated. */
    for(size_t capacity=1;capacity<=sizeof(result);++capacity) {
        reset(0);memset(result,0xa5,sizeof(result));
        assert(tonie_writer_set_dump(active,target_uid,dump,1,4));
        assert(tonie_writer_write(active,&transport,result,capacity));
        assert(memchr(result,0,capacity));
        if(capacity<sizeof(result)) assert((unsigned char)result[capacity]==0xa5);
    }
    assert(!tonie_writer_write(NULL,&transport,result,sizeof(result)));
    assert(!tonie_writer_write(active,NULL,result,sizeof(result)));
    assert(!tonie_writer_write(active,&transport,NULL,sizeof(result)));
    assert(!tonie_writer_write(active,&transport,result,0));
    tonie_writer_free(active);
    puts("Writer: SLI normal-mode frames, Option fallback, UID families, 18 fault scenarios 500 reuse cycles, safe clear, exhaustive geometry/response boundaries and tiny result buffers PASS");
}
