#include <gui/icon.h>
#include "tonie_emulator_icons.h"
#include "toolbox/path.h"
#include <furi.h>
#include <furi/core/log.h>
#include "fbp_browser_view.h"
#include "../helpers/fbp_browser.h"

#define SCROLL_INTERVAL (333)
#define SCROLL_DELAY    (2)

static const Icon* FbpItemIcons[] = {
    [FbpFileTypeIButton] = &I_ibutt_10px,
    [FbpFileTypeNFC] = &I_Nfc_10px,
    [FbpFileTypeSubGhz] = &I_sub1_10px,
    [FbpFileTypeLFRFID] = &I_125_10px,
    [FbpFileTypeInfrared] = &I_ir_10px,
    [FbpFileTypeBadUsb] = &I_badusb_10px,
    [FbpFileTypeU2f] = &I_u2f_10px,
    [FbpFileTypeUpdateManifest] = &I_update_10px,
    [FbpFileTypeFolder] = &I_dir_10px,
    [FbpFileTypeUnknown] = &I_unknown_10px,
    [FbpFileTypeLoading] = &I_loading_10px,
    [FbpFileTypeApplication] = &I_unknown_10px,
    [FbpFileTypeJS] = &I_js_script_10px,
};

void fbp_browser_set_callback(
    FbpBrowserView* browser,
    FbpBrowserViewCallback callback,
    void* context) {
    furi_assert(browser);
    furi_assert(callback);
    browser->callback = callback;
    browser->context = context;
}

static void render_item_menu(Canvas* canvas, FbpBrowserViewModel* model) {
    // A directory action, independent of the selected entry (including Empty).
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 36, 17, 91, 17);
    canvas_set_color(canvas, ColorBlack);
    elements_slightly_rounded_frame(canvas, 35, 16, 93, 19);
    canvas_draw_icon(canvas, 39, 22, &I_ButtonRight_4x7);
    canvas_draw_str(canvas, 47, 29, model->action_label);

}

static void fbp_draw_frame(Canvas* canvas, uint16_t idx, bool scrollbar) {
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 15 + idx * FRAME_HEIGHT, (scrollbar ? 122 : 127), FRAME_HEIGHT);

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_dot(canvas, 0, 15 + idx * FRAME_HEIGHT);
    canvas_draw_dot(canvas, 1, 15 + idx * FRAME_HEIGHT);
    canvas_draw_dot(canvas, 0, (15 + idx * FRAME_HEIGHT) + 1);

    canvas_draw_dot(canvas, 0, (15 + idx * FRAME_HEIGHT) + 11);
    canvas_draw_dot(canvas, scrollbar ? 121 : 126, 15 + idx * FRAME_HEIGHT);
    canvas_draw_dot(canvas, scrollbar ? 121 : 126, (15 + idx * FRAME_HEIGHT) + 11);
}

static void fbp_draw_loading(Canvas* canvas, FbpBrowserViewModel* model) {
    furi_assert(model);
    static const int8_t inner[8][2] = {{0,-4},{3,-3},{4,0},{3,3},{0,4},{-3,3},{-4,0},{-3,-3}};
    static const int8_t outer[8][2] = {{0,-9},{6,-6},{9,0},{6,6},{0,9},{-6,6},{-9,0},{-6,-6}};
    uint32_t elapsed = furi_get_tick() - model->loading_started;
    unsigned phase = elapsed / furi_ms_to_ticks(100) % 8;
    for(unsigned n = 0; n < 3; n++) {
        unsigned i = (phase + n) % 8;
        canvas_draw_line(canvas, 64 + inner[i][0], 29 + inner[i][1], 64 + outer[i][0], 29 + outer[i][1]);
    }
    const char* text = model->list_loading ? "Loading page..." :
        model->flattened ? "Reading subfolders..." : "Reading folder...";
    canvas_draw_str_aligned(canvas, 64, 51, AlignCenter, AlignBottom, text);
}

static void draw_list(Canvas* canvas, FbpBrowserViewModel* model) {
    furi_assert(model);

    size_t array_size = fbp_files_array_size(model->files);
    bool scrollbar = model->item_cnt > 4;

    for(uint32_t i = 0; i < MIN(model->item_cnt - model->list_offset, VISIBLE_ROWS); ++i) {
        FuriString* str_buf;
        str_buf = furi_string_alloc();
        int32_t idx = CLAMP((uint32_t)(i + model->list_offset), model->item_cnt, 0u);

        FbpFileTypeEnum file_type = FbpFileTypeLoading;

        if(idx >= model->array_offset && idx < (int32_t)model->item_cnt &&
           (size_t)(idx - model->array_offset) < array_size) {
            FbpFile_t* file = fbp_files_array_get(
                model->files, CLAMP(idx - model->array_offset, (int32_t)(array_size - 1), 0));
            file_type = file->type;
            path_extract_filename(file->path, str_buf, false);
            // Show the filename first; the parent suffix disambiguates duplicates.
            if(model->flattened && furi_string_size(file->rel_dir) > 0) {
                furi_string_cat_str(str_buf, " - ");
                furi_string_cat(str_buf, file->rel_dir);
            }
        } else {
            furi_string_set(str_buf, "---");
        }

        size_t scroll_counter = model->scroll_counter;

        if(model->item_idx == idx) {
            fbp_draw_frame(canvas, i, scrollbar);
            if(scroll_counter < SCROLL_DELAY) {
                scroll_counter = 0;
            } else {
                scroll_counter -= SCROLL_DELAY;
            }
        } else {
            canvas_set_color(canvas, ColorBlack);
            scroll_counter = 0;
        }

        canvas_draw_icon(canvas, 2, 16 + i * FRAME_HEIGHT, FbpItemIcons[file_type]);

        elements_scrollable_text_line(
            canvas,
            15,
            24 + i * FRAME_HEIGHT,
            (scrollbar ? MAX_LEN_PX - 6 : MAX_LEN_PX),
            str_buf,
            scroll_counter,
            (model->item_idx != idx));

        furi_string_free(str_buf);
    }

    if(scrollbar) {
        elements_scrollbar_pos(canvas, 126, 15, 49, model->item_idx, model->item_cnt);
    }

    if(model->menu) {
        render_item_menu(canvas, model);
    }
}

static void fbp_render_status_bar(Canvas* canvas, FbpBrowserViewModel* model) {
    furi_assert(model);

    canvas_draw_icon(canvas, 0, 0, &I_Background_128x11);

    // Left pill: app name (like the native tab name)
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 0, 0, 50, 13);

    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, 0, 0, 51, 13, 1);
    canvas_draw_line(canvas, 49, 1, 49, 11);
    canvas_draw_line(canvas, 1, 11, 49, 11);
    canvas_draw_str_aligned(canvas, 25, 9, AlignCenter, AlignBottom, "Browser");

    // Right pill: sort mode + flatten indicator; flattened mode shows scope
    char mode_str[24];
    const char* sort_name = fbp_sort_mode_name(model->sort_mode);
    if(model->flattened) {
        snprintf(mode_str, sizeof(mode_str), "%s ALL", sort_name);
    } else {
        snprintf(mode_str, sizeof(mode_str), "%s", sort_name);
    }

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 70, 0, 57, 13);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, 70, 0, 58, 13, 1);
    canvas_draw_line(canvas, 126, 1, 126, 11);
    canvas_draw_line(canvas, 71, 11, 126, 11);
    canvas_draw_str_aligned(canvas, 99, 9, AlignCenter, AlignBottom, mode_str);

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_dot(canvas, 50, 0);
    canvas_draw_dot(canvas, 127, 0);

    canvas_set_color(canvas, ColorBlack);
}

static void fbp_view_render(Canvas* canvas, void* mdl) {
    FbpBrowserViewModel* model = mdl;

    fbp_render_status_bar(canvas, mdl);

    if(model->folder_loading) {
        if((uint32_t)(furi_get_tick() - model->loading_started) >= furi_ms_to_ticks(700)) {
            fbp_draw_loading(canvas, model);
        } else if(model->item_cnt) {
            draw_list(canvas, model);
        }
    } else if(model->error[0]) {
        elements_multiline_text_aligned(canvas, 64, 28, AlignCenter, AlignTop, model->error);
    } else if(model->item_cnt > 0) {
        draw_list(canvas, model);
    } else {
        canvas_draw_str_aligned(
            canvas, 128 / 2, 40, AlignCenter, AlignCenter, "Empty");
        if(model->menu) render_item_menu(canvas, model);
    }
}

View* fbp_browser_get_view(FbpBrowserView* browser) {
    furi_assert(browser);
    return browser->view;
}

static bool fbp_view_input(InputEvent* event, void* context) {
    furi_assert(event);
    furi_assert(context);

    FbpBrowserView* browser = context;

    bool in_menu;
    with_view_model(
        browser->view, FbpBrowserViewModel * model, { in_menu = model->menu; }, false);

    if(in_menu) {
        if(event->type == InputTypeShort) {
            if(event->key == InputKeyUp || event->key == InputKeyDown) {
                with_view_model(
                    browser->view,
                    FbpBrowserViewModel * model,
                    {
                        if(event->key == InputKeyUp) {
                            model->menu_idx = ((model->menu_idx - 1) + MENU_ITEMS) % MENU_ITEMS;
                        } else if(event->key == InputKeyDown) {
                            model->menu_idx = (model->menu_idx + 1) % MENU_ITEMS;
                        }
                    },
                    true);
            }

            if(event->key == InputKeyOk) {
                browser->callback(FbpBrowserEventDirectoryAction, browser->context);
            } else if(event->key == InputKeyBack) {
                browser->callback(FbpBrowserEventFileMenuClose, browser->context);
            }
        }

    } else {
        if(event->type == InputTypeLong && event->key == InputKeyRight) {
            browser->callback(FbpBrowserEventListRefresh, browser->context);
        }
        if(event->type == InputTypeShort) {
            if(event->key == InputKeyRight) {
                browser->callback(FbpBrowserEventSortNext, browser->context);
            } else if(event->key == InputKeyLeft) {
                browser->callback(FbpBrowserEventFlattenToggle, browser->context);
            } else if(event->key == InputKeyBack) {
                browser->callback(FbpBrowserEventExit, browser->context);
            }
        }

        if((event->key == InputKeyUp || event->key == InputKeyDown) &&
           (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
            int page = 0;
            with_view_model(browser->view, FbpBrowserViewModel * model, {
                if(!model->folder_loading && model->item_cnt) {
                    int32_t next = model->item_idx + (event->key == InputKeyUp ? -1 : 1);
                    if(next < 0) page = -2;
                    else if(next >= (int32_t)model->item_cnt) page = 2;
                    else if(next < model->array_offset) page = -1;
                    else if(next >= model->array_offset + (int32_t)fbp_files_array_size(model->files)) page = 1;
                    else model->item_idx = next;
                    model->scroll_counter = 0;
                }
            }, true);
            if(page) browser->callback(
                page == -2 ? FbpBrowserEventLoadLastItems :
                page == 2 ? FbpBrowserEventLoadFirstItems :
                page < 0 ? FbpBrowserEventLoadPrevItems : FbpBrowserEventLoadNextItems,
                browser->context);
            fbp_update_offset(browser);
        }

        if(event->key == InputKeyOk) {
            bool valid = false, folder = false;
            with_view_model(browser->view, FbpBrowserViewModel * model, {
                valid = fbp_is_item_in_array(model, model->item_idx);
                if(valid) folder = fbp_files_array_get(model->files, model->item_idx - model->array_offset)->type == FbpFileTypeFolder;
            }, false);
            if(event->type == InputTypeLong) {
                browser->callback(FbpBrowserEventFileMenuOpen, browser->context);
            } else if(valid) {
                if(event->type == InputTypeShort) {
                    if(folder) {
                        browser->callback(FbpBrowserEventEnterDir, browser->context);
                    } else {
                        browser->callback(FbpBrowserEventFileMenuRun, browser->context);
                    }
                }
            }
        }
    }

    if(event->type == InputTypeRelease) {
        with_view_model(
            browser->view,
            FbpBrowserViewModel * model,
            { model->button_held_for_ticks = 0; },
            true);
    }

    return true;
}

static void browser_scroll_timer(void* context) {
    furi_assert(context);
    FbpBrowserView* browser = context;
    with_view_model(
        browser->view, FbpBrowserViewModel * model, {
            model->scroll_tick_ms += 100;
            if(model->scroll_tick_ms >= SCROLL_INTERVAL) {
                model->scroll_tick_ms -= SCROLL_INTERVAL;
                model->scroll_counter++;
            }
        }, true);
}

static void browser_view_enter(void* context) {
    furi_assert(context);
    FbpBrowserView* browser = context;
    with_view_model(
        browser->view, FbpBrowserViewModel * model, { model->scroll_counter = 0; }, true);
    furi_timer_start(browser->scroll_timer, furi_ms_to_ticks(100));
}

static void browser_view_exit(void* context) {
    furi_assert(context);
    FbpBrowserView* browser = context;
    furi_timer_stop(browser->scroll_timer);
}

FbpBrowserView* fbp_browser_alloc(void) {
    FbpBrowserView* browser = calloc(1, sizeof(FbpBrowserView));
    browser->view = view_alloc();
    view_allocate_model(browser->view, ViewModelTypeLocking, sizeof(FbpBrowserViewModel));
    view_set_context(browser->view, browser);
    view_set_draw_callback(browser->view, fbp_view_render);
    view_set_input_callback(browser->view, fbp_view_input);
    view_set_enter_callback(browser->view, browser_view_enter);
    view_set_exit_callback(browser->view, browser_view_exit);

    browser->scroll_timer = furi_timer_alloc(browser_scroll_timer, FuriTimerTypePeriodic, browser);

    browser->path = furi_string_alloc_set(EXT_PATH("nfc"));
    browser->root_path = STORAGE_EXT_PATH_PREFIX;
    browser->extension = NULL;
    browser->cache_directory = EXT_PATH("apps_data/fs_browser");
    browser->cache_path = furi_string_alloc_printf("%s/.browser-cache", browser->cache_directory);
    browser->scan_root = furi_string_alloc();
    browser->cache_root = furi_string_alloc();
    browser->scan_thread = NULL;
    atomic_init(&browser->scan_cancel, false);
    FbpFile_t_init(&browser->scan_anchor);
    FbpFile_t_init(&browser->selection);

    with_view_model(
        browser->view,
        FbpBrowserViewModel * model,
        {
            fbp_files_array_init(model->files);
            model->action_label = "Action";
            model->sort_mode = FbpSortNameAsc;
            model->flattened = false;
        },
        true);

    return browser;
}

void fbp_browser_free(FbpBrowserView* browser) {
    furi_assert(browser);

    furi_timer_free(browser->scroll_timer);

    fbp_stop_scan(browser);

    with_view_model(
        browser->view, FbpBrowserViewModel * model, { fbp_files_array_clear(model->files); },
        false);

    FbpFile_t_clear(&browser->scan_anchor);
    FbpFile_t_clear(&browser->selection);
    furi_string_free(browser->scan_root);
    furi_string_free(browser->cache_root);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_remove(storage, furi_string_get_cstr(browser->cache_path));
    furi_string_free(browser->cache_path);
    furi_record_close(RECORD_STORAGE);
    furi_string_free(browser->path);

    view_free(browser->view);
    free(browser);
}
