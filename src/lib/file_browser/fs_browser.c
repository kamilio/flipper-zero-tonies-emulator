#include "fs_browser.h"
#include "helpers/fbp_browser.h"
#include <gui/view_dispatcher.h>

typedef struct {
    ViewDispatcher* dispatcher;
    FbpBrowserView* browser;
    FuriString* selected;
    FsBrowserResult result;
} FsBrowser;

static void send_event(FbpBrowserEvent event, void* context) {
    FsBrowser* picker = context;
    view_dispatcher_send_custom_event(picker->dispatcher, event);
}

static bool browser_event(void* context, uint32_t event) {
    FsBrowser* picker = context;
    FbpBrowserView* browser = picker->browser;
    switch(event) {
    case FbpBrowserEventFileMenuOpen:
        fbp_show_file_menu(browser, true);
        break;
    case FbpBrowserEventFileMenuClose:
        fbp_show_file_menu(browser, false);
        break;
    case FbpBrowserEventDirectoryAction:
        picker->result = FsBrowserDirectoryAction;
        view_dispatcher_stop(picker->dispatcher);
        break;
    case FbpBrowserEventFileMenuRun: {
        FbpFile_t* selected = fbp_get_current_file(browser);
        if(selected && selected->type != FbpFileTypeFolder &&
           (!browser->extension || furi_string_end_withi(selected->path, browser->extension))) {
            furi_string_set(picker->selected, selected->path);
            picker->result = FsBrowserFileSelected;
            view_dispatcher_stop(picker->dispatcher);
        }
        break;
    }
    case FbpBrowserEventEnterDir: {
        FbpFile_t* selected = fbp_get_current_file(browser);
        if(selected && selected->type == FbpFileTypeFolder) {
            FuriString* path = furi_string_alloc_set(selected->path);
            fbp_enter_dir(browser, path);
            furi_string_free(path);
        }
        break;
    }
    case FbpBrowserEventLoadPrevItems: fbp_file_array_load(browser, -1); break;
    case FbpBrowserEventLoadNextItems: fbp_file_array_load(browser, 1); break;
    case FbpBrowserEventLoadFirstItems: fbp_file_array_load(browser, 2); break;
    case FbpBrowserEventLoadLastItems: fbp_file_array_load(browser, -2); break;
    case FbpBrowserEventSortNext:
        with_view_model(browser->view, FbpBrowserViewModel* model, {
            model->sort_mode = (model->sort_mode + 1) % FbpSortTotal;
        }, true);
        fbp_open_dir(browser);
        break;
    case FbpBrowserEventFlattenToggle:
        with_view_model(browser->view, FbpBrowserViewModel* model, {
            model->flattened = !model->flattened;
        }, true);
        fbp_open_dir(browser);
        break;
    case FbpBrowserEventListRefresh: fbp_refresh_dir(browser); break;
    case FbpBrowserEventExit:
        if(fbp_is_home(browser)) view_dispatcher_stop(picker->dispatcher);
        else fbp_leave_dir(browser);
        break;
    default: return false;
    }
    return true;
}

static bool browser_back(void* context) {
    return browser_event(context, FbpBrowserEventExit);
}

FsBrowserResult fs_browser_run(
    Gui* gui, const FsBrowserConfig* config, FuriString* directory, FuriString* selected_file) {
    furi_assert(gui && config && config->root_path && config->cache_directory && config->action_label);
    furi_assert(directory && selected_file && directory != selected_file);
    FsBrowser picker = {.result = FsBrowserCancelled, .selected = selected_file};
    picker.browser = fbp_browser_alloc();
    picker.dispatcher = view_dispatcher_alloc();
    FbpBrowserView* browser = picker.browser;
    browser->root_path = config->root_path;
    browser->extension = config->extension;
    browser->cache_directory = config->cache_directory;
    furi_string_printf(browser->cache_path, "%s/.browser-cache", config->cache_directory);
    const char* path = furi_string_get_cstr(directory);
    size_t root_length = strlen(config->root_path);
    bool within_root = !strncmp(path, config->root_path, root_length) &&
        (path[root_length] == '\0' || path[root_length] == '/');
    FileInfo info;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool folder = within_root && storage_common_stat(storage, path, &info) == FSE_OK && file_info_is_dir(&info);
    furi_record_close(RECORD_STORAGE);
    furi_string_set_str(browser->path, folder ? path : config->root_path);
    with_view_model(browser->view, FbpBrowserViewModel* model, {
        model->action_label = config->action_label;
    }, false);
    fbp_browser_set_callback(browser, send_event, &picker);
    view_dispatcher_set_event_callback_context(picker.dispatcher, &picker);
    view_dispatcher_set_custom_event_callback(picker.dispatcher, browser_event);
    view_dispatcher_set_navigation_event_callback(picker.dispatcher, browser_back);
    view_dispatcher_add_view(picker.dispatcher, 0, fbp_browser_get_view(browser));
    view_dispatcher_attach_to_gui(picker.dispatcher, gui, ViewDispatcherTypeFullscreen);
    fbp_open_dir(browser);
    view_dispatcher_switch_to_view(picker.dispatcher, 0);
    view_dispatcher_run(picker.dispatcher);
    fbp_stop_scan(browser);
    furi_string_set(directory, browser->path);
    view_dispatcher_remove_view(picker.dispatcher, 0);
    view_dispatcher_free(picker.dispatcher);
    fbp_browser_free(browser);
    return picker.result;
}
