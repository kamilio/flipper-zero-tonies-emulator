#include "tonie_native.h"
#include "quiet_recovery.h"
#include "vendor/iso15693_3_listener_i.h"
#include "vendor/slix_listener_i.h"
#include <nfc/protocols/nfc_listener_base.h>

struct TonieNative {
    Nfc* nfc;
    NfcDevice* device;
    Iso15693_3Listener* iso;
    SlixListener* slix;
    TonieNativeTrace trace;
    void* context;
    TonieQuietRecovery recovery;
};
/* The NFC HAL permits one owner. Set before the worker starts; clear after join. */
static TonieNative* active;
static NfcError traced_tx(Nfc* nfc, const BitBuffer* buffer) {
    NfcError result = nfc_listener_tx(nfc, buffer);
    if(active) active->trace(active->context, 2, bit_buffer_get_data(buffer),
                             bit_buffer_get_size_bytes(buffer), result);
    return result;
}
#define nfc_listener_tx traced_tx
#include "vendor/iso15693_3_listener.inc"
#undef TAG
#include "vendor/iso15693_3_listener_i.inc"
#undef TAG
#include "vendor/slix_listener.inc"
#undef TAG
#include "vendor/slix_listener_i.inc"
#undef TAG
#undef nfc_listener_tx
#include "vendor/data_helpers.inc"

static NfcCommand receive(NfcEvent event, void* context) {
    TonieNative* native = context;
    uint8_t request[8];
    size_t request_size=0;
    bool valid=false;
    if(event.type == NfcEventTypeRxEnd) {
        const uint8_t* bytes=bit_buffer_get_data(event.data.buffer);
        size_t size=bit_buffer_get_size_bytes(event.data.buffer);
        native->trace(native->context, 1, bytes, size, 0);
        valid=iso13239_crc_check(Iso13239CrcTypeDefault,event.data.buffer);
        if(valid && size>=2) {
            request_size=size-2;
            memcpy(request,bytes,request_size>sizeof(request) ? sizeof(request) : request_size);
            if(native->slix && tonie_quiet_recover(&native->recovery,request,request_size,
                    native->iso->state==Iso15693_3ListenerStateQuiet)) {
                iso15693_3_listener_ready(native->iso);
                native->trace(native->context,4,NULL,0,0);
            }
        }
    }
    NfcGenericEvent generic = {.protocol=NfcProtocolInvalid, .instance=native->nfc, .event_data=&event};
    NfcCommand result = iso15693_3_listener_run(generic, native->iso);
    if(valid && native->slix) tonie_quiet_observe(&native->recovery,request,request_size,
        native->iso->state==Iso15693_3ListenerStateQuiet,
        native->slix->session_state.password_match[SlixPasswordTypePrivacy]);
    if(result != NfcCommandContinue) native->trace(native->context, 3, NULL, 0, result);
    return result;
}
TonieNative* tonie_native_alloc(Nfc* nfc, const NfcDevice* device, TonieNativeTrace trace, void* context) {
    TonieNative* native = malloc(sizeof(TonieNative));
    memset(native, 0, sizeof(*native));
    native->nfc=nfc; native->trace=trace; native->context=context;
    native->device=nfc_device_alloc();
    NfcProtocol protocol=nfc_device_get_protocol(device);
    nfc_device_set_data(native->device,protocol,nfc_device_get_data(device,protocol));
    Iso15693_3Data* iso=(Iso15693_3Data*)nfc_device_get_data(native->device,NfcProtocolIso15693_3);
    native->iso=iso15693_3_listener_alloc(nfc,iso);
    if(protocol==NfcProtocolSlix) {
        native->slix=slix_listener_alloc(native->iso,(SlixData*)nfc_device_get_data(native->device,protocol));
        iso15693_3_listener_set_callback(native->iso,slix_listener_run,native->slix);
    }
    return native;
}
void tonie_native_start(TonieNative* native) {active=native;nfc_start(native->nfc,receive,native);}
void tonie_native_stop(TonieNative* native) {nfc_stop(native->nfc);active=NULL;}
void tonie_native_free(TonieNative* native) {
    /* Match the firmware's parent-first listener teardown. */
    iso15693_3_listener_free(native->iso);
    if(native->slix) slix_listener_free(native->slix);
    nfc_device_free(native->device);free(native);
}
