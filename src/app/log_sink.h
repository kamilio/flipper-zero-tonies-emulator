#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef size_t (*TonieLogWrite)(void*,const void*,size_t);
typedef struct {uint8_t bytes[4096];size_t used;bool failed;} TonieLogSink;
static inline bool tonie_log_flush(TonieLogSink* sink,TonieLogWrite write,void* context) {
    if(sink->failed) return false;
    if(sink->used && write(context,sink->bytes,sink->used)!=sink->used) sink->failed=true;
    sink->used=0;return !sink->failed;
}
static inline bool tonie_log_append(TonieLogSink* sink,const void* data,size_t length,TonieLogWrite write,void* context) {
    const uint8_t* source=data;
    while(length && !sink->failed) {
        if(sink->used==sizeof(sink->bytes) && !tonie_log_flush(sink,write,context)) return false;
        size_t n=sizeof(sink->bytes)-sink->used;if(n>length)n=length;
        memcpy(sink->bytes+sink->used,source,n);sink->used+=n;source+=n;length-=n;
    }
    return !sink->failed;
}
