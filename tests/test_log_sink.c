#include "../src/app/log_sink.h"
#include <assert.h>
#include <stdio.h>
typedef struct {uint8_t data[10000];size_t used,calls;bool fail;} Fake;
static size_t write_bytes(void* context,const void* data,size_t length) {
    Fake* f=context;++f->calls;if(f->fail)return length-1;
    assert(f->used+length<=sizeof(f->data));memcpy(f->data+f->used,data,length);f->used+=length;return length;
}
int main(void) {
    TonieLogSink sink={0};Fake f={0};uint8_t data[9000];
    for(size_t i=0;i<sizeof(data);++i)data[i]=i*17;
    assert(tonie_log_append(&sink,data,17,write_bytes,&f));assert(f.calls==0);
    assert(tonie_log_append(&sink,data+17,sizeof(data)-17,write_bytes,&f));
    assert(tonie_log_flush(&sink,write_bytes,&f));assert(f.calls==3 && f.used==sizeof(data));
    assert(!memcmp(f.data,data,sizeof(data)));
    assert(tonie_log_flush(&sink,write_bytes,&f) && f.calls==3);
    sink=(TonieLogSink){0};f=(Fake){.fail=true};
    assert(tonie_log_append(&sink,data,100,write_bytes,&f));
    assert(!tonie_log_flush(&sink,write_bytes,&f) && sink.failed);
    assert(!tonie_log_append(&sink,data,100,write_bytes,&f));assert(f.calls==1);
    puts("PASS: log batching, exact bytes, partial write failure and sticky error state");
}
