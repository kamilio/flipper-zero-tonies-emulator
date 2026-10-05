#pragma once

#include "../helpers/fbp_files.h"

#include <gui/gui.h>
#include <gui/view.h>
#include <gui/canvas.h>
#include <gui/elements.h>
#include <stdatomic.h>
#include <storage/storage.h>
#include <furi.h>

#define MAX_LEN_PX   110
#define MAX_NAME_LEN 255
#define MAX_EXT_LEN  6
#define FRAME_HEIGHT 12
#define MENU_ITEMS   1u
#define VISIBLE_ROWS 4u

typedef enum {
    FbpBrowserEventFileMenuOpen,
    FbpBrowserEventFileMenuClose,
    FbpBrowserEventFileMenuRun,
    FbpBrowserEventDirectoryAction,

    FbpBrowserEventEnterDir,

    FbpBrowserEventLoadPrevItems,
    FbpBrowserEventLoadNextItems,
    FbpBrowserEventLoadFirstItems,
    FbpBrowserEventLoadLastItems,

    FbpBrowserEventSortNext,
    FbpBrowserEventFlattenToggle,
    FbpBrowserEventListRefresh,

    FbpBrowserEventExit,
} FbpBrowserEvent;



typedef enum {
    FbpSortNameAsc = 0,
    FbpSortNameDesc,
    FbpSortNewest,
    FbpSortOldest,
    FbpSortTotal,
} FbpSortMode;

typedef struct FbpBrowserView FbpBrowserView;

typedef void (*FbpBrowserViewCallback)(FbpBrowserEvent event, void* context);

struct FbpBrowserView {
    View* view;
    FbpBrowserViewCallback callback;
    void* context;
    FuriString* path;
    const char* root_path;
    const char* extension;
    const char* cache_directory;
    FuriString* cache_path;
    FuriTimer* scroll_timer;

    FuriThread* scan_thread;
    FuriString* scan_root;
    FuriString* cache_root;
    bool cache_valid;
    bool cache_recursive;
    bool cache_has_dates;
    atomic_bool scan_cancel;
    FbpSortMode scan_sort;
    bool scan_flattened;
    bool scan_backwards;
    FbpFile_t scan_anchor;
    FbpFile_t selection; // Stable snapshot; never a pointer into a background-owned array.

};

typedef struct {
    fbp_files_array_t files;

    const char* action_label;
    uint8_t menu_idx;
    bool menu;
    bool list_loading;
    bool folder_loading;
    uint32_t loading_started;
    uint16_t scroll_tick_ms;
    char error[64];

    FbpSortMode sort_mode;
    bool flattened;

    uint32_t item_cnt;
    int32_t item_idx;
    int32_t array_offset;
    int32_t list_offset;
    size_t scroll_counter;

    uint32_t button_held_for_ticks;
} FbpBrowserViewModel;

void fbp_browser_set_callback(FbpBrowserView* browser, FbpBrowserViewCallback callback, void* context);

View* fbp_browser_get_view(FbpBrowserView* browser);

FbpBrowserView* fbp_browser_alloc(void);
void fbp_browser_free(FbpBrowserView* browser);
