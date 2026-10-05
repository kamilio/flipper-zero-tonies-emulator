#pragma once
#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#define UNUSED(x) (void)(x)
#define COUNT_OF(x) (sizeof(x) / sizeof((x)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(x, hi, lo) MIN(MAX(x, lo), hi)
#define furi_assert(x) assert(x)
#define furi_check(x) assert(x)
#define EXT_PATH(x) "/ext/" x
#define STORAGE_EXT_PATH_PREFIX "/ext"
#define RECORD_STORAGE "storage"
#define RECORD_DIALOGS "dialogs"
#define RECORD_LOADER "loader"
#define RECORD_GUI "gui"
#define FURI_LOG_E(...) ((void)0)
#define FURI_LOG_W(...) ((void)0)
void *test_malloc(size_t);
void *test_calloc(size_t, size_t);
void *test_realloc(void *, size_t);
void test_free(void *);
#define malloc test_malloc
#define calloc test_calloc
#define realloc test_realloc
#define free test_free
size_t memmgr_get_free_heap(void);
void *furi_record_open(const char *);
void furi_record_close(const char *);
typedef struct {
    char *data;
} FuriString;
FuriString *furi_string_alloc(void);
FuriString *furi_string_alloc_printf(const char *, ...);
void furi_string_free(FuriString *);
void furi_string_set_str(FuriString *, const char *);
void furi_string_set_f(FuriString *, const FuriString *);
#define furi_string_set(d, s)                                                                      \
    _Generic((s),                                                                                  \
        FuriString *: furi_string_set_f,                                                           \
        const FuriString *: furi_string_set_f,                                                     \
        default: furi_string_set_str)(d, s)
FuriString *furi_string_alloc_set_str(const char *);
FuriString *furi_string_alloc_set_f(const FuriString *);
#define furi_string_alloc_set(s)                                                                   \
    _Generic((s),                                                                                  \
        FuriString *: furi_string_alloc_set_f,                                                     \
        const FuriString *: furi_string_alloc_set_f,                                               \
        default: furi_string_alloc_set_str)(s)
const char *furi_string_get_cstr(const FuriString *);
size_t furi_string_size(const FuriString *);
bool furi_string_empty(const FuriString *);
void furi_string_reset(FuriString *);
void furi_string_set_strn(FuriString *, const char *, size_t);
void furi_string_cat_str(FuriString *, const char *);
void furi_string_cat(FuriString *, const FuriString *);
void furi_string_cat_printf(FuriString *, const char *, ...);
void furi_string_printf(FuriString *, const char *, ...);
void furi_string_swap(FuriString *, FuriString *);
void furi_string_left(FuriString *, size_t);
size_t furi_string_search_rchar(const FuriString *, char);
size_t furi_string_search(const FuriString *, const char *);
int furi_string_cmp_str(const FuriString *, const char *);
bool furi_string_end_withi(const FuriString *, const char *);
typedef struct {
    pthread_t thread;
    int32_t (*fn)(void *);
    void *ctx;
    bool joined;
} FuriThread;
FuriThread *furi_thread_alloc_ex(const char *, size_t, int32_t (*)(void *), void *);
void furi_thread_start(FuriThread *);
void furi_thread_join(FuriThread *);
void furi_thread_free(FuriThread *);
typedef struct {
    int dummy;
} FuriTimer;
enum { FuriTimerTypePeriodic };
FuriTimer *furi_timer_alloc(void (*)(void *), int, void *);
void furi_timer_free(FuriTimer *);
void furi_timer_start(FuriTimer *, int);
void furi_timer_stop(FuriTimer *);
typedef enum {
    InputTypePress,
    InputTypeRelease,
    InputTypeShort,
    InputTypeLong,
    InputTypeRepeat
} InputType;
typedef enum {
    InputKeyUp,
    InputKeyDown,
    InputKeyLeft,
    InputKeyRight,
    InputKeyOk,
    InputKeyBack
} InputKey;
typedef struct {
    InputType type;
    InputKey key;
} InputEvent;
typedef struct {
    int unused;
} Canvas, Icon, Gui, Loader, DialogsApp;
typedef enum { AlignLeft, AlignRight, AlignTop, AlignBottom, AlignCenter } Align;
enum { ColorWhite, ColorBlack, ViewModelTypeLocking, ViewDispatcherTypeFullscreen };
typedef struct {
    void *model;
    void *ctx;
    pthread_mutex_t lock;
    bool (*input)(InputEvent *, void *);
    void (*draw)(Canvas *, void *);
} View;
View *view_alloc(void);
void view_free(View *);
void view_allocate_model(View *, int, size_t);
void view_set_context(View *, void *);
void view_set_draw_callback(View *, void (*)(Canvas *, void *));
void view_set_input_callback(View *, bool (*)(InputEvent *, void *));
void view_set_enter_callback(View *, void (*)(void *));
void view_set_exit_callback(View *, void (*)(void *));
#define with_view_model(v, decl, code, redraw)                                                     \
    do {                                                                                           \
        pthread_mutex_lock(&(v)->lock);                                                            \
        decl = (v)->model;                                                                         \
        code;                                                                                      \
        pthread_mutex_unlock(&(v)->lock);                                                          \
    } while(0)
void canvas_set_color(Canvas *, int);
void canvas_draw_box(Canvas *, int, int, int, int);
void canvas_draw_rframe(Canvas *, int, int, int, int, int);
void canvas_draw_line(Canvas *, int, int, int, int);
void canvas_draw_dot(Canvas *, int, int);
void canvas_draw_str(Canvas *, int, int, const char *);
void canvas_draw_str_aligned(Canvas *, int, int, Align, Align, const char *);
void canvas_draw_icon(Canvas *, int, int, const Icon *);
void elements_slightly_rounded_frame(Canvas *, int, int, int, int);
void elements_scrollable_text_line(Canvas *, int, int, int, FuriString *, size_t, bool);
void elements_scrollbar_pos(Canvas *, int, int, int, int, int);
void elements_multiline_text_aligned(Canvas *, int, int, Align, Align, const char *);
typedef enum { SceneManagerEventTypeCustom, SceneManagerEventTypeBack } SceneManagerEventType;
typedef struct {
    SceneManagerEventType type;
    uint32_t event;
} SceneManagerEvent;
typedef struct {
    void (*const *on_enter_handlers)(void *);
    bool (*const *on_event_handlers)(void *, SceneManagerEvent);
    void (*const *on_exit_handlers)(void *);
    size_t scene_num;
} SceneManagerHandlers;
typedef struct {
    const SceneManagerHandlers *handlers;
    void *ctx;
    int stack[16];
    int depth;
} SceneManager;
SceneManager *scene_manager_alloc(const SceneManagerHandlers *, void *);
void scene_manager_free(SceneManager *);
void scene_manager_next_scene(SceneManager *, int);
bool scene_manager_previous_scene(SceneManager *);
void scene_manager_set_scene_state(SceneManager *, int, int);
bool scene_manager_handle_custom_event(SceneManager *, uint32_t);
bool scene_manager_handle_back_event(SceneManager *);
typedef struct {
    void *ctx;
    uint32_t events[4096];
    size_t count;
    int view;
    bool stopped;
} ViewDispatcher;
ViewDispatcher *view_dispatcher_alloc(void);
void view_dispatcher_free(ViewDispatcher *);
void view_dispatcher_attach_to_gui(ViewDispatcher *, Gui *, int);
void view_dispatcher_set_event_callback_context(ViewDispatcher *, void *);
void view_dispatcher_set_custom_event_callback(ViewDispatcher *, bool (*)(void *, uint32_t));
void view_dispatcher_set_navigation_event_callback(ViewDispatcher *, bool (*)(void *));
void view_dispatcher_add_view(ViewDispatcher *, int, View *);
void view_dispatcher_remove_view(ViewDispatcher *, int);
void view_dispatcher_send_custom_event(ViewDispatcher *, uint32_t);
void view_dispatcher_switch_to_view(ViewDispatcher *, int);
void view_dispatcher_stop(ViewDispatcher *);
void view_dispatcher_run(ViewDispatcher *);
typedef struct {
    View *view;
    void *validator;
    char *buffer;
    size_t limit;
    void (*callback)(void *);
    void *ctx;
} TextInput;
typedef struct {
    View *view;
} Widget;
typedef enum { GuiButtonTypeLeft, GuiButtonTypeRight } GuiButtonType;
TextInput *text_input_alloc(void);
void text_input_free(TextInput *);
View *text_input_get_view(TextInput *);
void text_input_set_header_text(TextInput *, const char *);
void text_input_set_result_callback(TextInput *, void (*)(void *), void *, char *, size_t, bool);
void text_input_set_validator(TextInput *, bool (*)(const char *, FuriString *, void *), void *);
void *text_input_get_validator_callback_context(TextInput *);
void text_input_reset(TextInput *);
typedef struct {
    char *parent;
    char *current;
} ValidatorIsFile;
ValidatorIsFile *validator_is_file_alloc_init(const char *, const char *, const char *);
void validator_is_file_free(ValidatorIsFile *);
bool validator_is_file_callback(const char *, FuriString *, void *);
Widget *widget_alloc(void);
void widget_free(Widget *);
View *widget_get_view(Widget *);
void widget_reset(Widget *);
void widget_add_button_element(Widget *, GuiButtonType, const char *,
                               void (*)(GuiButtonType, InputType, void *), void *);
void widget_add_text_box_element(Widget *, int, int, int, int, Align, Align, const char *, bool);
enum { LoaderDeferredLaunchFlagGui = 2 };
void loader_enqueue_launch(Loader *, const char *, const char *, int);
void loader_start_with_gui_error(Loader *, const char *, const char *);
void dialog_message_show_storage_error(DialogsApp *, const char *);
typedef struct {
    int unused;
} Storage;
typedef enum { FSE_OK, FSE_NOT_EXIST, FSE_DENIED, FSE_INTERNAL } FS_Error;
typedef struct {
    bool dir;
} FileInfo;
static inline bool file_info_is_dir(const FileInfo *f) { return f->dir; }
FS_Error storage_common_stat(Storage *, const char *, FileInfo *);
FS_Error storage_common_timestamp(Storage *, const char *, uint32_t *);
FS_Error storage_common_rename(Storage *, const char *, const char *);
FS_Error storage_common_remove(Storage *, const char *);
bool storage_simply_remove_recursive(Storage *, const char *);
typedef enum { DirWalkOK, DirWalkError, DirWalkLast } DirWalkResult;
typedef struct WalkDir WalkDir;
typedef struct {
    WalkDir *head;
    bool recursive;
    bool (*filter)(const char *, FileInfo *, void *);
    void *ctx;
    bool descend;
    FuriString *pending;
} DirWalk;
DirWalk *dir_walk_alloc(Storage *);
void dir_walk_free(DirWalk *);
void dir_walk_set_recursive(DirWalk *, bool);
void dir_walk_set_filter_cb(DirWalk *, bool (*)(const char *, FileInfo *, void *), void *);
bool dir_walk_open(DirWalk *, const char *);
void dir_walk_close(DirWalk *);
DirWalkResult dir_walk_read(DirWalk *, FuriString *, FileInfo *);
void path_extract_filename(FuriString *, FuriString *, bool);
void path_extract_dirname(const char *, FuriString *);
void path_extract_extension(FuriString *, char *, size_t);
typedef struct {
    FILE *handle;
    FS_Error error;
} File;
enum { FSAM_READ, FSAM_WRITE, FSOM_OPEN_EXISTING, FSOM_CREATE_ALWAYS };
File *storage_file_alloc(Storage *);
void storage_file_free(File *);
bool storage_file_open(File *, const char *, int, int);
void storage_file_close(File *);
size_t storage_file_read(File *, void *, size_t);
size_t storage_file_write(File *, const void *, size_t);
FS_Error storage_file_get_error(File *);
bool storage_simply_mkdir(Storage *, const char *);

uint32_t furi_get_tick(void);
uint32_t furi_ms_to_ticks(uint32_t ms);
