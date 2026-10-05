#pragma once
#include <furi.h>
#include <gui/gui.h>

typedef enum {
    FsBrowserCancelled,
    FsBrowserFileSelected,
    FsBrowserDirectoryAction,
} FsBrowserResult;

typedef struct {
    const char* root_path;
    const char* extension; // NULL allows all files; matching is case-insensitive.
    const char* action_label; // The sole long-OK directory menu item.
    const char* cache_directory;
} FsBrowserConfig;

// Synchronous modal browser. Borrowed config strings must outlive this call.
// directory is updated on every result, including Cancel and DirectoryAction.
// selected_file changes only on FileSelected. All views, workers and page memory
// are released before returning, so the caller can safely start NFC or another UI.
FsBrowserResult fs_browser_run(
    Gui* gui, const FsBrowserConfig* config, FuriString* directory, FuriString* selected_file);
