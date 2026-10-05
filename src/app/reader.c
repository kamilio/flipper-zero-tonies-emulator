#include "reader.h"
#include "read_check.h"
#include <assets_icons.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/popup.h>
#include <nfc/nfc_device.h>
#include <nfc/nfc_poller.h>
#include <nfc/protocols/slix/slix_poller.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

enum { ReaderPopup, ReaderHistorySize = 16 };
static const NotificationSequence reader_saved = {
    &message_force_vibro_setting_on,
    &message_red_0, &message_blue_0, &message_green_255,
    &message_vibro_on, &message_delay_100, &message_vibro_off,
    &message_delay_100, &message_green_0, &message_blue_255,
    &message_do_not_reset, NULL,
};

typedef struct {
    ViewDispatcher* dispatcher;
    Popup* popup;
    Nfc* nfc;
    NfcPoller* poller;
    NfcDevice* device;
    Storage* storage;
    FuriString* directory;
    NotificationApp* notifications;
    atomic_bool ready;
    atomic_bool retry;
    atomic_bool cancelled;
    const char* read_warning;
    unsigned password_index;
    struct { uint8_t uid[8]; char name[40]; bool unchecked; } recent[ReaderHistorySize];
    unsigned recent_count;
    unsigned recent_next;
    bool save_pending;
    uint32_t retry_at;
    uint32_t saved_count;
    char status[64];
    char name[40];
} Reader;

/* Keep filenames rather than sixteen full NFC snapshots. Re-read and compare
 * the entire saved device (blocks and metadata), never UID alone. */
static bool reader_seen(Reader* reader) {
    const Iso15693_3Data* iso = nfc_device_get_data(reader->device, NfcProtocolIso15693_3);
    for(unsigned i = 0; i < reader->recent_count; ++i) {
        if(memcmp(reader->recent[i].uid, iso->uid, 8) != 0 ||
           reader->recent[i].unchecked != (reader->read_warning != NULL)) continue;
        FuriString* path = furi_string_alloc_printf("%s/%s.nfc", furi_string_get_cstr(reader->directory), reader->recent[i].name);
        NfcDevice* previous = nfc_device_alloc();
        bool equal = nfc_device_load(previous, furi_string_get_cstr(path)) &&
            nfc_device_is_equal(reader->device, previous);
        nfc_device_free(previous);
        furi_string_free(path);
        if(equal) return true;
    }
    return false;
}

static void reader_remember(Reader* reader, const uint8_t* uid) {
    memcpy(reader->recent[reader->recent_next].uid, uid, 8);
    snprintf(reader->recent[reader->recent_next].name, sizeof(reader->name), "%s", reader->name);
    reader->recent[reader->recent_next].unchecked = reader->read_warning != NULL;
    reader->recent_next = (reader->recent_next + 1) % ReaderHistorySize;
    if(reader->recent_count < ReaderHistorySize) ++reader->recent_count;
}

static void reader_stop(Reader* reader) {
    if(!reader->poller) return;
    /* Join the worker before touching its snapshot or releasing any context. */
    atomic_store(&reader->cancelled, true);
    nfc_poller_stop(reader->poller);
    nfc_poller_free(reader->poller);
    reader->poller = NULL;
}

/* Runs only in the NFC callback. Address every independent readback to the
 * detected UID so a chip swap cannot silently validate a different chip. */
static const char* reader_check(Reader* reader, SlixPoller* poller, const SlixData* data) {
    const Iso15693_3Data* iso = data->iso15693_3_data;
    const uint16_t blocks = iso15693_3_get_block_count(iso);
    const size_t bytes = simple_array_get_count(iso->block_data);
    if(!tonie_read_shape(blocks, iso15693_3_get_block_size(iso), bytes))
        return "Incomplete chip data.";
    const uint8_t* contents = simple_array_cget_data(iso->block_data);
    if(tonie_read_blank(contents, bytes))
        return "Chip data is blank.";
    BitBuffer* tx = bit_buffer_alloc(11);
    BitBuffer* rx = bit_buffer_alloc(64); /* Match the SDK's receive capacity. */
    const char* warning = NULL;
    uint8_t frame[11] = {0x22, 0x20};
    for(size_t i = 0; i < 8; ++i) frame[2 + i] = iso->uid[7 - i];
    for(uint16_t block = 0; block < blocks; ++block) {
        bool matches = false;
        frame[10] = (uint8_t)block;
        bit_buffer_reset(tx);
        bit_buffer_append_bytes(tx, frame, sizeof(frame));
        for(unsigned attempt = 0; attempt < 2; ++attempt) {
            if(atomic_load(&reader->cancelled)) break;
            bit_buffer_reset(rx);
            if(slix_poller_send_frame(poller, tx, rx, 500000) != SlixErrorNone) continue;
            if(bit_buffer_get_size_bytes(rx) != 5 || bit_buffer_get_byte(rx, 0) != 0) continue;
            if(memcmp(bit_buffer_get_data(rx) + 1, contents + block * 4U, 4) == 0) {
                matches = true;
                break;
            }
        }
        if(!matches) {
            warning = "Readback did not match.";
            break;
        }
    }
    bit_buffer_free(rx);
    bit_buffer_free(tx);
    return warning;
}

static NfcCommand reader_callback(NfcGenericEvent event, void* context) {
    Reader* reader = context;
    if(event.protocol != NfcProtocolSlix) return NfcCommandContinue;
    SlixPollerEvent* slix_event = event.event_data;
    if(slix_event->type == SlixPollerEventTypePrivacyUnlockRequest) {
        static const SlixPassword passwords[] = {0x5B6EFD7F, 0x0F0F0F0F};
        slix_event->data->privacy_password.password = passwords[reader->password_index++ % 2];
        slix_event->data->privacy_password.password_set = true;
    } else if(slix_event->type == SlixPollerEventTypeError) {
        /* Reallocate on the dispatcher thread: failed reads may leave cached
         * passwords or optional metadata in the firmware poller. */
        atomic_store(&reader->retry, true);
        return NfcCommandStop;
    } else if(slix_event->type == SlixPollerEventTypeReady) {
        const SlixData* data = nfc_poller_get_data(reader->poller);
        reader->read_warning = reader_check(reader, event.instance, data);
        if(atomic_load(&reader->cancelled)) return NfcCommandStop;
        nfc_device_set_data(reader->device, NfcProtocolSlix, data);
        atomic_store(&reader->ready, true);
        return NfcCommandStop;
    }
    /* No card, field loss and failed authentication remain cancellable and retry. */
    return NfcCommandContinue;
}

static void reader_show(Reader* reader, const char* header) {
    popup_reset(reader->popup);
    popup_set_icon(reader->popup, 0, 8, &I_NFC_manual_60x50);
    popup_set_header(reader->popup, header, 97, 8, AlignCenter, AlignTop);
    popup_set_text(reader->popup, reader->status, 94, 22, AlignCenter, AlignTop);
    view_dispatcher_switch_to_view(reader->dispatcher, ReaderPopup);
}

static void reader_start(Reader* reader) {
    reader->password_index = 0;
    atomic_store(&reader->cancelled, false);
    reader->read_warning = NULL;
    atomic_store(&reader->ready, false);
    atomic_store(&reader->retry, false);
    reader_show(reader, "Reading");
    reader->poller = nfc_poller_alloc(reader->nfc, NfcProtocolSlix);
    nfc_poller_start(reader->poller, reader_callback, reader);
}

/* UID names are reproducible; suffixes preserve any existing dump, even when
 * the same chip is intentionally read in a later session. Never overwrite. */
static bool reader_choose_name(Reader* reader) {
    const Iso15693_3Data* iso = nfc_device_get_data(reader->device, NfcProtocolIso15693_3);
    char base[32];
    snprintf(base, sizeof(base), "SLIX_%02X%02X%02X%02X%02X%02X%02X%02X%s",
        iso->uid[0], iso->uid[1], iso->uid[2], iso->uid[3],
        iso->uid[4], iso->uid[5], iso->uid[6], iso->uid[7], reader->read_warning ? "_unchecked" : "");
    for(unsigned suffix = 0; suffix < 1000; ++suffix) {
        if(suffix) snprintf(reader->name, sizeof(reader->name), "%s_%u", base, suffix);
        else snprintf(reader->name, sizeof(reader->name), "%s", base);
        FuriString* path = furi_string_alloc_printf("%s/%s.nfc", furi_string_get_cstr(reader->directory), reader->name);
        FS_Error status = storage_common_stat(reader->storage, furi_string_get_cstr(path), NULL);
        furi_string_free(path);
        if(status == FSE_NOT_EXIST) return true;
        if(status != FSE_OK) return false;
    }
    return false;
}

static bool reader_save(Reader* reader) {
    bool ok = reader_choose_name(reader);
    FuriString* path = furi_string_alloc_printf("%s/%s.nfc", furi_string_get_cstr(reader->directory), reader->name);
    FuriString* staging = furi_string_alloc_printf("%s/.tonie-read-%lu.tmp", furi_string_get_cstr(reader->directory), (unsigned long)furi_get_tick());
    bool owned = false;
    if(ok) {
        owned = storage_common_stat(reader->storage, furi_string_get_cstr(staging), NULL) == FSE_NOT_EXIST;
        ok = owned && nfc_device_save(reader->device, furi_string_get_cstr(staging));
    }
    if(ok) {
        NfcDevice* check = nfc_device_alloc();
        ok = nfc_device_load(check, furi_string_get_cstr(staging)) && nfc_device_is_equal(reader->device, check);
        nfc_device_free(check);
    }
    if(ok) ok = storage_common_stat(reader->storage, furi_string_get_cstr(path), NULL) == FSE_NOT_EXIST &&
        storage_common_rename_safe(reader->storage, furi_string_get_cstr(staging), furi_string_get_cstr(path)) == FSE_OK;
    if(owned && !ok) storage_common_remove(reader->storage, furi_string_get_cstr(staging));
    furi_string_free(staging);
    furi_string_free(path);
    return ok;
}

static void reader_try_save(Reader* reader) {
    if(reader_save(reader)) {
        const Iso15693_3Data* iso = nfc_device_get_data(reader->device, NfcProtocolIso15693_3);
        reader_remember(reader, iso->uid);
        ++reader->saved_count;
        reader->save_pending = false;
        snprintf(reader->status, sizeof(reader->status), "Saved: %lu%s\nHold next card", (unsigned long)reader->saved_count, reader->read_warning ? "\nUnchecked read" : "");
        notification_message(reader->notifications, &reader_saved);
        reader_start(reader);
    } else {
        /* Retain the snapshot until SD recovers. No success feedback or history
         * insertion until the verified file has actually been published. */
        if(!reader->save_pending) notification_message(reader->notifications, &sequence_error);
        reader->save_pending = true;
        reader->retry_at = furi_get_tick();
        snprintf(reader->status, sizeof(reader->status), "Check SD card\nAuto retry...\nBack to exit");
        reader_show(reader, "Save failed");
    }
}

static void reader_tick(void* context) {
    Reader* reader = context;
    if(reader->save_pending) {
        if((uint32_t)(furi_get_tick() - reader->retry_at) >= furi_ms_to_ticks(1000))
            reader_try_save(reader);
        return;
    }
    if(atomic_load(&reader->retry)) {
        reader_stop(reader);
        reader_start(reader);
        return;
    }
    if(!atomic_load(&reader->ready)) return;
    reader_stop(reader);
    if(reader_seen(reader)) {
        snprintf(reader->status, sizeof(reader->status), "Duplicate skipped\nSaved: %lu\nHold next card", (unsigned long)reader->saved_count);
        reader_start(reader);
        return;
    }
    reader_try_save(reader);
}

static bool reader_back(void* context) {
    UNUSED(context);
    return false;
}

void tonie_reader_run(Gui* gui, Nfc* nfc, Storage* storage, NotificationApp* notifications, const char* directory) {
    furi_assert(directory && directory[0] == '/');
    Reader* reader = malloc(sizeof(Reader));
    memset(reader, 0, sizeof(*reader));
    atomic_init(&reader->ready, false);
    atomic_init(&reader->retry, false);
    atomic_init(&reader->cancelled, false);
    reader->nfc = nfc;
    reader->storage = storage;
    reader->directory = furi_string_alloc_set_str(directory);
    reader->notifications = notifications;
    reader->device = nfc_device_alloc();
    reader->dispatcher = view_dispatcher_alloc();
    reader->popup = popup_alloc();
    snprintf(reader->status, sizeof(reader->status), "Hold card next\nto Flipper's back");
    view_dispatcher_set_event_callback_context(reader->dispatcher, reader);
    view_dispatcher_set_navigation_event_callback(reader->dispatcher, reader_back);
    view_dispatcher_set_tick_event_callback(reader->dispatcher, reader_tick, 100);
    view_dispatcher_add_view(reader->dispatcher, ReaderPopup, popup_get_view(reader->popup));
    view_dispatcher_attach_to_gui(reader->dispatcher, gui, ViewDispatcherTypeFullscreen);
    notification_message(reader->notifications, &sequence_set_only_blue_255);
    reader_start(reader);
    view_dispatcher_run(reader->dispatcher);
    reader_stop(reader);
    notification_message_block(reader->notifications, &sequence_reset_rgb);
    view_dispatcher_remove_view(reader->dispatcher, ReaderPopup);
    popup_free(reader->popup);
    view_dispatcher_free(reader->dispatcher);
    nfc_device_free(reader->device);
    furi_string_free(reader->directory);
    free(reader);
}
