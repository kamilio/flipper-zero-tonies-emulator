# Modal file browser

Adapted from the local FileMan+ browser in flipper-fileman. Self-contained package used by Tonie Emulator; no sync behavior.

Call `fs_browser_run(gui, config, directory, selected_file)` on the application thread with distinct allocated strings. Config strings remain borrowed until return. The result is Cancelled, FileSelected or DirectoryAction. Directory is always updated; selected_file changes only on selection. The directory action applies to the displayed scope, even for nested flat results. The caller owns read/unlock and saving.

The modal call joins the scanner and releases views/pages before returning. Use one instance at a time. Cache directory must be writable and unique to the caller; `.browser-cache` is disposable. Root must be an absolute directory without a trailing slash. Extension is optional and case insensitive.

Build all package, helpers and views C files, and use icons as the application's fap_icon_assets. The view currently includes the generated `tonie_emulator_icons.h`; adjust that include when embedding in another FAP. FAT date access uses Momentum SD HAL; A–Z views do not read dates.

Regular view defaults to A–Z. Left changes flattening within this scope; Right changes ordering. Folder navigation resets both. Pages hold 50 entries, plus the pending page during loading. Spinner begins at 700 ms. Long OK opens the single configured directory action, including empty folders.

Run `make check-browser` for production-C pagination, filtering, menu, lifecycle, error and FAT parser tests under ASan/UBSan. Requires clang and the mlib headers from a Flipper SDK (`FLIPPER_SDK_HEADERS` can override the ufbt default).
