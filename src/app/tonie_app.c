#include "../protocol/tonie_protocol.h"
#include "log_sink.h"
#include "../native/tonie_native.h"
#include <stdatomic.h>

#include <dialogs/dialogs.h>
#include <furi.h>
#include <furi_hal_rtc.h>
#include <furi_hal_random.h>
#include <gui/canvas.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include <nfc/helpers/iso13239_crc.h>
#include <nfc/nfc.h>
#include <nfc/nfc_device.h>
#include <nfc/nfc_listener.h>
#include <nfc/protocols/iso15693_3/iso15693_3.h>
#include <nfc/protocols/slix/slix.h>
#include <storage/storage.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_DIR EXT_PATH("apps_data/tonie_emulator/logs")
#define NFC_DIR EXT_PATH("nfc")
#define DEFAULT_FDT_FC 4320U
#define FAST_FDT_FC 4096U

/* A quiet, steady indicator, scaled by the user's LED brightness setting. */
static const NotificationMessage message_emulating_blue = {
    .type = NotificationMessageTypeLedBlue,
    .data.led.value = 32,
};
static const NotificationSequence sequence_emulating = {
    &message_blink_stop,
    &message_red_0,
    &message_green_0,
    &message_emulating_blue,
    &message_do_not_reset,
    NULL,
};

typedef enum { TimingDefault, TimingFast, TimingCustom } TimingMode;

typedef struct {
    uint16_t len;
    uint8_t kind;
    uint8_t result;
    uint32_t ms;
    char text[254];
} LogLine;

typedef struct {
    Gui* gui;
    NotificationApp* notifications;
    DialogsApp* dialogs;
    Storage* storage;
    ViewPort* view_port;
    FuriMessageQueue* input_queue;
    FuriMessageQueue* log_queue;
    FuriThread* log_thread;
    File* log_file;
    Nfc* nfc;
    TonieNative* native_listener;
    NfcError last_tx_error;
    bool native_mode;
    BitBuffer* tx_buffer;
    NfcDevice* device;
    FuriString* file_path;
    TonieTag tag;
    TonieSession session;
    atomic_bool running;
    atomic_bool logger_running;
    atomic_bool log_io_error;
    TonieLogSink log_sink;
    atomic_uint_least32_t rx_count;
    atomic_uint_least32_t tx_count;
    atomic_uint_least32_t errors;
    atomic_uint_least32_t dropped_logs;
    uint32_t started_tick;
    TimingMode timing;
    uint32_t custom_fdt_fc;
    bool verbose;
    bool loaded;
    char status[32];
    char log_path[96];
} TonieApp;

static uint32_t elapsed_ms(const TonieApp* app) {
    if(!app->started_tick) return 0;
    const uint32_t ticks = furi_get_tick() - app->started_tick;
    return (uint32_t)(((uint64_t)ticks * 1000U) / furi_kernel_get_tick_frequency());
}

static void log_line(TonieApp* app, const char* format, ...) {
    if(!app->logger_running || !app->log_queue) return;
    if(app->log_io_error) {++app->dropped_logs;return;}
    LogLine line = {0};
    const uint32_t ms = elapsed_ms(app);
    int prefix = snprintf(line.text, sizeof(line.text), "[%03lu.%03lu] ",
                          (unsigned long)(ms / 1000U), (unsigned long)(ms % 1000U));
    if(prefix < 0) return;
    va_list args;
    va_start(args, format);
    int body = vsnprintf(line.text + prefix, sizeof(line.text) - (size_t)prefix, format, args);
    va_end(args);
    if(body < 0) return;
    size_t used = (size_t)prefix + (size_t)body;
    if(used >= sizeof(line.text) - 1U) used = sizeof(line.text) - 2U;
    line.text[used++] = '\n';
    line.text[used] = '\0';
    line.len = (uint16_t)used;
    if(furi_message_queue_put(app->log_queue, &line, 0) != FuriStatusOk) ++app->dropped_logs;
}

static void log_hex(TonieApp* app, const char* direction, const uint8_t* data, size_t len) {
    char hex[201] = {0};
    const size_t shown = len > 66U ? 66U : len;
    size_t pos = 0;
    for(size_t i = 0; i < shown && pos + 3U < sizeof(hex); ++i)
        pos += (size_t)snprintf(hex + pos, sizeof(hex) - pos, "%02X", data[i]);
    if(shown < len && pos + 4U < sizeof(hex)) memcpy(hex + pos, "...", 4);
    log_line(app, "%s %s bytes=%u", direction, hex, (unsigned)len);
}

static size_t log_write(void* context,const void* data,size_t size) {
    TonieApp* app=context;return storage_file_write(app->log_file,data,size);
}
static int32_t logger_worker(void* context) {
    TonieApp* app=context;LogLine line;
    uint32_t last_flush=furi_get_tick(),last_sync=last_flush;
    while(app->logger_running || furi_message_queue_get_count(app->log_queue)) {
        if(furi_message_queue_get(app->log_queue,&line,100)==FuriStatusOk) {
            if(line.kind) {
                char text[600];
                int used=snprintf(text,sizeof(text),"[%03lu.%03lu] %s result=%u bytes=%u ",
                    (unsigned long)(line.ms/1000),(unsigned long)(line.ms%1000),
                    line.kind==1 ? "RX" : line.kind==2 ? "TX" : line.kind==4 ? "QUIET_RECOVERY" : "WORKER_COMMAND",
                    line.result,line.len);
                static const char hex[]="0123456789ABCDEF";
                for(size_t i=0;i<line.len;++i) {
                    uint8_t byte=(uint8_t)line.text[i];
                    text[used++]=hex[byte>>4];text[used++]=hex[byte&15];
                }
                text[used++]='\n';
                if(!tonie_log_append(&app->log_sink,text,used,log_write,app)) app->log_io_error=true;
            } else if(!tonie_log_append(&app->log_sink,line.text,line.len,log_write,app)) app->log_io_error=true;
        }
        uint32_t now=furi_get_tick();
        if(now-last_flush>=furi_kernel_get_tick_frequency()/10) {
            if(!tonie_log_flush(&app->log_sink,log_write,app)) app->log_io_error=true;
            last_flush=now;
        }
        if(!app->log_io_error && now-last_sync>=furi_kernel_get_tick_frequency()) {
            if(!storage_file_sync(app->log_file)) app->log_io_error=true;
            last_sync=now;
        }
    }
    char footer[100];
    snprintf(footer,sizeof(footer),"# LOGGER_END dropped=%lu io_error=%u\n",
             (unsigned long)app->dropped_logs,(unsigned)app->log_io_error);
    if(!tonie_log_append(&app->log_sink,footer,strlen(footer),log_write,app) ||
       !tonie_log_flush(&app->log_sink,log_write,app) || !storage_file_sync(app->log_file))
        app->log_io_error=true;
    return 0;
}

static bool logger_start(TonieApp* app) {
    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    storage_common_mkdir(app->storage, EXT_PATH("apps_data/tonie_emulator"));
    storage_common_mkdir(app->storage, LOG_DIR);
    app->log_io_error=false;memset(&app->log_sink,0,sizeof(app->log_sink));
    app->log_file=storage_file_alloc(app->storage);
    bool opened=false;
    for(unsigned suffix=0;suffix<10000;++suffix) {
        snprintf(app->log_path,sizeof(app->log_path),
                 LOG_DIR "/%04u-%02u-%02u_%02u%02u%02u-%04u.log",
                 now.year,now.month,now.day,now.hour,now.minute,now.second,suffix);
        if(storage_common_stat(app->storage,app->log_path,NULL)==FSE_OK) continue;
        opened=storage_file_open(app->log_file,app->log_path,FSAM_WRITE,FSOM_CREATE_NEW);break;
    }
    if(!opened) {storage_file_free(app->log_file);app->log_file=NULL;return false;}
    app->log_queue = furi_message_queue_alloc(64, sizeof(LogLine));
    app->logger_running = true;
    app->log_thread = furi_thread_alloc_ex("TonieLog", 4096, logger_worker, app);
    furi_thread_start(app->log_thread);
    return true;
}

static void logger_stop(TonieApp* app) {
    if(!app->logger_running) return;
    app->logger_running = false;
    furi_thread_join(app->log_thread);
    furi_thread_free(app->log_thread);
    app->log_thread = NULL;
    furi_message_queue_free(app->log_queue);
    app->log_queue = NULL;
    if(!storage_file_close(app->log_file)) app->log_io_error=true;
    storage_file_free(app->log_file);
    app->log_file = NULL;
}

static void send_result(TonieApp* app, const TonieProtocolResult* result) {
    if(result->action != TonieActionRespondNow || result->response_len == 0) return;
    bit_buffer_copy_bytes(app->tx_buffer, result->response, result->response_len);
    iso13239_crc_append(Iso13239CrcTypeDefault, app->tx_buffer);
    const NfcError error = nfc_listener_tx(app->nfc, app->tx_buffer);
    app->last_tx_error = error;
    if(error == NfcErrorNone) ++app->tx_count;
    else ++app->errors;
}

static void log_result(TonieApp* app, const TonieProtocolResult* result) {
    if(result->action != TonieActionRespondNow) return;
    log_hex(app, app->last_tx_error == NfcErrorNone ? "TX" : "TX_FAILED",
            result->response, result->response_len);
    if(app->last_tx_error != NfcErrorNone)
        log_line(app, "TX ERROR nfc_error=%u", (unsigned)app->last_tx_error);
}

static NfcCommand nfc_callback(NfcEvent event, void* context) {
    TonieApp* app = context;
    if(event.type == NfcEventTypeFieldOn) {
        tonie_field_on(&app->session);
        log_line(app, "FIELD ON");
    } else if(event.type == NfcEventTypeFieldOff) {
        const TonieState old = app->session.state;
        tonie_field_off(&app->session);
        log_line(app, "FIELD OFF: reset %s -> READY", tonie_state_name(old));
    } else if(event.type == NfcEventTypeRxEnd) {
        BitBuffer* rx = event.data.buffer;
        const size_t raw_len = bit_buffer_get_size_bytes(rx);
        const uint8_t* raw = bit_buffer_get_data(rx);
        ++app->rx_count;
        if(raw_len == 0U) {
            TonieProtocolResult result = tonie_handle_eof(&app->session, &app->tag);
            send_result(app, &result);
            if(app->verbose)
                log_line(app, "EOF progression=%u target=%u action=%u",
                         app->session.inventory_eof_count, result.inventory_slot, result.action);
            log_result(app, &result);
        } else {
            if(!iso13239_crc_check(Iso13239CrcTypeDefault, rx)) {
                tonie_cancel_pending(&app->session);
                ++app->errors;
                log_hex(app, "RX_BAD_CRC", raw, raw_len);
            } else {
                iso13239_crc_trim(rx);
                const uint8_t* frame = bit_buffer_get_data(rx);
                const size_t frame_len = bit_buffer_get_size_bytes(rx);
                const TonieState old = app->session.state;
                TonieProtocolResult result =
                    tonie_handle_frame(&app->session, &app->tag, frame, frame_len);
                send_result(app, &result); /* Meet the RF deadline before formatting logs. */
                log_hex(app, "RX", frame, frame_len);
                log_result(app, &result);
                if(frame_len && !(frame[0] & 2U))
                    log_line(app, "LOW_RATE requested; requires patched firmware HAL");
                if(frame_len && (frame[0] & 1U))
                    log_line(app, "DUAL_SUBCARRIER requested; transport unsupported");
                log_line(app, "CMD %s(0x%02X) flags=0x%02X result=%u action=%u mask=%s slot=%u",
                         tonie_command_name(result.command), result.command,
                         frame_len ? frame[0] : 0, result.code, result.action,
                         result.mask_match ? "yes" : "no", result.inventory_slot);
                if(old != app->session.state)
                    log_line(app, "STATE %s -> %s", tonie_state_name(old),
                             tonie_state_name(app->session.state));
                if(result.code == TonieResultUnsupported) {
                    ++app->errors;
                    log_line(app, "UNKNOWN CMD 0x%02X", result.command);
                } else if(result.code == TonieResultMalformed) {
                    ++app->errors;
                    log_line(app, "MALFORMED CMD 0x%02X", result.command);
                }
            }
        }
    }
    return app->running ? NfcCommandContinue : NfcCommandStop;
}

static bool copy_device(TonieApp* app) {
    const NfcProtocol protocol = nfc_device_get_protocol(app->device);
    if(protocol != NfcProtocolIso15693_3 && protocol != NfcProtocolSlix) return false;
    const Iso15693_3Data* iso =
        (const Iso15693_3Data*)nfc_device_get_data(app->device, NfcProtocolIso15693_3);
    const uint16_t count = iso15693_3_get_block_count(iso);
    const uint8_t size = iso15693_3_get_block_size(iso);
    if(!count || count > TONIE_MAX_BLOCKS || !size || size > TONIE_MAX_BLOCK_SIZE) return false;
    memset(&app->tag, 0, sizeof(app->tag));
    memcpy(app->tag.uid, iso->uid, 8);
    app->tag.dsfid = iso->system_info.dsfid;
    app->tag.afi = iso->system_info.afi;
    app->tag.ic_ref = iso->system_info.ic_ref;
    app->tag.system_info_flags = iso->system_info.flags;
    app->tag.lock_bits = (iso->settings.lock_bits.dsfid ? 4U : 0U) |
                         (iso->settings.lock_bits.afi ? 1U : 0U);
    app->tag.block_count = count;
    app->tag.block_size = size;
    for(uint16_t i = 0; i < count; ++i) {
        memcpy(&app->tag.blocks[(size_t)i * size], iso15693_3_get_block_data(iso, (uint8_t)i), size);
        app->tag.security[i] = iso15693_3_is_block_locked(iso, (uint8_t)i) ? 1U : 0U;
    }
    if(protocol == NfcProtocolSlix) {
        const SlixData* slix = (const SlixData*)nfc_device_get_data(app->device, NfcProtocolSlix);
        app->tag.slix = true;
        memcpy(app->tag.signature, slix->signature, sizeof(app->tag.signature));
        app->tag.protection_pointer = slix->system_info.protection.pointer;
        app->tag.protection_condition = slix->system_info.protection.condition;
        app->tag.lock_bits = (iso->settings.lock_bits.dsfid ? 4U : 0U) |
                             (iso->settings.lock_bits.afi ? 1U : 0U) |
                             (slix->system_info.lock_bits.eas ? 2U : 0U) |
                             (slix->system_info.lock_bits.ppl ? 8U : 0U);
        for(size_t i = 0; i < 5; ++i) {
            app->tag.passwords[i] = slix->passwords[i];
            if(slix_type_supports_password(slix_get_type(slix), i))
                app->tag.password_mask |= 1U << i;
        }
        app->tag.accept_all_passwords = slix->capabilities == SlixCapabilitiesAcceptAllPasswords;
        app->tag.privacy_persistent = slix->privacy;
    }
    return true;
}

static bool load_file(TonieApp* app) {
    nfc_device_clear(app->device);
    if(!nfc_device_load(app->device, furi_string_get_cstr(app->file_path)) || !copy_device(app)) {
        snprintf(app->status, sizeof(app->status), "Unsupported/invalid .nfc");
        app->loaded = false;
        return false;
    }
    app->loaded = true;
    snprintf(app->status, sizeof(app->status), "Ready");
    return true;
}

static bool choose_file(TonieApp* app) {
    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, ".nfc", NULL);
    options.base_path = NFC_DIR;
    options.hide_dot_files = true;
    if(furi_string_empty(app->file_path)) furi_string_set(app->file_path, NFC_DIR);
    if(!dialog_file_browser_show(app->dialogs, app->file_path, app->file_path, &options)) return false;
    return load_file(app);
}

static uint32_t selected_fdt(const TonieApp* app) {
    if(app->timing == TimingFast) return FAST_FDT_FC;
    if(app->timing == TimingCustom) return app->custom_fdt_fc;
    return DEFAULT_FDT_FC;
}

static void native_trace(void* context, unsigned kind, const uint8_t* bytes, size_t size, unsigned result) {
    TonieApp* app=context;
    if(!app->logger_running || !app->log_queue) return;
    LogLine line={.kind=kind,.result=result,.ms=elapsed_ms(app)};
    line.len=size>sizeof(line.text) ? sizeof(line.text) : size;
    if(line.len) memcpy(line.text,bytes,line.len);
    if(size>sizeof(line.text)) ++app->dropped_logs;
    if(kind==1) ++app->rx_count;
    if(kind==2) {if(result==NfcErrorNone) ++app->tx_count;else ++app->errors;}
    if(furi_message_queue_put(app->log_queue,&line,0)!=FuriStatusOk) ++app->dropped_logs;
}

static bool emulation_start(TonieApp* app) {
    if(!app->loaded || app->running) return false;
    if(!copy_device(app)) return false; /* Restart with the original dump, not RAM mutations. */
    tonie_session_init(&app->session);
    app->session.random_seed = furi_hal_random_get();
    app->rx_count = app->tx_count = app->errors = app->dropped_logs = 0;
    app->started_tick = furi_get_tick();
    if(!logger_start(app)) {
        snprintf(app->status, sizeof(app->status), "Log open failed");
        return false;
    }
    app->running = true;
    if(app->native_mode) {
        app->native_listener = tonie_native_alloc(app->nfc, app->device, native_trace, app);
    } else {
        nfc_config(app->nfc, NfcModeListener, NfcTechIso15693);
        nfc_set_fdt_listen_fc(app->nfc, selected_fdt(app));
    }
    log_line(app, "ENGINE %s", app->native_mode ? "firmware-tonie-recovery-v1" : "enhanced");
    log_line(app, "START fdt_fc=%lu verbose=%s uid=%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
             (unsigned long)selected_fdt(app), app->verbose ? "on" : "off",
             app->tag.uid[0], app->tag.uid[1], app->tag.uid[2], app->tag.uid[3],
             app->tag.uid[4], app->tag.uid[5], app->tag.uid[6], app->tag.uid[7]);
    if(app->native_mode) tonie_native_start(app->native_listener);
    else nfc_start(app->nfc, nfc_callback, app);
    notification_message(app->notifications, &sequence_emulating);
    snprintf(app->status, sizeof(app->status), "Emulating");
    return true;
}

static void emulation_stop(TonieApp* app) {
    if(!app->running) return;
    app->running = false;
    if(app->native_listener) {
        tonie_native_stop(app->native_listener);
        tonie_native_free(app->native_listener);
        app->native_listener = NULL;
    } else nfc_stop(app->nfc);
    notification_message_block(app->notifications, &sequence_reset_rgb);
    log_line(app, "STOP rx=%lu tx=%lu errors=%lu dropped_logs=%lu",
             (unsigned long)app->rx_count, (unsigned long)app->tx_count,
             (unsigned long)app->errors, (unsigned long)app->dropped_logs);
    logger_stop(app);
    snprintf(app->status,sizeof(app->status),"%s",app->log_io_error ? "SD error; log incomplete" : app->dropped_logs ? "Stopped; log has gaps" : "Stopped; log saved");
}

static const char* base_name(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void draw(Canvas* canvas, void* context) {
    TonieApp* app = context;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 5, AlignCenter, AlignTop, "Tonie Emulator");
    canvas_draw_line(canvas, 12, 18, 116, 18);

    char title[96];
    snprintf(title, sizeof(title), "%s", base_name(furi_string_get_cstr(app->file_path)));
    char* extension = strrchr(title, '.');
    if(extension) *extension = '\0';
    bool upper = true;
    for(char* c = title; *c; ++c) {
        if(*c == '-' || *c == '_') *c = ' ';
        if(upper && *c >= 'a' && *c <= 'z') *c -= 'a' - 'A';
        upper = *c == ' ';
    }
    canvas_set_font(canvas, FontSecondary);
    FuriString* name = furi_string_alloc_set_str(title);
    elements_string_fit_width(canvas, name, 116);
    canvas_draw_str_aligned(canvas, 64, 26, AlignCenter, AlignTop, furi_string_get_cstr(name));
    furi_string_free(name);
    if(!app->running) {
        canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignTop, app->status);
    }
    elements_button_left(canvas, "Back to files");
}

static void input(InputEvent* event, void* context) {
    TonieApp* app = context;
    furi_message_queue_put(app->input_queue, event, 0);
}

int32_t tonie_emulator_app(void* context) {
    const char* launch_path = context;
    TonieApp* app = malloc(sizeof(TonieApp));
    memset(app, 0, sizeof(*app));
    atomic_init(&app->running,false);atomic_init(&app->logger_running,false);
    atomic_init(&app->log_io_error,false);atomic_init(&app->rx_count,0);
    atomic_init(&app->tx_count,0);atomic_init(&app->errors,0);atomic_init(&app->dropped_logs,0);
    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->device = nfc_device_alloc();
    app->nfc = nfc_alloc();
    app->tx_buffer = bit_buffer_alloc(TONIE_MAX_RESPONSE + 2U);
    app->file_path = furi_string_alloc_set_str(NFC_DIR);
    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->view_port = view_port_alloc();
    app->native_mode = true;
    app->verbose = false;
    app->custom_fdt_fc = DEFAULT_FDT_FC;
    snprintf(app->status, sizeof(app->status), "Select an NFC file");

    view_port_draw_callback_set(app->view_port, draw, app);
    view_port_input_callback_set(app->view_port, input, app);
    bool selected;
    if(launch_path && strncmp(launch_path, "/ext/", 5) == 0) {
        furi_string_set(app->file_path, launch_path);
        selected = load_file(app);
    } else {
        selected = choose_file(app);
    }
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
    if(selected) emulation_start(app);
    view_port_update(app->view_port);

    bool exit = !selected;
    while(!exit) {
        InputEvent event;
        if(furi_message_queue_get(app->input_queue, &event, 100) == FuriStatusOk &&
           event.type == InputTypeShort &&
           (event.key == InputKeyBack || event.key == InputKeyLeft)) {
            emulation_stop(app);
            gui_remove_view_port(app->gui, app->view_port);
            selected = choose_file(app);
            gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
            if(selected) emulation_start(app);
            else exit = true;
        }
        view_port_update(app->view_port);
    }

    emulation_stop(app);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_message_queue_free(app->input_queue);
    furi_string_free(app->file_path);
    bit_buffer_free(app->tx_buffer);
    nfc_free(app->nfc);
    nfc_device_free(app->device);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    free(app);
    return 0;
}
