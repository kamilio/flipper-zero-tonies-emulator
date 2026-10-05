#!/usr/bin/env python3
"""Production batch reader with a file-backed device mock and injected failures."""
from pathlib import Path
import subprocess, tempfile, os
root = Path(__file__).resolve().parents[1]
source = Path(os.environ.get('READER_SOURCE', root / 'src/app/reader.c')).read_text()
def function(result, name):
    start=source.index(f'static {result} {name}(')
    return source[start:source.index('\n}',start)+2]
preamble=r'''
#include <assert.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define UNUSED(x) (void)(x)
typedef int ViewDispatcher, Popup, Nfc, Storage, NotificationApp;
typedef struct {char text[2048];} FuriString;
typedef struct {uint8_t uid[8], blocks[128], metadata[16];} Iso15693_3Data;
typedef Iso15693_3Data NfcDevice;
typedef Iso15693_3Data NfcPoller;
typedef struct {Iso15693_3Data* iso15693_3_data;} SlixData;
typedef int SlixPoller;
typedef unsigned SlixPassword;
typedef enum {FSE_OK,FSE_NOT_EXIST,FSE_ERROR} FS_Error;
typedef enum {NfcCommandContinue,NfcCommandStop} NfcCommand;
enum {NfcProtocolSlix,NfcProtocolIso15693_3,AlignCenter,AlignTop};
enum {SlixPollerEventTypePrivacyUnlockRequest,SlixPollerEventTypeError,SlixPollerEventTypeReady};
typedef struct {struct {unsigned password;bool password_set;} privacy_password;} SlixPollerEventData;
typedef struct {int type;SlixPollerEventData* data;} SlixPollerEvent;
typedef struct {int protocol;void* event_data;void* instance;} NfcGenericEvent;
static unsigned tick,successes,errors,starts,stops,checks,snapshots;
static bool live;
static int sequence_error, reader_saved, I_NFC_manual_60x50;
static NfcDevice chip;
static SlixData chip_slix={&chip};
static const char* warning;
static FuriString* furi_string_alloc_printf(const char* fmt,...) {
 FuriString* p=calloc(1,sizeof(*p));va_list ap;va_start(ap,fmt);vsnprintf(p->text,sizeof(p->text),fmt,ap);va_end(ap);return p;
}
static const char* furi_string_get_cstr(const FuriString* s){return s->text;}
static void furi_string_free(FuriString* s){free(s);}
static uint32_t furi_get_tick(void){return tick;}
static uint32_t furi_ms_to_ticks(uint32_t ms){return ms;}
static void popup_reset(Popup* p){UNUSED(p);}
static void popup_set_icon(Popup* p,int x,int y,const int* i){UNUSED(p);UNUSED(x);UNUSED(y);UNUSED(i);}
static void popup_set_header(Popup* p,const char* t,int x,int y,int a,int b){UNUSED(p);UNUSED(t);UNUSED(x);UNUSED(y);UNUSED(a);UNUSED(b);}
static void popup_set_text(Popup* p,const char* t,int x,int y,int a,int b){UNUSED(p);UNUSED(t);UNUSED(x);UNUSED(y);UNUSED(a);UNUSED(b);}
static void view_dispatcher_switch_to_view(ViewDispatcher* d,int v){UNUSED(d);UNUSED(v);}
static void nfc_poller_stop(NfcPoller* p){UNUSED(p);assert(live);live=false;++stops;}
static void nfc_poller_free(NfcPoller* p){UNUSED(p);assert(!live);}
static NfcPoller* nfc_poller_alloc(Nfc* n,int p){UNUSED(n);UNUSED(p);assert(!live);return &chip;}
static void nfc_poller_start(NfcPoller* p,NfcCommand (*cb)(NfcGenericEvent,void*),void* ctx){UNUSED(p);UNUSED(cb);UNUSED(ctx);assert(!live);live=true;++starts;}
static const SlixData* nfc_poller_get_data(NfcPoller* p){UNUSED(p);return &chip_slix;}
static const Iso15693_3Data* nfc_device_get_data(NfcDevice* d,int p){UNUSED(p);return d;}
static void nfc_device_set_data(NfcDevice* d,int p,const SlixData* s){UNUSED(p);*d=*s->iso15693_3_data;++snapshots;}
static NfcDevice* nfc_device_alloc(void){return calloc(1,sizeof(NfcDevice));}
static void nfc_device_free(NfcDevice* d){free(d);}
static void notification_message(NotificationApp* n,const int* seq){UNUSED(n);if(seq==&reader_saved)++successes;else {assert(seq==&sequence_error);++errors;}}
/* Mock persistent files preserve every byte. Existing names are never overwritten. */
typedef struct {bool used;char path[2048];NfcDevice data;} File;
static File files[1200];
static int failure;
static unsigned stat_calls;
static bool unreadable_history;
static const char* expected_directory;
static void check_path(const char* p){size_t n=strlen(expected_directory);assert(!strncmp(p,expected_directory,n)&&p[n]=='/');}
static File* find_file(const char* p){for(unsigned i=0;i<1200;++i)if(files[i].used&&!strcmp(files[i].path,p))return &files[i];return NULL;}
static File* add_file(const char* p,const NfcDevice* d){assert(!find_file(p));for(unsigned i=0;i<1200;++i)if(!files[i].used){files[i].used=true;snprintf(files[i].path,sizeof(files[i].path),"%s",p);files[i].data=*d;return &files[i];}abort();}
static FS_Error storage_common_stat(Storage* s,const char* p,void* info){UNUSED(s);UNUSED(info);check_path(p);++stat_calls;if(failure==1)return FSE_ERROR;return find_file(p)?FSE_OK:FSE_NOT_EXIST;}
static bool nfc_device_save(NfcDevice* d,const char* p){check_path(p);add_file(p,d);return failure!=2;}
static bool nfc_device_load(NfcDevice* d,const char* p){check_path(p);File* f=find_file(p);if(!f||failure==3||(unreadable_history&&!strstr(p,".tmp")))return false;*d=f->data;return true;}
static bool nfc_device_is_equal(NfcDevice* a,NfcDevice* b){return failure!=4&&!memcmp(a,b,sizeof(*a));}
static FS_Error storage_common_rename_safe(Storage* s,const char* a,const char* b){UNUSED(s);check_path(a);check_path(b);if(failure==6&&!find_file(b))add_file(b,&chip);if(failure==5||find_file(b))return FSE_ERROR;File* f=find_file(a);assert(f);snprintf(f->path,sizeof(f->path),"%s",b);return FSE_OK;}
static void storage_common_remove(Storage* s,const char* p){UNUSED(s);assert(strstr(p,".tmp"));File* f=find_file(p);if(f)f->used=false;}
'''
structure=source[source.index('typedef struct {'):source.index('/* Keep filenames')]
check=r'''
static const char* reader_check(Reader* r,SlixPoller* p,const SlixData* d){UNUSED(r);UNUSED(p);UNUSED(d);++checks;return warning;}
'''
main=r'''
static void scan(Reader* r,unsigned uid,unsigned content,unsigned meta,const char* warn){
 assert(live);memset(&chip,0,sizeof(chip));chip.uid[0]=uid;chip.uid[1]=uid>>8;chip.blocks[0]=content;chip.metadata[0]=meta;warning=warn;
 SlixPollerEvent event={.type=SlixPollerEventTypeReady};
 NfcGenericEvent generic={.protocol=NfcProtocolSlix,.event_data=&event};
 assert(reader_callback(generic,r)==NfcCommandStop);reader_tick(r);++tick;
}
static unsigned file_count(void){unsigned n=0;for(unsigned i=0;i<1200;++i)if(files[i].used){assert(!strstr(files[i].path,".tmp"));++n;}return n;}
static void reset(Reader* r,NfcDevice* device,FuriString* dir){
 if(live)reader_stop(r);memset(r,0,sizeof(*r));memset(files,0,sizeof(files));r->device=device;r->directory=dir;expected_directory=dir->text;
 successes=errors=0;failure=0;warning=NULL;unreadable_history=false;reader_start(r);
}
int main(void){
 NfcDevice device={0};FuriString dir={"/ext/nfc/tonies/Nested folder/é"};Reader r={0};reset(&r,&device,&dir);
 SlixPollerEventData data={0};SlixPollerEvent event={SlixPollerEventTypePrivacyUnlockRequest,&data};NfcGenericEvent generic={NfcProtocolSlix,&event,NULL};
 for(unsigned i=0;i<8;++i){assert(reader_callback(generic,&r)==NfcCommandContinue);assert(data.privacy_password.password==(i%2?0x0F0F0F0F:0x5B6EFD7F));assert(data.privacy_password.password_set);}
 scan(&r,1,1,1,NULL);assert(successes==1&&file_count()==1&&live);
 assert(strstr(files[0].path,"SLIX_0100000000000000.nfc"));
 for(unsigned i=0;i<20;++i)scan(&r,1,1,1,NULL);
 assert(successes==1&&file_count()==1&&checks>=21); // Never dedup on UID before checking bytes.
 scan(&r,1,2,1,NULL);scan(&r,1,2,2,NULL);assert(successes==3&&file_count()==3);
 scan(&r,1,1,1,NULL);assert(successes==3); // Match any recent version, not just newest.
 scan(&r,1,1,1,"Bad readback");assert(successes==4&&strstr(files[3].path,"_unchecked"));
 scan(&r,1,1,1,"Bad readback");assert(successes==4);
 scan(&r,2,5,0,"Incomplete");scan(&r,2,5,0,NULL);assert(successes==6); // Preserve later verified scan too.
 unreadable_history=true;scan(&r,1,1,1,NULL);assert(successes==7);unreadable_history=false;
 reset(&r,&device,&dir);
 for(unsigned i=1;i<=16;++i)scan(&r,i,i,0,NULL);
 for(unsigned i=1;i<=16;++i)scan(&r,i,i,0,NULL);
 assert(file_count()==16&&r.recent_count==16);
 scan(&r,17,17,0,NULL);scan(&r,1,1,0,NULL);assert(file_count()==18); // Eviction keeps bounded history.
 for(int f=1;f<=6;++f){
  reset(&r,&device,&dir);failure=f;scan(&r,42,9,2,NULL);
  assert(r.save_pending&&!live&&successes==0&&r.recent_count==0);NfcDevice snapshot=device;
  unsigned before=starts, stats=stat_calls;tick+=998;reader_tick(&r);assert(starts==before && stat_calls==stats);
  failure=0;tick+=1000;reader_tick(&r);assert(!r.save_pending&&live&&successes==1&&!memcmp(&snapshot,&device,sizeof(device)));
  assert(r.recent_count==1);reader_stop(&r);
 }
 // Compare every data/metadata byte, not a prefix or only a UID.
 reset(&r,&device,&dir);scan(&r,7,1,2,NULL);
 for(unsigned byte=0;byte<sizeof(chip.blocks)+sizeof(chip.metadata);++byte){
  if(byte<sizeof(chip.blocks))chip.blocks[byte]^=0x80;
  else chip.metadata[byte-sizeof(chip.blocks)]^=0x80;
  SlixPollerEvent changed={.type=SlixPollerEventTypeReady};
  NfcGenericEvent change_event={NfcProtocolSlix,&changed,NULL};
  assert(reader_callback(change_event,&r)==NfcCommandStop);reader_tick(&r);++tick;
  assert(successes==byte+2 && live);
 }
 // Prior file deleted or changed on SD: never silently drop a fresh scan.
 reset(&r,&device,&dir);scan(&r,8,8,0,NULL);files[0].used=false;
 scan(&r,8,8,0,NULL);assert(successes==2);
 files[0].data.blocks[20]^=1;scan(&r,8,8,0,NULL);assert(successes==3);
 // Retry timing works across the 32-bit tick wrap; no repeated SD access each tick.
 reset(&r,&device,&dir);tick=UINT32_MAX-500;failure=2;scan(&r,9,9,0,NULL);
 unsigned stats=stat_calls;tick+=998;reader_tick(&r);assert(stat_calls==stats);
 failure=0;tick+=1;reader_tick(&r);assert(successes==1 && live);
 // Cancel a pending save, or a callback completing after cancellation: no success/history.
 reset(&r,&device,&dir);failure=1;scan(&r,10,10,0,NULL);
 assert(!reader_back(&r));reader_stop(&r);assert(!successes && !r.recent_count && !file_count());
 reset(&r,&device,&dir);unsigned copies=snapshots;atomic_store(&r.cancelled,true);
 event.type=SlixPollerEventTypeReady;
 assert(reader_callback(generic,&r)==NfcCommandStop && !atomic_load(&r.ready) && snapshots==copies);
 reader_stop(&r);
 // Do not remove staging files owned by somebody else.
 reset(&r,&device,&dir);char stage[2048];snprintf(stage,sizeof(stage),"%s/.tonie-read-%u.tmp",dir.text,tick);File* foreign=add_file(stage,&chip);
 scan(&r,4,4,4,NULL);assert(r.save_pending&&foreign->used&&successes==0);foreign->used=false;tick+=1000;reader_tick(&r);assert(successes==1);
 reset(&r,&device,&dir);
 for(unsigned i=0;i<1000;++i){scan(&r,i+100,i,0,NULL);scan(&r,i+100,i,0,NULL);assert(live&&!r.save_pending&&successes==i+1);}
 assert(file_count()==1000);
 event.type=SlixPollerEventTypeError;reader_callback(generic,&r);unsigned before=starts;reader_tick(&r);assert(starts==before+1&&live);
 assert(!reader_back(&r));reader_stop(&r);unsigned stopped=stops;reader_stop(&r);assert(stops==stopped);
 for(unsigned i=0;i<3;++i){snprintf(dir.text,sizeof(dir.text),"%s",i==0?"/ext/nfc":i==1?"/ext/nfc/empty":"/ext/nfc/flat-scope");reset(&r,&device,&dir);scan(&r,1,2,3,NULL);assert(successes==1);reader_stop(&r);}
 puts("PASS batch reader: automatic UID names/current directory, complete-data/metadata duplicate comparison, 16-entry history/eviction, unchecked variants, collisions, SD/save/load/verify/rename failures and retained retries, every data/metadata byte, missing/changed prior files, retry clock wrap, cancellation, 1000 continuous scans, password retries and Back");
}
'''
functions=[('bool','reader_seen'),('void','reader_remember'),('void','reader_stop'),('NfcCommand','reader_callback'),('void','reader_show'),('void','reader_start'),('bool','reader_choose_name'),('bool','reader_save'),('void','reader_try_save'),('void','reader_tick'),('bool','reader_back')]
with tempfile.TemporaryDirectory(prefix='tonie-batch-tests-') as tmp:
 c=Path(tmp)/'test.c';exe=Path(tmp)/'test';c.write_text(preamble+'\nenum {ReaderPopup,ReaderHistorySize=16};\n'+structure+check+'\n'.join(function(*f) for f in functions)+main)
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
