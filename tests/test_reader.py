#!/usr/bin/env python3
"""Run production batch-save transitions against injected SD failures."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/app/reader.c').read_text()
def function(result, name):
    start = source.index(f'static {result} {name}(')
    return source[start:source.index('\n}', start) + 2]

preamble = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define READ_DIR "/ext/nfc"
enum {FSE_OK, FSE_NOT_EXIST, FSE_ERROR};
enum {AlignCenter, AlignTop, ReaderPopup, ReaderName};
static void popup_reset(void* p) {(void)p;}
static void popup_set_header(void* p,const char* t,int x,int y,int a,int b) {(void)p;(void)t;(void)x;(void)y;(void)a;(void)b;}
static void view_dispatcher_switch_to_view(void* p,int v) {(void)p;(void)v;}
static unsigned submitted_events, stopped, freed, named;
static bool poller_live;
static void nfc_poller_stop(void* p) {(void)p;assert(poller_live);poller_live=false;++stopped;}
static void nfc_poller_free(void* p) {(void)p;assert(!poller_live);++freed;}
static void view_dispatcher_send_custom_event(void* p,int v) {(void)p;assert(v==1);++submitted_events;}
typedef struct { char text[256]; } FuriString;
typedef struct { uint8_t uid[8]; } Iso15693_3Data;
typedef Iso15693_3Data NfcDevice;
typedef NfcDevice SlixData;
enum {NfcProtocolIso15693_3, NfcProtocolSlix};
typedef unsigned SlixPassword;
typedef enum {NfcCommandContinue, NfcCommandStop} NfcCommand;
enum {SlixPollerEventTypePrivacyUnlockRequest, SlixPollerEventTypeReady, SlixPollerEventTypeError};
typedef struct {struct {SlixPassword password;bool password_set;} privacy_password;} SlixPollerEventData;
typedef struct {int type;SlixPollerEventData* data;} SlixPollerEvent;
typedef struct {int protocol;void* event_data;void* instance;} NfcGenericEvent;
static void nfc_device_set_data(NfcDevice* d,int protocol,const NfcDevice* source) {assert(protocol==NfcProtocolSlix);*d=*source;}
static const NfcDevice* nfc_poller_get_data(NfcDevice* p) {return p;}
typedef struct {
    bool naming, have_previous;
    atomic_bool ready, save_pending, retry, cancelled;
    const char* read_warning;
    unsigned password_index;
    NfcDevice* poller;
    unsigned starts;
    uint8_t previous_uid[8];
    char name[64], submitted_name[64];
    void *storage, *notifications, *input, *popup, *dispatcher;
    NfcDevice* device;
} Reader;
static const char* reader_check(Reader* r,void* p,const SlixData* d) {(void)r;(void)p;(void)d;return NULL;}
static bool save_choice;
static unsigned warnings;
typedef int DialogsApp;
typedef int DialogMessage;
enum {RECORD_DIALOGS, DialogMessageButtonLeft, DialogMessageButtonCenter};
static DialogsApp dialogs;
static DialogMessage message;
static unsigned dialog_allocations;
static void* furi_record_open(int record) {assert(record==RECORD_DIALOGS);return &dialogs;}
static void furi_record_close(int record) {assert(record==RECORD_DIALOGS);}
static DialogMessage* dialog_message_alloc(void) {++dialog_allocations;return &message;}
static void dialog_message_free(DialogMessage* m) {assert(m==&message && dialog_allocations);--dialog_allocations;}
static void dialog_message_set_header(DialogMessage* m,const char* t,int x,int y,int a,int b) {
    (void)m;(void)x;(void)y;(void)a;(void)b;assert(!strcmp(t,"Possibly corrupted"));
}
static void dialog_message_set_text(DialogMessage* m,const char* t,int x,int y,int a,int b) {
    (void)m;(void)x;(void)y;(void)a;(void)b;assert(t);
}
static void dialog_message_set_buttons(DialogMessage* m,const char* left,const char* center,const char* right) {
    (void)m;assert(!strcmp(left,"Skip") && center && !strcmp(center,"Save anyway") && !right);
}
static int dialog_message_show(DialogsApp* d,DialogMessage* m) {
    assert(d==&dialogs && m==&message);++warnings;
    return save_choice ? DialogMessageButtonCenter : DialogMessageButtonLeft;
}
static int failure, saves, removes, publishes, successes, errors;
static bool stage_exists, destination_exists;
static char header[64];
static int sequence_success, sequence_error;
static FuriString* furi_string_alloc(void) {return calloc(1,sizeof(FuriString));}
static FuriString* furi_string_alloc_printf(const char* format, ...) {
    FuriString* s=furi_string_alloc(); va_list args; va_start(args,format);
    vsnprintf(s->text,sizeof(s->text),format,args); va_end(args); return s;
}
static void furi_string_free(FuriString* s) {free(s);}
static void furi_string_set(FuriString* s,const char* t) {snprintf(s->text,sizeof(s->text),"%s",t);}
static const char* furi_string_get_cstr(FuriString* s) {return s->text;}
static unsigned furi_get_tick(void) {return 42;}
static int storage_common_stat(void* storage,const char* path,void* out) {
    (void)storage;(void)out;
    if(failure==1) return FSE_ERROR;
    return (strstr(path,".tmp") ? stage_exists : destination_exists) ? FSE_OK : FSE_NOT_EXIST;
}
static bool nfc_device_save(NfcDevice* d,const char* path) {
    (void)d;(void)path; ++saves;stage_exists=true;return failure!=2;
}
static NfcDevice* nfc_device_alloc(void) {return calloc(1,sizeof(NfcDevice));}
static bool nfc_device_load(NfcDevice* d,const char* p) {(void)d;(void)p;return failure!=3;}
static bool nfc_device_is_equal(NfcDevice* a,NfcDevice* b) {(void)a;(void)b;return failure!=4;}
static void nfc_device_free(NfcDevice* d) {free(d);}
static int storage_common_rename_safe(void* s,const char* a,const char* b) {
    (void)s;(void)a;(void)b;assert(!destination_exists);
    if(failure==6) destination_exists=true;
    if(failure==5 || failure==6) return FSE_ERROR;
    ++publishes;stage_exists=false;destination_exists=true;return FSE_OK;
}
static void storage_common_remove(void* s,const char* p) {
    (void)s;assert(strstr(p,".tmp"));++removes;stage_exists=false;
}
static const void* nfc_device_get_data(NfcDevice* d,int protocol) {(void)protocol;return d;}
static void notification_message(void* n,void* sequence) {
    (void)n;if(sequence==&sequence_success) ++successes;
    else {assert(sequence==&sequence_error);++errors;}
}
static void reader_start(Reader* r) {
    assert(!poller_live);++r->starts;r->naming=false;
    r->poller=r->device;poller_live=true;r->read_warning=NULL;atomic_store(&r->cancelled,false);
    atomic_store(&r->ready,false);atomic_store(&r->retry,false);
}
static void text_input_reset(void* i) {(void)i;}
static void text_input_set_minimum_length(void* i,size_t n) {(void)i;assert(n==1);}
static void name_generator_make_auto(char* name,size_t size,const char* prefix) {snprintf(name,size,"%s_Test",prefix);}
static void text_input_set_result_callback(void* i,void (*cb)(void*),void* r,char* name,size_t size,bool clear) {
    (void)i;(void)cb;(void)r;(void)name;assert(size==64 && clear);++named;
}
static void text_input_set_validator(void* i,bool (*cb)(const char*,FuriString*,void*),void* r) {(void)i;(void)cb;(void)r;}

static void text_input_set_header_text(void* i,const char* t) {(void)i;snprintf(header,sizeof(header),"%s",t);}
'''
main = r'''
int main(void) {
    NfcDevice device={{1,2,3,4,5,6,7,8}};
    Reader reader={.device=&device};strcpy(reader.name,"Test");
    NfcDevice chip={{8,7,6,5,4,3,2,1}};
    reader.poller=&chip;atomic_init(&reader.ready,false);
    SlixPollerEventData data={0};
    SlixPollerEvent slix={SlixPollerEventTypePrivacyUnlockRequest,&data};
    NfcGenericEvent event={NfcProtocolSlix,&slix,NULL};
    for(unsigned i=0;i<20;++i) {
        assert(reader_callback(event,&reader)==NfcCommandContinue);
        assert(data.privacy_password.password_set);
        assert(data.privacy_password.password==(i%2 ? 0x0F0F0F0F : 0x5B6EFD7F));
        assert(!atomic_load(&reader.ready));
    }
    slix.type=SlixPollerEventTypeError;
    assert(reader_callback(event,&reader)==NfcCommandStop && !atomic_load(&reader.ready));
    assert(atomic_load(&reader.retry));
    slix.type=SlixPollerEventTypeReady;
    assert(reader_callback(event,&reader)==NfcCommandStop && atomic_load(&reader.ready));
    assert(!memcmp(device.uid,chip.uid,8));
    FuriString* error=furi_string_alloc();
    const char* bad[]={"",".","..","../x","a/b","a\\b","a:b","a\n"," ","card.","card ","a\x7f"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i) assert(!reader_validate(bad[i],error,&reader));
    assert(reader_validate("My Tonie-2",error,&reader));
    for(failure=0;failure<=6;++failure) {
        saves=removes=publishes=successes=errors=0;stage_exists=destination_exists=false;
        poller_live=false;reader.naming=true;reader.have_previous=false;reader.starts=0;
        assert((reader_name_done(&reader), reader_save(&reader,1)));
        if(!failure) {
            assert(!reader.naming && reader.starts==1 && reader.have_previous);
            assert(!memcmp(reader.previous_uid,device.uid,8));
            assert(publishes==1 && successes==1 && errors==0 && !stage_exists);
            /* A duplicate queued keyboard event cannot save again. */
            assert(!reader_save(&reader,1));assert(saves==1);
        } else {
            assert(reader.naming && !reader.starts && !reader.have_previous);
            assert(!publishes && !successes && errors==1 && !stage_exists);
            assert(strstr(header,"Save failed"));
            assert(removes==(failure>=2));
            /* Retry preserves the snapshot and succeeds without re-reading. */
            int injected=failure;failure=0;destination_exists=false;poller_live=false;assert((reader_name_done(&reader), reader_save(&reader,1)));failure=injected;
            assert(reader.starts==1 && destination_exists && successes==1);
        }
    }
    failure=0;poller_live=false;reader.naming=true;reader.starts=0;destination_exists=true;saves=0;
    assert((reader_name_done(&reader), reader_save(&reader,1)));assert(!saves && reader.naming && !reader.starts);
    destination_exists=false;stage_exists=true;
    assert((reader_name_done(&reader), reader_save(&reader,1)));assert(!saves && stage_exists && reader.naming);
    /* Rapid repeat confirms capture once, independent of later keyboard edits. */
    stage_exists=destination_exists=false;poller_live=false;reader.naming=true;reader.starts=0;
    atomic_store(&reader.save_pending,false);submitted_events=0;
    strcpy(reader.name,"Confirmed");reader_name_done(&reader);
    strcpy(reader.name,"Later edit");reader_name_done(&reader);
    assert(submitted_events==1 && !strcmp(reader.submitted_name,"Confirmed"));
    assert(reader_save(&reader,1) && reader.starts==1);
    /* Hundreds of distinct chips: save, leave the same chip present, then swap.
     * The actual tick/back/stop handlers must join before starting another worker. */
    reader_stop(&reader);
    unsigned before_stop=stopped;reader_stop(&reader);assert(stopped==before_stop);
    for(unsigned cycle=0;cycle<1000;++cycle) {
        device.uid[6]=(uint8_t)(cycle>>8);device.uid[7]=(uint8_t)cycle;
        reader.naming=false;reader_start(&reader);atomic_store(&reader.ready,true);
        unsigned before_named=named;reader_tick(&reader);
        assert(reader.naming && !poller_live && named==before_named+1);
        reader_tick(&reader);assert(named==before_named+1);
        stage_exists=destination_exists=false;
        reader_name_done(&reader);assert(reader_save(&reader,1));
        assert(!reader.naming && poller_live);
        atomic_store(&reader.ready,true);reader_tick(&reader);
        assert(!reader.naming && poller_live && named==before_named+1);
        /* Field/read error requires a fresh worker, without showing a name. */
        atomic_store(&reader.retry,true);unsigned before_start=reader.starts;
        reader_tick(&reader);assert(reader.starts==before_start+1 && !reader.naming);
        reader_stop(&reader);
    }
    assert(stopped==freed);
    /* Warnings are informational: Skip advances; Save anyway names and saves. */
    for(unsigned save_anyway=0;save_anyway<2;++save_anyway) {
        reader.have_previous=false;reader.naming=false;reader_start(&reader);
        reader.read_warning="Possibly corrupted";atomic_store(&reader.ready,true);
        save_choice=save_anyway;unsigned before_named=named;int before_saved=saves;
        reader_tick(&reader);
        assert(reader.naming==(bool)save_anyway);
        assert(named==before_named+save_anyway && saves==before_saved);
        if(save_anyway) {
            assert(!poller_live && !reader.have_previous);
            stage_exists=destination_exists=false;
            reader_name_done(&reader);assert(reader_save(&reader,1));
            assert(saves==before_saved+1 && poller_live && !reader.naming);
        } else assert(poller_live && reader.have_previous);
        reader_stop(&reader);
    }
    assert(warnings==2 && !dialog_allocations);
    reader.naming=true;poller_live=false;reader.poller=NULL;
    assert(reader_back(&reader) && poller_live && !reader.naming);
    assert(!reader_back(&reader));reader_stop(&reader);
    char long_name[65];memset(long_name,'x',sizeof(long_name));long_name[64]=0;
    destination_exists=false;
    assert(!reader_validate(long_name,error,&reader));long_name[63]=0;
    assert(reader_validate(long_name,error,&reader));
    furi_string_free(error);
    puts("Reader: Tommybox passwords, complete-read snapshot, name validation, no overwrite, save/load/verify/rename faults, retained scan retries 1000 scan/save/swap/error cycles, and warning OK-save/left-skip flow PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='tonie-reader-') as directory:
    c = Path(directory) / 'reader.c'
    binary = Path(directory) / 'reader'
    c.write_text(preamble + function('NfcCommand', 'reader_callback') + function('bool', 'reader_validate') + function('void', 'reader_name_done') + function('bool', 'reader_save') + function('void', 'reader_stop') + function('bool', 'reader_save_warning') + function('void', 'reader_tick') + function('bool', 'reader_back') + main)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', str(c), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
