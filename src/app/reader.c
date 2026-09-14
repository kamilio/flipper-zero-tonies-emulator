#include "reader.h"
#include "read_check.h"
#include <dialogs/dialogs.h>
#include <assets_icons.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/popup.h>
#include <gui/modules/text_input.h>
#include <nfc/nfc_device.h>
#include <nfc/nfc_poller.h>
#include <nfc/protocols/slix/slix_poller.h>
#include <toolbox/name_generator.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define READ_DIR EXT_PATH("nfc")
enum { ReaderPopup, ReaderName };
typedef struct {
    ViewDispatcher* dispatcher;
    Popup* popup;
    TextInput* input;
    Nfc* nfc;
    NfcPoller* poller;
    NfcDevice* device;
    Storage* storage;
    NotificationApp* notifications;
    atomic_bool ready;
    atomic_bool retry;
    atomic_bool cancelled;
    const char* read_warning;
    atomic_bool save_pending;
    unsigned password_index;
    bool naming;
    bool have_previous;
    uint8_t previous_uid[8];
    char name[64];
    char submitted_name[64];
} Reader;

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

static void reader_start(Reader* reader) {
    reader->naming = false;
    reader->password_index = 0;
    atomic_store(&reader->cancelled, false);
    reader->read_warning = NULL;
    atomic_store(&reader->ready, false);
    atomic_store(&reader->retry, false);
    popup_reset(reader->popup);
    popup_set_icon(reader->popup, 0, 8, &I_NFC_manual_60x50);
    popup_set_header(reader->popup, "Reading", 97, 15, AlignCenter, AlignTop);
    popup_set_text(reader->popup,
        reader->have_previous ? "Hold next card\nto Flipper's back" : "Hold card next\nto Flipper's back",
        94, 27, AlignCenter, AlignTop);
    view_dispatcher_switch_to_view(reader->dispatcher, ReaderPopup);
    reader->poller = nfc_poller_alloc(reader->nfc, NfcProtocolSlix);
    nfc_poller_start(reader->poller, reader_callback, reader);
}

static bool reader_validate(const char* text, FuriString* error, void* context) {
    Reader* reader = context;
    const size_t length = strlen(text);
    bool invalid = !length || length >= sizeof(reader->name) || text[0] == '.' ||
        (length && (text[length - 1] == ' ' || text[length - 1] == '.'));
    for(const char* p = text; *p; ++p)
        if((unsigned char)*p < 32 || (unsigned char)*p == 127 || strchr("<>:\"/\\|?*", *p)) invalid = true;
    if(invalid) {
        furi_string_set(error, "Choose a valid\nfile name.");
        return false;
    }
    FuriString* path = furi_string_alloc_printf(READ_DIR "/%s.nfc", text);
    bool available = storage_common_stat(reader->storage, furi_string_get_cstr(path), NULL) == FSE_NOT_EXIST;
    furi_string_free(path);
    if(!available) furi_string_set(error, "Name exists or\nSD unavailable.\nChoose another\nname/check SD.");
    return available;
}

static void reader_name_done(void* context) {
    Reader* reader = context;
    /* Runs under the keyboard model lock. Capture exactly the confirmed name;
     * the GUI may still receive input before the dispatcher handles this event. */
    if(atomic_exchange(&reader->save_pending, true)) return;
    memcpy(reader->submitted_name, reader->name, sizeof(reader->submitted_name));
    view_dispatcher_send_custom_event(reader->dispatcher, 1);
}

static bool reader_save(void* context, uint32_t event) {
    Reader* reader = context;
    if(event != 1 || !reader->naming || !atomic_load(&reader->save_pending)) return false;
    popup_reset(reader->popup);
    popup_set_header(reader->popup, "Saving", 64, 25, AlignCenter, AlignTop);
    view_dispatcher_switch_to_view(reader->dispatcher, ReaderPopup);
    FuriString* error = furi_string_alloc();
    bool ok = reader_validate(reader->submitted_name, error, reader);
    FuriString* path = furi_string_alloc_printf(READ_DIR "/%s.nfc", reader->submitted_name);
    FuriString* staging = furi_string_alloc_printf(READ_DIR "/.tonie-read-%lu.tmp", (unsigned long)furi_get_tick());
    /* Verify a private staging file before publishing the user's named scan. */
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
    if(ok) ok = reader_validate(reader->submitted_name, error, reader) &&
        storage_common_rename_safe(reader->storage, furi_string_get_cstr(staging), furi_string_get_cstr(path)) == FSE_OK;
    if(owned && !ok) storage_common_remove(reader->storage, furi_string_get_cstr(staging));
    furi_string_free(staging);
    furi_string_free(path);
    furi_string_free(error);
    atomic_store(&reader->save_pending, false);
    if(ok) {
        const Iso15693_3Data* iso = nfc_device_get_data(reader->device, NfcProtocolIso15693_3);
        memcpy(reader->previous_uid, iso->uid, sizeof(reader->previous_uid));
        reader->have_previous = true;
        notification_message(reader->notifications, &sequence_success);
        reader_start(reader);
    } else {
        /* Keep the complete read in RAM and the name editable for a safe retry. */
        text_input_set_header_text(reader->input, "Save failed - check SD");
        view_dispatcher_switch_to_view(reader->dispatcher, ReaderName);
        notification_message(reader->notifications, &sequence_error);
    }
    return true;
}

static bool reader_save_warning(Reader* reader) {
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, "Possibly corrupted", 64, 2, AlignCenter, AlignTop);
    dialog_message_set_text(message, reader->read_warning, 64, 23, AlignCenter, AlignTop);
    dialog_message_set_buttons(message, "Skip", "Save anyway", NULL);
    const bool save = dialog_message_show(dialogs, message) == DialogMessageButtonCenter;
    dialog_message_free(message);
    furi_record_close(RECORD_DIALOGS);
    return save;
}

static void reader_tick(void* context) {
    Reader* reader = context;
    if(reader->naming) return;
    if(atomic_load(&reader->retry)) {
        reader_stop(reader);
        reader_start(reader);
        return;
    }
    if(!atomic_load(&reader->ready)) return;
    reader_stop(reader);
    const Iso15693_3Data* iso = nfc_device_get_data(reader->device, NfcProtocolIso15693_3);
    if(reader->have_previous && memcmp(reader->previous_uid, iso->uid, 8) == 0) {
        reader_start(reader);
        return;
    }
    if(reader->read_warning) {
        if(!reader_save_warning(reader)) {
            memcpy(reader->previous_uid, iso->uid, sizeof(reader->previous_uid));
            reader->have_previous = true;
            reader_start(reader);
            return;
        }
    }
    reader->naming = true;
    name_generator_make_auto(reader->name, sizeof(reader->name), "SLIX");
    text_input_reset(reader->input);
    text_input_set_header_text(reader->input, "Name the card");
    text_input_set_minimum_length(reader->input, 1);
    text_input_set_result_callback(reader->input, reader_name_done, reader, reader->name, sizeof(reader->name), true);
    text_input_set_validator(reader->input, reader_validate, reader);
    view_dispatcher_switch_to_view(reader->dispatcher, ReaderName);
}

static bool reader_back(void* context) {
    Reader* reader = context;
    if(reader->naming) {
        /* Back discards this unsaved read, without an extra confirmation. */
        const Iso15693_3Data* iso = nfc_device_get_data(reader->device, NfcProtocolIso15693_3);
        memcpy(reader->previous_uid, iso->uid, sizeof(reader->previous_uid));
        reader->have_previous = true;
        reader_start(reader);
        return true;
    }
    return false;
}

void tonie_reader_run(Gui* gui, Nfc* nfc, Storage* storage, NotificationApp* notifications) {
    Reader* reader = malloc(sizeof(Reader));
    memset(reader, 0, sizeof(*reader));
    atomic_init(&reader->ready, false);
    atomic_init(&reader->retry, false);
    atomic_init(&reader->cancelled, false);
    atomic_init(&reader->save_pending, false);
    reader->nfc = nfc;
    reader->storage = storage;
    reader->notifications = notifications;
    reader->device = nfc_device_alloc();
    reader->dispatcher = view_dispatcher_alloc();
    reader->popup = popup_alloc();
    reader->input = text_input_alloc();
    storage_common_mkdir(storage, READ_DIR);
    view_dispatcher_set_event_callback_context(reader->dispatcher, reader);
    view_dispatcher_set_navigation_event_callback(reader->dispatcher, reader_back);
    view_dispatcher_set_custom_event_callback(reader->dispatcher, reader_save);
    view_dispatcher_set_tick_event_callback(reader->dispatcher, reader_tick, 100);
    view_dispatcher_add_view(reader->dispatcher, ReaderPopup, popup_get_view(reader->popup));
    view_dispatcher_add_view(reader->dispatcher, ReaderName, text_input_get_view(reader->input));
    view_dispatcher_attach_to_gui(reader->dispatcher, gui, ViewDispatcherTypeFullscreen);
    reader_start(reader);
    view_dispatcher_run(reader->dispatcher);
    reader_stop(reader);
    view_dispatcher_remove_view(reader->dispatcher, ReaderPopup);
    view_dispatcher_remove_view(reader->dispatcher, ReaderName);
    text_input_free(reader->input);
    popup_free(reader->popup);
    view_dispatcher_free(reader->dispatcher);
    nfc_device_free(reader->device);
    free(reader);
}
