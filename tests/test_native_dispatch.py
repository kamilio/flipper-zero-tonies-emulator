#!/usr/bin/env python3
"""Compile the actual embedded standard and extension dispatchers with host stubs.

This tests table indexing under sanitizers, not RF transport or hardware timing.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/native/vendor/iso15693_3_listener_i.inc").read_text()
start = source.index("static Iso15693_3Error iso15693_3_listener_handle_standard_request(")
end = source.index("\nstatic inline Iso15693_3Error", start)
PREAMBLE = r"""
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#define ISO15693_3_CMD_MANDATORY_START 1
#define ISO15693_3_CMD_MANDATORY_RFU 3
#define ISO15693_3_CMD_OPTIONAL_START 0x20
#define ISO15693_3_CMD_OPTIONAL_RFU 0x2d
#define ISO15693_3_RESP_FLAG_NONE 0
#define ISO15693_3_RESP_FLAG_ERROR 1
#define ISO15693_3_RESP_ERROR_UNKNOWN 0xf
 typedef enum {Iso15693_3ErrorNone,Iso15693_3ErrorNotSupported,Iso15693_3ErrorFullyHandled,Iso15693_3ErrorFormat,Iso15693_3ErrorIgnore} Iso15693_3Error;
 typedef struct {bool wait_for_eof;} Iso15693_3ListenerSessionState;
 typedef Iso15693_3Error (*Iso15693_3ExtensionHandler)(void*,va_list);
 typedef struct {Iso15693_3ExtensionHandler mandatory[2],optional[13];} ExtensionTable;
 typedef struct {void* tx_buffer;Iso15693_3ListenerSessionState session_state;ExtensionTable* extension_table;void* extension_context;} Iso15693_3Listener;
 typedef Iso15693_3Error (*Iso15693_3RequestHandler)(Iso15693_3Listener*,const uint8_t*,size_t,uint8_t);
 static struct {Iso15693_3RequestHandler mandatory[2],optional[13];} iso15693_3_handler_table;
 static void bit_buffer_reset(void* p){(void)p;}
 static void bit_buffer_append_byte(void* p,uint8_t b){(void)p;(void)b;}
 static Iso15693_3Error iso15693_3_listener_send_frame(Iso15693_3Listener* p,void* b){(void)p;(void)b;return Iso15693_3ErrorNone;}
"""
MAIN = r"""
static unsigned calls;
static Iso15693_3Error test_handler(Iso15693_3Listener* i,const uint8_t* d,size_t n,uint8_t f) {
    (void)i;(void)d;(void)n;(void)f;++calls;return Iso15693_3ErrorIgnore;
}
static Iso15693_3Error test_extension(void* context,va_list args) {
    (void)context;(void)args;++calls;return Iso15693_3ErrorIgnore;
}
int main(void) {
    ExtensionTable extensions={0};
    Iso15693_3Listener instance={0};
    for(unsigned i=0;i<2;++i) iso15693_3_handler_table.mandatory[i]=test_handler;
    for(unsigned i=0;i<13;++i) iso15693_3_handler_table.optional[i]=test_handler;
    for(unsigned i=0;i<2;++i) extensions.mandatory[i]=test_extension;
    for(unsigned i=0;i<13;++i) extensions.optional[i]=test_extension;
    instance.extension_table=&extensions;
    for(unsigned command=0;command<256;++command) {
        unsigned before=calls;
        Iso15693_3Error result=iso15693_3_listener_handle_standard_request(
            &instance,NULL,0,(uint8_t)command,2);
        bool supported=(command>=1 && command<3) || (command>=0x20 && command<0x2d);
        if(calls!=before+(supported ? 1U : 0U)) return 1;
        if(result!=(supported ? Iso15693_3ErrorIgnore : Iso15693_3ErrorNotSupported)) return 2;
        before=calls;
        result=iso15693_3_listener_extension_handler(&instance,command);
        if(calls!=before+(supported ? 1U : 0U)) return 3;
        if(result!=(supported ? Iso15693_3ErrorIgnore : Iso15693_3ErrorNone)) return 4;
    }
    puts("PASS: embedded dispatchers reject reserved commands; all 256 command indices checked");
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="tonie-dispatch-") as directory:
    c = Path(directory) / "dispatch.c"
    binary = Path(directory) / "dispatch"
    extension_start = source.index("static Iso15693_3Error\n    iso15693_3_listener_extension_handler(")
    extension_end = source.index("\nstatic Iso15693_3Error iso15693_3_listener_inventory_handler", extension_start)
    c.write_text(PREAMBLE + source[extension_start:extension_end] + source[start:end] + MAIN)
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-g",
                    str(c), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
