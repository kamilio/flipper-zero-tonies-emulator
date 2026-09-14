#!/usr/bin/env python3
"""Exercise actual readback verification with malformed RF replies and chip swaps."""
import os
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'src/app/reader.c').read_text()
start = source.index('static const char* reader_check(')
function = source[start:source.index('\n}', start)+2]
preamble = r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/app/read_check.h"
typedef struct {uint8_t data[1024];size_t size;} SimpleArray;
typedef struct {uint8_t uid[8];uint16_t blocks;uint8_t size;SimpleArray* block_data;} Iso15693_3Data;
typedef struct {Iso15693_3Data* iso15693_3_data;} SlixData;
typedef struct {atomic_bool cancelled;} Reader;
typedef struct {size_t capacity,size;uint8_t data[64];} BitBuffer;
typedef int SlixPoller;
enum {SlixErrorNone, SlixErrorTimeout};
static unsigned allocations, exchanges;
static int mode;
static size_t reply_size=5;
static unsigned corrupt_byte;
static Reader* active;
static Iso15693_3Data* chip;
static uint16_t iso15693_3_get_block_count(const Iso15693_3Data* d) {return d->blocks;}
static uint8_t iso15693_3_get_block_size(const Iso15693_3Data* d) {return d->size;}
static size_t simple_array_get_count(const SimpleArray* a) {return a->size;}
static const uint8_t* simple_array_cget_data(const SimpleArray* a) {assert(a->size);return a->data;}
static BitBuffer* bit_buffer_alloc(size_t cap) {assert(cap<=64);BitBuffer* b=calloc(1,sizeof(*b));b->capacity=cap;++allocations;return b;}
static void bit_buffer_free(BitBuffer* b) {assert(allocations);--allocations;free(b);}
static void bit_buffer_reset(BitBuffer* b) {b->size=0;}
static void bit_buffer_append_bytes(BitBuffer* b,const uint8_t* p,size_t n) {assert(b->size+n<=b->capacity);memcpy(b->data+b->size,p,n);b->size+=n;}
static size_t bit_buffer_get_size_bytes(const BitBuffer* b) {return b->size;}
static uint8_t bit_buffer_get_byte(const BitBuffer* b,size_t i) {assert(i<b->size);return b->data[i];}
static const uint8_t* bit_buffer_get_data(const BitBuffer* b) {return b->data;}
static int slix_poller_send_frame(SlixPoller* p,const BitBuffer* tx,BitBuffer* rx,unsigned timeout) {
    (void)p;assert(timeout && tx->size==11 && tx->data[0]==0x22 && tx->data[1]==0x20);
    for(size_t i=0;i<8;++i) assert(tx->data[2+i]==chip->uid[7-i]);
    unsigned block=tx->data[10];assert(block<chip->blocks);++exchanges;
    if(mode==1 || (mode==2 && exchanges==1)) return SlixErrorTimeout;
    if(mode==3) atomic_store(&active->cancelled,true);
    uint8_t reply[64]={0};memcpy(reply+1,chip->block_data->data+block*4,4);
    if(mode==4 && block==corrupt_byte/4) reply[1+corrupt_byte%4]^=1;
    if(mode==5) reply[0]=1;
    bit_buffer_append_bytes(rx,reply,reply_size);return SlixErrorNone;
}
'''
main = r'''
int main(void) {
    SimpleArray memory={.size=32};for(size_t i=0;i<sizeof(memory.data);++i) memory.data[i]=(uint8_t)i;
    Iso15693_3Data iso={.uid={0xe0,4,3,0x50,1,2,3,4},.blocks=8,.size=4,.block_data=&memory};
    SlixData data={&iso};Reader reader={0};SlixPoller poller=0;active=&reader;chip=&iso;
    for(unsigned count=0;count<=UINT16_MAX;++count) {
        bool valid=count>=1 && count<=256;
        assert(tonie_read_shape((uint16_t)count,4,(size_t)count*4)==valid);
    }
    for(size_t bytes=0;bytes<1100;++bytes) assert(tonie_read_shape(8,4,bytes)==(bytes==32));
    for(unsigned size=0;size<=255;++size) assert(tonie_read_shape(8,(uint8_t)size,8*size)==(size==4));
    for(mode=0;mode<=5;++mode) {
        exchanges=0;atomic_store(&reader.cancelled,false);
        const char* warning=reader_check(&reader,&poller,&data);
        assert((warning==NULL)==(mode==0 || mode==2));assert(!allocations);
        if(mode==1) assert(exchanges==2);
        if(mode==3) assert(exchanges==1);
    }
    mode=4;
    for(corrupt_byte=0;corrupt_byte<32;++corrupt_byte) {
        atomic_store(&reader.cancelled,false);exchanges=0;
        assert(reader_check(&reader,&poller,&data) && exchanges<=16 && !allocations);
    }
    mode=0;
    for(reply_size=0;reply_size<=64;++reply_size) {
        atomic_store(&reader.cancelled,false);
        assert((reader_check(&reader,&poller,&data)==NULL)==(reply_size==5));assert(!allocations);
    }
    reply_size=5;iso.blocks=256;memory.size=1024;exchanges=0;
    assert(!reader_check(&reader,&poller,&data) && exchanges==256);
    for(size_t bytes=0;bytes<1024;++bytes) {
        memory.size=bytes;exchanges=0;
        assert(reader_check(&reader,&poller,&data) && !exchanges && !allocations);
    }
    memory.size=32;iso.blocks=8;
    for(unsigned value=0;value<=255;value+=255) {
        memset(memory.data,value,32);exchanges=0;
        assert(reader_check(&reader,&poller,&data) && !exchanges);
    }
    memory.data[0]=0x42;assert(!tonie_read_blank(memory.data,32));
    assert(tonie_read_blank(NULL,32) && tonie_read_blank(memory.data,0));
    puts("Read integrity: missing/blank data, every geometry/length, per-byte corruption, addressed UID binding, RF loss, cancellation and full 256-block reads PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='tonie-read-check-') as directory:
    c=Path(directory)/'check.c';binary=Path(directory)/'check'
    c.write_text(preamble+function+main)
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-fno-sanitize-recover=all','-I',str(root),str(c),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
