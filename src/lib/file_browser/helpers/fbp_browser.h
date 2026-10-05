#pragma once

#include "../views/fbp_browser_view.h"
#include <storage/storage.h>

#define FILE_LIST_BUF_LEN 50


static const char* const known_ext[] = {
    [FbpFileTypeIButton] = ".ibtn",
    [FbpFileTypeNFC] = ".nfc",
    [FbpFileTypeSubGhz] = ".sub",
    [FbpFileTypeLFRFID] = ".rfid",
    [FbpFileTypeInfrared] = ".ir",
    [FbpFileTypeBadUsb] = ".txt",
    [FbpFileTypeU2f] = "?",
    [FbpFileTypeApplication] = ".fap",
    [FbpFileTypeJS] = ".js",
    [FbpFileTypeUpdateManifest] = ".fuf",
    [FbpFileTypeFolder] = "?",
    [FbpFileTypeUnknown] = "*",
};

static inline bool fbp_is_known_app(FbpFileTypeEnum type) {
    return type != FbpFileTypeFolder && type != FbpFileTypeUnknown && type != FbpFileTypeLoading;
}

static inline const char* fbp_sort_mode_name(FbpSortMode mode) {
    switch(mode) {
    case FbpSortNameAsc:
        return "A-Z";
    case FbpSortNameDesc:
        return "Z-A";
    case FbpSortNewest:
        return "New";
    case FbpSortOldest:
        return "Old";
    default:
        return "?";
    }
}

bool fbp_is_item_in_array(FbpBrowserViewModel* model, uint32_t idx);
void fbp_update_offset(FbpBrowserView* browser);
void fbp_file_array_load(FbpBrowserView* browser, int8_t dir);
FbpFile_t* fbp_get_current_file(FbpBrowserView* browser);
bool fbp_is_home(FbpBrowserView* browser);
bool fbp_get_selected_path(FbpBrowserView* browser, FuriString* out);
void fbp_show_file_menu(FbpBrowserView* browser, bool show);
void fbp_open_dir(FbpBrowserView* browser);
void fbp_enter_dir(FbpBrowserView* browser, FuriString* path);
void fbp_leave_dir(FbpBrowserView* browser);
void fbp_refresh_dir(FbpBrowserView* browser);
void fbp_stop_scan(FbpBrowserView* browser);
