#include "fbp_browser.h"
#include "fbp_cache.h"
#include "fbp_dates.h"
#include <toolbox/dir_walk.h>
#include <toolbox/path.h>
#include <strings.h>

#define SCAN_HEAP_RESERVE (16 * 1024)
#define SCAN_PATH_LIMIT 1024

static const char* basename_of(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int file_compare(const FbpFile_t* a, const FbpFile_t* b, FbpSortMode sort) {
    bool ad = a->type == FbpFileTypeFolder, bd = b->type == FbpFileTypeFolder;
    if(ad != bd) return ad ? -1 : 1;
    const char* ap = furi_string_get_cstr(a->path);
    const char* bp = furi_string_get_cstr(b->path);
    int name = strcasecmp(basename_of(ap), basename_of(bp));
    if(!name) name = strcmp(ap, bp); // Duplicate names remain distinct and deterministic.
    if(sort == FbpSortNameAsc) return name;
    if(sort == FbpSortNameDesc) return -name;
    if(!a->timestamp != !b->timestamp) return a->timestamp ? -1 : 1;
    if(a->timestamp != b->timestamp) {
        int date = a->timestamp > b->timestamp ? -1 : 1;
        return sort == FbpSortNewest ? date : -date;
    }
    return name;
}

// Keep only one sorted page. Every scan still visits the entire scope, so
// ordering and counts are global even for a library larger than device RAM.
static void page_insert(fbp_files_array_t page, const FbpFile_t* file, FbpSortMode sort, bool last) {
    size_t count = fbp_files_array_size(page);
    if(count == FILE_LIST_BUF_LEN) {
        size_t edge = last ? 0 : count - 1;
        int cmp = file_compare(file, fbp_files_array_get(page, edge), sort);
        if(last ? cmp <= 0 : cmp >= 0) return;
        fbp_files_array_remove_v(page, edge, edge + 1);
        count--;
    }
    fbp_files_array_push_back(page, *file);
    while(count > 0 && file_compare(fbp_files_array_get(page, count - 1), fbp_files_array_get(page, count), sort) > 0) {
        // Entries own their strings. Exchange ownership directly: the generic
        // M*LIB fallback deep-copies/free strings three times per comparison.
        FbpFile_t* a = fbp_files_array_get(page, count - 1);
        FbpFile_t* b = fbp_files_array_get(page, count);
        FbpFile_t temporary = *a;
        *a = *b;
        *b = temporary;
        count--;
    }
}

void fbp_stop_scan(FbpBrowserView* browser) {
    atomic_store(&browser->scan_cancel, true);
    if(browser->scan_thread) {
        furi_thread_join(browser->scan_thread);
        furi_thread_free(browser->scan_thread);
        browser->scan_thread = NULL;
    }
}

static bool cache_covers(FbpBrowserView* browser, const char* root) {
    if(!browser->cache_valid) return false;
    bool needs_dates = browser->scan_sort == FbpSortNewest || browser->scan_sort == FbpSortOldest;
    // A shallow folder view never reads a recursive snapshot of its descendants.
    return !furi_string_cmp_str(browser->cache_root, root) &&
           browser->cache_recursive == browser->scan_flattened &&
           (!needs_dates || browser->cache_has_dates);
}

static int32_t scan_thread(void* context) {
    FbpBrowserView* browser = context;
    fbp_files_array_t page;
    fbp_files_array_init(page);
    FbpFile_t item;
    FbpFile_t_init(&item);
    uint32_t total = 0, before = 0, through = 0;
    bool bounded = !furi_string_empty(browser->scan_anchor.path);
    const bool backwards = browser->scan_backwards;
    const bool needs_dates = browser->scan_sort == FbpSortNewest || browser->scan_sort == FbpSortOldest;
    char error[64] = "";
    Storage* storage = furi_record_open(RECORD_STORAGE);
    const char* root = furi_string_get_cstr(browser->scan_root);
    for(unsigned pass = 0; pass < 2; ++pass) {
        bool cached = cache_covers(browser, root);
        FbpCache* cache = cached ? fbp_cache_open(storage, furi_string_get_cstr(browser->cache_path), false) : NULL;
        if(cached && !cache) { browser->cache_valid = false; cached = false; }
        DirWalk* walk = NULL;
        FbpDates* dates = NULL;
        if(!cached) {
            browser->cache_valid = false;
            storage_simply_mkdir(storage, EXT_PATH("apps_data"));
            storage_simply_mkdir(storage, browser->cache_directory);
            cache = fbp_cache_open(storage, furi_string_get_cstr(browser->cache_path), true);
            if(needs_dates) dates = fbp_dates_device_open(&browser->scan_cancel);
            walk = dir_walk_alloc(storage);
            dir_walk_set_recursive(walk, browser->scan_flattened);
            if(!dir_walk_open(walk, root)) strlcpy(error, "Cannot open folder\nCheck SD card", sizeof(error));
        }
        bool complete = false;
        while(!error[0] && !atomic_load(&browser->scan_cancel)) {
            if(memmgr_get_free_heap() < SCAN_HEAP_RESERVE) {
                strlcpy(error, "Low memory\nClose other apps", sizeof(error));
                break;
            }
            if(cached) {
                int status = fbp_cache_read(cache, &item);
                if(status == 0) { complete = true; break; }
                if(status < 0) {
                    browser->cache_valid = false;
                    strlcpy(error, "Cache read failed\nHold Right to refresh", sizeof(error));
                    break;
                }
            } else {
                FileInfo info;
                DirWalkResult status = dir_walk_read(walk, item.path, &info);
                if(status == DirWalkLast) { complete = true; break; }
                if(status != DirWalkOK) {
                    strlcpy(error, "Folder read failed\nCheck SD card", sizeof(error));
                    break;
                }
                if(furi_string_size(item.path) >= SCAN_PATH_LIMIT) {
                    strlcpy(error, "Path too long\n1024 byte limit", sizeof(error));
                    break;
                }
                const char* relative = furi_string_get_cstr(item.path) + strlen(root) + 1;
                if(relative[0] == '.' || strstr(relative, "/.")) continue;
                fbp_set_file_type(&item, root, file_info_is_dir(&info));
                item.timestamp = needs_dates ? fbp_dates_get(dates, furi_string_get_cstr(item.path)) : 0;
                if(cache && !fbp_cache_write(cache, &item)) { fbp_cache_close(cache); cache = NULL; }
            }
            if(item.type != FbpFileTypeFolder && browser->extension &&
               !furi_string_end_withi(item.path, browser->extension)) continue;
            const char* path = furi_string_get_cstr(item.path);
            size_t length = strlen(root);
            if(strncmp(path, root, length) || path[length] != '/') continue;
            const char* relative = path + length + 1;
            if(!browser->scan_flattened && strchr(relative, '/')) continue;
            if(browser->scan_flattened && item.type == FbpFileTypeFolder) continue;
            furi_string_reset(item.rel_dir);
            const char* slash = strrchr(relative, '/');
            if(slash) furi_string_set_strn(item.rel_dir, relative, slash - relative);
            total++;
            int cmp = bounded ? file_compare(&item, &browser->scan_anchor, browser->scan_sort) : 1;
            if(bounded && cmp < 0) before++;
            if(bounded && cmp <= 0) through++;
            if(!bounded || (backwards ? cmp < 0 : cmp > 0)) page_insert(page, &item, browser->scan_sort, backwards);
        }
        if(walk) dir_walk_free(walk);
        fbp_dates_device_close(dates);
        if(cache) {
            bool ok = fbp_cache_close(cache);
            if(!cached && ok && complete && !atomic_load(&browser->scan_cancel)) {
                furi_string_set(browser->cache_root, root);
                browser->cache_recursive = browser->scan_flattened;
                browser->cache_has_dates = needs_dates;
                browser->cache_valid = true;
            }
        }
        if(!(bounded && !error[0] && total && !fbp_files_array_size(page) && !atomic_load(&browser->scan_cancel))) break;
        // The boundary can disappear between refreshes. Recover at the first/last page.
        bounded = false;
        total = before = through = 0;
    }
    furi_record_close(RECORD_STORAGE);
    FbpFile_t_clear(&item);
    if(!atomic_load(&browser->scan_cancel)) {
        with_view_model(browser->view, FbpBrowserViewModel * model, {
            uint32_t count = fbp_files_array_size(page);
            uint32_t offset = backwards ? (bounded ? before : total) - count : (bounded ? through : 0);
            if(error[0]) { fbp_files_array_reset(page); count = total = offset = 0; }
            fbp_files_array_swap(model->files, page);
            model->item_cnt = total;
            model->array_offset = offset;
            model->item_idx = count ? offset + (backwards ? count - 1 : 0) : 0;
            model->list_offset = count ? MAX((int32_t)offset, model->item_idx - 3) : 0;
            model->scroll_counter = 0;
            model->folder_loading = model->list_loading = false;
            strlcpy(model->error, error, sizeof(model->error));
        }, true);
    }
    fbp_files_array_clear(page);
    return 0;
}

static void scan_start(FbpBrowserView* browser, int direction) {
    fbp_stop_scan(browser);
    FbpFile_t_set(&browser->scan_anchor, &browser->selection);
    furi_string_reset(browser->scan_anchor.path);
    browser->scan_backwards = direction < 0;
    with_view_model(browser->view, FbpBrowserViewModel * model, {
        browser->scan_sort = model->sort_mode;
        browser->scan_flattened = model->flattened;
        size_t count = fbp_files_array_size(model->files);
        if(count && (direction == 1 || direction == -1)) {
            FbpFile_t_set(&browser->scan_anchor, fbp_files_array_get(model->files, direction < 0 ? 0 : count - 1));
        }
        // Keep the previous bounded page visible during short loads. The
        // loading flag prevents selecting it until the replacement is ready.
        model->menu = false;
        model->folder_loading = true;
        model->list_loading = direction != 0;
        model->loading_started = furi_get_tick();
        model->error[0] = '\0';
    }, true);
    if(memmgr_get_free_heap() < SCAN_HEAP_RESERVE) {
        with_view_model(browser->view, FbpBrowserViewModel * model, {
            model->folder_loading = false;
            fbp_files_array_reset(model->files);
            model->item_cnt = 0;
            model->item_idx = model->array_offset = model->list_offset = 0;
            strlcpy(model->error, "Low memory\nClose other apps", sizeof(model->error));
        }, true);
        return;
    }
    furi_string_set(browser->scan_root, browser->path);
    atomic_store(&browser->scan_cancel, false);
    browser->scan_thread = furi_thread_alloc_ex("FbpScan", 4096, scan_thread, browser);
    furi_thread_start(browser->scan_thread);
}

void fbp_open_dir(FbpBrowserView* browser) { scan_start(browser, 0); }
void fbp_refresh_dir(FbpBrowserView* browser) { fbp_stop_scan(browser); browser->cache_valid = false; scan_start(browser, 0); }
void fbp_file_array_load(FbpBrowserView* browser, int8_t dir) { scan_start(browser, dir); }

bool fbp_is_item_in_array(FbpBrowserViewModel* model, uint32_t idx) {
    return !model->folder_loading && !model->error[0] && idx < model->item_cnt && idx >= (uint32_t)model->array_offset &&
           idx - model->array_offset < fbp_files_array_size(model->files);
}

void fbp_update_offset(FbpBrowserView* browser) {
    with_view_model(browser->view, FbpBrowserViewModel * model, {
        if(model->item_cnt == 0) model->item_idx = model->list_offset = 0;
        else {
            if(model->item_idx < model->list_offset) model->list_offset = model->item_idx;
            if(model->item_idx > model->list_offset + 3) model->list_offset = model->item_idx - 3;
            model->list_offset = MAX(model->array_offset, model->list_offset);
        }
    }, true);
}

FbpFile_t* fbp_get_current_file(FbpBrowserView* browser) {
    bool found = false;
    with_view_model(browser->view, FbpBrowserViewModel * model, {
        if(fbp_is_item_in_array(model, model->item_idx)) {
            FbpFile_t_set(&browser->selection, fbp_files_array_get(model->files, model->item_idx - model->array_offset));
            found = true;
        }
    }, false);
    return found ? &browser->selection : NULL;
}

bool fbp_get_selected_path(FbpBrowserView* browser, FuriString* out) {
    FbpFile_t* selected = fbp_get_current_file(browser);
    if(!selected) return false;
    furi_string_set(out, selected->path);
    return true;
}

bool fbp_is_home(FbpBrowserView* browser) {
    return furi_string_cmp_str(browser->path, browser->root_path) == 0;
}

void fbp_show_file_menu(FbpBrowserView* browser, bool show) {
    with_view_model(browser->view, FbpBrowserViewModel * model, {
        model->menu = show && !model->folder_loading;
        model->menu_idx = 0;
    }, true);
}

static void regular_view(FbpBrowserView* browser) {
    fbp_stop_scan(browser);
    with_view_model(browser->view, FbpBrowserViewModel * model, {
        model->flattened = false;
        model->sort_mode = FbpSortNameAsc;
    }, false);
}

void fbp_enter_dir(FbpBrowserView* browser, FuriString* path) {
    regular_view(browser);
    furi_string_set(browser->path, path);
    fbp_open_dir(browser);
}

void fbp_leave_dir(FbpBrowserView* browser) {
    if(fbp_is_home(browser)) return;
    regular_view(browser);
    size_t slash = furi_string_search_rchar(browser->path, '/');
    furi_string_left(browser->path, slash);
    if(furi_string_empty(browser->path)) furi_string_set(browser->path, STORAGE_EXT_PATH_PREFIX);
    fbp_open_dir(browser);
}
