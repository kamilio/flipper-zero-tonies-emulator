#!/usr/bin/env python3
"""Compile actual embedded memory handlers; exercise exact-sized RF payloads under ASan/UBSan."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = Path(os.environ.get('TONIE_LISTENER_SOURCE', root / 'src/native/vendor/iso15693_3_listener_i.inc')).read_text()
slix = (root / 'src/native/vendor/slix_listener_i.inc').read_text()

def function(text, name):
    start = text.index(name + '(')
    start = text.rfind('static Iso15693_3Error', 0, start)
    end = text.index('\n}', start) + 2
    return text[start:end] + '\n'

preamble = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define UNUSED(x) ((void)(x))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ISO15693_3_REQ_FLAG_T4_OPTION 0x40
#define ISO15693_3_CMD_WRITE_BLOCK 0x21
#define ISO15693_3_CMD_WRITE_MULTI_BLOCKS 0x24
#define ISO15693_3_CMD_READ_MULTI_BLOCKS 0x23
#define SLIX_COUNTER_BLOCK_NUM 79
#define SlixPasswordTypeRead 0
#define SlixPasswordTypeWrite 1
typedef enum { Iso15693_3ErrorNone, Iso15693_3ErrorFormat, Iso15693_3ErrorInternal,
    Iso15693_3ErrorIgnore, Iso15693_3ErrorFullyHandled } Iso15693_3Error;
typedef struct { size_t capacity, size; uint8_t bytes[512]; } BitBuffer;
typedef struct {
    struct { uint32_t block_count, block_size; } system_info;
    uint8_t memory[128][4]; bool locked[128];
} Data;
typedef struct {
    Data* data; BitBuffer* tx_buffer;
    struct { bool wait_for_eof; } session_state;
} Iso15693_3Listener;
static unsigned writes, extensions;
static size_t bit_buffer_get_capacity_bytes(BitBuffer* b) { return b->capacity; }
static void bit_buffer_append_byte(BitBuffer* b, uint8_t value) {
    assert(b->size < b->capacity); b->bytes[b->size++] = value;
}
static bool iso15693_3_is_block_locked(Data* d, uint32_t i) {
    assert(i < d->system_info.block_count); return d->locked[i];
}
static void iso15693_3_set_block_data(Data* d, uint32_t i, const uint8_t* p, size_t n) {
    assert(i < d->system_info.block_count && n == 4); ++writes; memcpy(d->memory[i], p, n);
}
static void iso15693_3_append_block(Data* d, uint32_t i, BitBuffer* b) {
    assert(i < d->system_info.block_count);
    for(size_t j=0;j<d->system_info.block_size;++j) bit_buffer_append_byte(b, d->memory[i][j]);
}
static void iso15693_3_append_block_security(Data* d, uint32_t i, BitBuffer* b) {
    bit_buffer_append_byte(b, iso15693_3_is_block_locked(d,i));
}
static Iso15693_3Error iso15693_3_listener_extension_handler(Iso15693_3Listener* i, unsigned cmd, ...) {
    (void)i; (void)cmd; ++extensions; return Iso15693_3ErrorNone;
}
typedef struct { Data* iso15693_3_data; } SlixData;
typedef struct { SlixData* data; } SlixListener;
static unsigned increments;
static bool slix_is_counter_increment_protected(SlixData* d) { (void)d; return false; }
static bool slix_listener_is_password_lock_enabled(SlixListener* i, int type) { (void)i; (void)type; return false; }
static bool slix_is_block_protected(SlixData* d, int type, uint32_t block) { (void)d; (void)type; (void)block; return false; }
static void slix_increment_counter(SlixData* d) { (void)d; ++increments; }
'''
main = r'''
static Iso15693_3Error counter_write(SlixListener* i, ...) {
    va_list args; va_start(args,i);
    Iso15693_3Error e=slix_listener_iso15693_3_write_block_extension_handler(i,args);
    va_end(args); return e;
}
int main(void) {
    Data d={.system_info={80,4}};
    BitBuffer b={.capacity=64,.size=1};
    Iso15693_3Listener i={.data=&d,.tx_buffer=&b};
    /* This exact-size, nonzero-start request reproduced an ASan overflow before the fix. */
    uint8_t* nonzero=calloc(10,1); assert(nonzero); nonzero[0]=5; nonzero[1]=1;
    assert(iso15693_3_listener_write_multi_blocks_handler(&i,nonzero,10,0)==Iso15693_3ErrorNone);
    free(nonzero);
    for(unsigned start=0;start<256;++start) for(unsigned count=0;count<256;++count) {
        size_t size=2+4*(count+1);
        uint8_t* packet=malloc(size); assert(packet);
        packet[0]=start; packet[1]=count;
        for(size_t j=2;j<size;++j) packet[j]=(uint8_t)(j*17);
        memset(d.memory,0xa5,sizeof(d.memory)); writes=0;
        bool valid=start<80 && count+1<=80-start;
        Iso15693_3Error e=iso15693_3_listener_write_multi_blocks_handler(&i,packet,size,0);
        assert((e==Iso15693_3ErrorNone)==valid);
        assert(writes==(valid?count+1:0));
        for(unsigned block=0;block<80;++block) {
            if(valid && block>=start && block<=start+count)
                assert(!memcmp(d.memory[block],packet+2+4*(block-start),4));
            else for(unsigned k=0;k<4;++k) assert(d.memory[block][k]==0xa5);
        }
        free(packet);
        uint8_t request[2]={(uint8_t)start,(uint8_t)count};
        for(unsigned option=0;option<=1;++option) {
            b.size=1;
            e=iso15693_3_listener_read_multi_blocks_handler(&i,request,2,option?0x40:0);
            assert(b.size<=b.capacity-2);
            if(start>=80) assert(e!=Iso15693_3ErrorNone);
        }
        b.size=1;
        e=iso15693_3_listener_get_multi_blocks_security_handler(&i,request,2,0);
        assert(b.size<=b.capacity-2);
        assert((e==Iso15693_3ErrorNone)==(start+count<80 && count+1<=61));
    }
    /* Truncated/surplus packets and a late locked block must mutate nothing. */
    for(size_t size=0;size<20;++size) {
        uint8_t* p=calloc(size?size:1,1); assert(p);
        if(size>1) p[1]=1;
        writes=0; d.locked[1]=true;
        assert(iso15693_3_listener_write_multi_blocks_handler(&i,p,size,0)!=Iso15693_3ErrorNone);
        assert(writes==0); d.locked[1]=false; free(p);
    }
    for(unsigned start=0;start<256;++start) for(size_t size=0;size<10;++size) {
        uint8_t* p=calloc(size?size:1,1); assert(p); if(size) p[0]=start;
        writes=0;
        Iso15693_3Error e=iso15693_3_listener_write_block_handler(&i,p,size,0);
        assert((e==Iso15693_3ErrorNone)==(start<80 && size==5));
        assert(writes==(unsigned)(start<80 && size==5)); free(p);
    }
    /* Counter payload deliberately unaligned; smaller dump block sizes rejected. */
    SlixData sd={.iso15693_3_data=&d}; SlixListener sl={.data=&sd};
    uint8_t* bytes=calloc(5,1); assert(bytes); bytes[1]=1;
    assert(counter_write(&sl,(uint32_t)79,(const uint8_t*)(bytes+1))==Iso15693_3ErrorFullyHandled);
    assert(increments==1); d.system_info.block_size=1;
    assert(counter_write(&sl,(uint32_t)79,(const uint8_t*)(bytes+1))==Iso15693_3ErrorFormat);
    free(bytes);
    puts("Listener: 65,536 block ranges, payload lengths, locked blocks, response capacity and unaligned counter writes PASS");
}
'''
handlers = ''.join(function(source, name) for name in [
    'iso15693_3_listener_write_block_handler', 'iso15693_3_listener_read_multi_blocks_handler',
    'iso15693_3_listener_write_multi_blocks_handler', 'iso15693_3_listener_get_multi_blocks_security_handler'])
handlers += function(slix, 'slix_listener_iso15693_3_write_block_extension_handler')
with tempfile.TemporaryDirectory(prefix='tonie-memory-') as directory:
    c=Path(directory)/'memory.c'; binary=Path(directory)/'memory'
    c.write_text(preamble+handlers+main)
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-fno-sanitize-recover=all','-g',str(c),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
