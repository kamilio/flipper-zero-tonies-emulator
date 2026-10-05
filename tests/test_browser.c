#include "../src/lib/file_browser/fs_browser.c"
#include "../src/lib/file_browser/helpers/fbp_dates.h"
#include <furi.h>
#include <time.h>
#undef malloc
#undef calloc
#undef realloc
#undef free
static _Atomic size_t allocated, peak;
static _Atomic size_t heap_limit = 128 * 1024;
typedef union {
  max_align_t align;
  size_t size;
} Header;
void *test_malloc(size_t size) {
  Header *h = malloc(sizeof(Header) + size);
  assert(h);
  h->size = size;
  size_t used = atomic_fetch_add(&allocated, size) + size;
  size_t high = atomic_load(&peak);
  while (used > high && !atomic_compare_exchange_weak(&peak, &high, used)) {
  }
  return h + 1;
}
void test_free(void *p) {
  if (p) {
    Header *h = (Header *)p - 1;
    atomic_fetch_sub(&allocated, h->size);
    free(h);
  }
}
void *test_calloc(size_t n, size_t s) {
  void *p = test_malloc(n * s);
  memset(p, 0, n * s);
  return p;
}
void *test_realloc(void *p, size_t s) {
  if (!p)
    return test_malloc(s);
  Header *h = (Header *)p - 1;
  void *q = test_malloc(s);
  memcpy(q, p, MIN(s, h->size));
  test_free(p);
  return q;
}
#define malloc test_malloc
#define calloc test_calloc
#define realloc test_realloc
#define free test_free
static uint32_t mock_tick;
static unsigned loading_labels, spinner_signature;
static _Atomic unsigned date_reads;
uint32_t furi_get_tick(void) { return mock_tick; }
uint32_t furi_ms_to_ticks(uint32_t ms) { return ms; }
static char root[2048];
static bool deny_mutation, deny_cache;
static _Atomic int fail_after = -1;
static _Atomic int delay_us;
static int errors, launches;
static char launched[2048];
size_t memmgr_get_free_heap(void) {
  size_t used = atomic_load(&allocated), lim = atomic_load(&heap_limit);
  return used >= lim ? 0 : lim - used;
}
void *furi_record_open(const char *s) {
  UNUSED(s);
  return (void *)1;
}
void furi_record_close(const char *s) { UNUSED(s); }
static char *dupstr(const char *s) {
  size_t n = strlen(s) + 1;
  char *r = malloc(n);
  memcpy(r, s, n);
  return r;
}
FuriString *furi_string_alloc(void) {
  FuriString *s = malloc(sizeof(*s));
  s->data = dupstr("");
  return s;
}
void furi_string_free(FuriString *s) {
  if (s) {
    free(s->data);
    free(s);
  }
}
void furi_string_set_str(FuriString *s, const char *v) {
  char *n = dupstr(v);
  free(s->data);
  s->data = n;
}
void furi_string_set_f(FuriString *s, const FuriString *v) {
  furi_string_set_str(s, v->data);
}
FuriString *furi_string_alloc_set_str(const char *v) {
  FuriString *s = furi_string_alloc();
  furi_string_set_str(s, v);
  return s;
}
FuriString *furi_string_alloc_set_f(const FuriString *v) {
  return furi_string_alloc_set_str(v->data);
}
const char *furi_string_get_cstr(const FuriString *s) { return s->data; }
size_t furi_string_size(const FuriString *s) { return strlen(s->data); }
bool furi_string_empty(const FuriString *s) { return !s->data[0]; }
void furi_string_reset(FuriString *s) { furi_string_set_str(s, ""); }
void furi_string_set_strn(FuriString *s, const char *v, size_t n) {
  char *p = malloc(n + 1);
  memcpy(p, v, n);
  p[n] = 0;
  free(s->data);
  s->data = p;
}
void furi_string_cat_str(FuriString *s, const char *v) {
  size_t a = strlen(s->data), b = strlen(v);
  s->data = realloc(s->data, a + b + 1);
  memcpy(s->data + a, v, b + 1);
}
void furi_string_cat(FuriString *s, const FuriString *v) {
  furi_string_cat_str(s, v->data);
}
static void fmt(FuriString *s, bool cat, const char *f, va_list ap) {
  char b[4096];
  int n = vsnprintf(b, sizeof(b), f, ap);
  assert(n >= 0 && (size_t)n < sizeof(b));
  if (cat)
    furi_string_cat_str(s, b);
  else
    furi_string_set_str(s, b);
}
FuriString *furi_string_alloc_printf(const char *f, ...) {
  FuriString *s = furi_string_alloc();
  va_list ap; va_start(ap, f); fmt(s, false, f, ap); va_end(ap);
  return s;
}
void furi_string_printf(FuriString *s, const char *f, ...) {
  va_list ap;
  va_start(ap, f);
  fmt(s, false, f, ap);
  va_end(ap);
}
void furi_string_cat_printf(FuriString *s, const char *f, ...) {
  va_list ap;
  va_start(ap, f);
  fmt(s, true, f, ap);
  va_end(ap);
}
void furi_string_swap(FuriString *a, FuriString *b) {
  char *t = a->data;
  a->data = b->data;
  b->data = t;
}
void furi_string_left(FuriString *s, size_t n) {
  assert(n <= strlen(s->data));
  s->data[n] = 0;
}
size_t furi_string_search_rchar(const FuriString *s, char c) {
  char *p = strrchr(s->data, c);
  return p ? (size_t)(p - s->data) : SIZE_MAX;
}
size_t furi_string_search(const FuriString *s, const char *v) {
  char *p = strstr(s->data, v);
  return p ? (size_t)(p - s->data) : SIZE_MAX;
}
int furi_string_cmp_str(const FuriString *s, const char *v) {
  return strcmp(s->data, v);
}
bool furi_string_end_withi(const FuriString *s, const char *v) {
  size_t a = strlen(s->data), b = strlen(v);
  return a >= b && !strcasecmp(s->data + a - b, v);
}
static void *thread_entry(void *p) {
  FuriThread *t = p;
  t->fn(t->ctx);
  return NULL;
}
FuriThread *furi_thread_alloc_ex(const char *n, size_t size,
                                 int32_t (*fn)(void *), void *ctx) {
  UNUSED(n);
  assert(size >= 4096);
  FuriThread *t = calloc(1, sizeof(*t));
  t->fn = fn;
  t->ctx = ctx;
  return t;
}
void furi_thread_start(FuriThread *t) {
  assert(!pthread_create(&t->thread, NULL, thread_entry, t));
}
void furi_thread_join(FuriThread *t) {
  if (!t->joined) {
    assert(!pthread_join(t->thread, NULL));
    t->joined = true;
  }
}
void furi_thread_free(FuriThread *t) {
  assert(t->joined);
  free(t);
}
FuriTimer *furi_timer_alloc(void (*f)(void *), int type, void *ctx) {
  UNUSED(f);
  UNUSED(type);
  UNUSED(ctx);
  return calloc(1, sizeof(FuriTimer));
}
void furi_timer_free(FuriTimer *t) { free(t); }
void furi_timer_start(FuriTimer *t, int n) {
  UNUSED(t);
  UNUSED(n);
}
void furi_timer_stop(FuriTimer *t) { UNUSED(t); }
View *view_alloc(void) {
  View *v = calloc(1, sizeof(*v));
  pthread_mutex_init(&v->lock, NULL);
  return v;
}
void view_free(View *v) {
  free(v->model);
  pthread_mutex_destroy(&v->lock);
  free(v);
}
void view_allocate_model(View *v, int type, size_t n) {
  UNUSED(type);
  v->model = calloc(1, n);
}
void view_set_context(View *v, void *c) { v->ctx = c; }
void view_set_draw_callback(View *v, void (*f)(Canvas *, void *)) {
  v->draw = f;
}
void view_set_input_callback(View *v, bool (*f)(InputEvent *, void *)) {
  v->input = f;
}
void view_set_enter_callback(View *v, void (*f)(void *)) {
  UNUSED(v);
  UNUSED(f);
}
void view_set_exit_callback(View *v, void (*f)(void *)) {
  UNUSED(v);
  UNUSED(f);
}
#define NOOP(name, args)                                                       \
  void name args {}
NOOP(canvas_set_color, (Canvas * c, int a))
NOOP(canvas_draw_box, (Canvas * c, int a, int b, int d, int e))
NOOP(canvas_draw_rframe, (Canvas * c, int a, int b, int d, int e, int f))
void canvas_draw_line(Canvas* c, int a, int b, int d, int e) {
    UNUSED(c);
    if(b >= 20 && e >= 20) spinner_signature = spinner_signature * 31 + a * 7 + b * 11 + d * 13 + e;
} NOOP(canvas_draw_dot, (Canvas * c, int a, int b))
    NOOP(canvas_draw_str, (Canvas * c, int a, int b, const char *s))

void canvas_draw_str_aligned(Canvas* c, int a, int b, Align d, Align e, const char* s) {
    UNUSED(c); UNUSED(a); UNUSED(b); UNUSED(d); UNUSED(e);
    if(!strcmp(s, "Loading page...") || !strcmp(s,"Reading folder...") || !strcmp(s,"Reading subfolders...")) loading_labels++;
}
            NOOP(canvas_draw_icon, (Canvas * c, int a, int b, const Icon *i))
                NOOP(elements_slightly_rounded_frame,
                     (Canvas * c, int a, int b, int d, int e))
                    NOOP(elements_scrollable_text_line,
                         (Canvas * c, int a, int b, int d, FuriString *s,
                          size_t n, bool x))
                        NOOP(elements_scrollbar_pos,
                             (Canvas * c, int a, int b, int d, int e, int f))
                            NOOP(elements_multiline_text_aligned,
                                 (Canvas * c, int a, int b, Align d, Align e,
                                  const char *s))
                                SceneManager *scene_manager_alloc(
                                    const SceneManagerHandlers *h, void *c) {
  SceneManager *s = calloc(1, sizeof(*s));
  s->handlers = h;
  s->ctx = c;
  return s;
}
void scene_manager_free(SceneManager *s) { free(s); }
void scene_manager_next_scene(SceneManager *s, int n) {
  assert(s->depth < 16);
  if (s->depth)
    s->handlers->on_exit_handlers[s->stack[s->depth - 1]](s->ctx);
  s->stack[s->depth++] = n;
  s->handlers->on_enter_handlers[n](s->ctx);
}
bool scene_manager_previous_scene(SceneManager *s) {
  if (s->depth < 2)
    return false;
  s->handlers->on_exit_handlers[s->stack[--s->depth]](s->ctx);
  s->handlers->on_enter_handlers[s->stack[s->depth - 1]](s->ctx);
  return true;
}
NOOP(scene_manager_set_scene_state, (SceneManager * s, int n, int v))
bool scene_manager_handle_custom_event(SceneManager *s, uint32_t e) {
  return s->handlers->on_event_handlers[s->stack[s->depth - 1]](
      s->ctx, (SceneManagerEvent){SceneManagerEventTypeCustom, e});
}
bool scene_manager_handle_back_event(SceneManager *s) {
  return scene_manager_previous_scene(s);
}
ViewDispatcher *view_dispatcher_alloc(void) {
  return calloc(1, sizeof(ViewDispatcher));
}
void view_dispatcher_free(ViewDispatcher *v) { free(v); }
NOOP(view_dispatcher_attach_to_gui, (ViewDispatcher * v, Gui *g, int x))
void view_dispatcher_set_event_callback_context(ViewDispatcher *v, void *c) {
  v->ctx = c;
}
NOOP(view_dispatcher_set_custom_event_callback,
     (ViewDispatcher * v, bool (*f)(void *, uint32_t)))
NOOP(view_dispatcher_set_navigation_event_callback,
     (ViewDispatcher * v, bool (*f)(void *)))
NOOP(view_dispatcher_add_view, (ViewDispatcher * v, int i, View *w))
NOOP(view_dispatcher_remove_view, (ViewDispatcher * v, int i))
void view_dispatcher_send_custom_event(ViewDispatcher *v, uint32_t e) {
  assert(v->count < COUNT_OF(v->events));
  v->events[v->count++] = e;
}
void view_dispatcher_switch_to_view(ViewDispatcher *v, int i) { v->view = i; }
void view_dispatcher_stop(ViewDispatcher *v) { v->stopped = true; }
static void (*modal_script)(ViewDispatcher*);
void view_dispatcher_run(ViewDispatcher* v) { assert(modal_script); modal_script(v); }
TextInput *text_input_alloc(void) {
  TextInput *t = calloc(1, sizeof(*t));
  t->view = view_alloc();
  return t;
}
void text_input_free(TextInput *t) {
  view_free(t->view);
  free(t);
}
View *text_input_get_view(TextInput *t) { return t->view; }
NOOP(text_input_set_header_text, (TextInput * t, const char *s))
void text_input_set_result_callback(TextInput *t, void (*f)(void *), void *c,
                                    char *b, size_t n, bool x) {
  UNUSED(x);
  t->callback = f;
  t->ctx = c;
  t->buffer = b;
  t->limit = n;
}
void text_input_set_validator(TextInput *t,
                              bool (*f)(const char *, FuriString *, void *),
                              void *c) {
  UNUSED(f);
  t->validator = c;
}
void *text_input_get_validator_callback_context(TextInput *t) {
  return t->validator;
}
NOOP(text_input_reset, (TextInput * t))
ValidatorIsFile *validator_is_file_alloc_init(const char *p, const char *e,
                                              const char *c) {
  UNUSED(e);
  ValidatorIsFile *v = malloc(sizeof(*v));
  v->parent = dupstr(p);
  v->current = dupstr(c);
  return v;
}
void validator_is_file_free(ValidatorIsFile *v) {
  free(v->parent);
  free(v->current);
  free(v);
}
bool validator_is_file_callback(const char *s, FuriString *err, void *c) {
  UNUSED(err);
  UNUSED(c);
  return s[0] && strcmp(s, ".") && strcmp(s, "..") && !strchr(s, '/');
}
Widget *widget_alloc(void) {
  Widget *w = malloc(sizeof(*w));
  w->view = view_alloc();
  return w;
}
void widget_free(Widget *w) {
  view_free(w->view);
  free(w);
}
View *widget_get_view(Widget *w) { return w->view; }
NOOP(widget_reset, (Widget * w))
NOOP(widget_add_button_element,
     (Widget * w, GuiButtonType b, const char *s,
      void (*f)(GuiButtonType, InputType, void *), void *c))
NOOP(widget_add_text_box_element, (Widget * w, int a, int b, int c, int d,
                                   Align e, Align f, const char *s, bool x))
void loader_enqueue_launch(Loader *l, const char *app, const char *path,
                           int flags) {
  UNUSED(l);
  UNUSED(app);
  assert(flags == LoaderDeferredLaunchFlagGui);
  launches++;
  strlcpy(launched, path ? path : app, sizeof(launched));
}
void dialog_message_show_storage_error(DialogsApp *d, const char *s) {
  UNUSED(d);
  UNUSED(s);
  errors++;
}
static void actual(const char *p, char *out) {
  assert(!strncmp(p, "/ext", 4));
  snprintf(out, 4096, "%s%s", root, p + 4);
}
FS_Error storage_common_stat(Storage *s, const char *p, FileInfo *i) {
  UNUSED(s);
  char a[4096];
  actual(p, a);
  struct stat st;
  if (lstat(a, &st))
    return errno == ENOENT ? FSE_NOT_EXIST : FSE_DENIED;
  i->dir = S_ISDIR(st.st_mode);
  return FSE_OK;
}
// Mirror the actual firmware API: this is a volume value, not file metadata.
FS_Error storage_common_timestamp(Storage *s, const char *p, uint32_t *t) {
  UNUSED(s);
  UNUSED(p);
  *t = 7;
  return FSE_OK;
}
FbpDates *fbp_dates_device_open(void *cancel) {
  UNUSED(cancel);
  return (FbpDates *)1;
}
void fbp_dates_device_close(FbpDates *d) { UNUSED(d); }
uint32_t fbp_dates_get(FbpDates *d, const char *p) {
  atomic_fetch_add(&date_reads, 1);
  UNUSED(d);
  char a[4096];
  actual(p, a);
  struct stat st;
  if (stat(a, &st) || strstr(p, "unknown-date"))
    return 0;
  return st.st_mtime;
}
FS_Error storage_common_rename(Storage *s, const char *a, const char *b) {
  UNUSED(s);
  if (deny_mutation)
    return FSE_DENIED;
  char x[4096], y[4096];
  actual(a, x);
  actual(b, y);
  struct stat st;
  if (!stat(y, &st) && strcmp(a, b))
    return FSE_DENIED;
  return rename(x, y) ? FSE_DENIED : FSE_OK;
}
FS_Error storage_common_remove(Storage *s, const char *p) {
  UNUSED(s);
  if (deny_mutation)
    return FSE_DENIED;
  char a[4096];
  actual(p, a);
  return remove(a) ? FSE_DENIED : FSE_OK;
}
bool storage_simply_remove_recursive(Storage *s, const char *p) {
  if (deny_mutation)
    return false;
  char a[4096];
  actual(p, a);
  DIR *d = opendir(a);
  if (!d)
    return storage_common_remove(s, p) == FSE_OK;
  struct dirent *e;
  bool ok = true;
  while ((e = readdir(d))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
      continue;
    char child[4096];
    snprintf(child, sizeof(child), "%s/%s", p, e->d_name);
    if (!storage_simply_remove_recursive(s, child))
      ok = false;
  }
  closedir(d);
  return ok && storage_common_remove(s, p) == FSE_OK;
}
struct WalkDir {
  DIR *dir;
  char *path;
  WalkDir *parent;
};
static bool pushdir(DirWalk *w, const char *p) {
  char a[4096];
  actual(p, a);
  DIR *d = opendir(a);
  if (!d)
    return false;
  WalkDir *n = malloc(sizeof(*n));
  n->dir = d;
  n->path = dupstr(p);
  n->parent = w->head;
  w->head = n;
  return true;
}
DirWalk *dir_walk_alloc(Storage *s) {
  UNUSED(s);
  DirWalk *w = calloc(1, sizeof(*w));
  w->pending = furi_string_alloc();
  return w;
}
void dir_walk_close(DirWalk *w) {
  while (w->head) {
    WalkDir *n = w->head;
    w->head = n->parent;
    closedir(n->dir);
    free(n->path);
    free(n);
  }
  w->descend = false;
}
void dir_walk_free(DirWalk *w) {
  dir_walk_close(w);
  furi_string_free(w->pending);
  free(w);
}
void dir_walk_set_recursive(DirWalk *w, bool b) { w->recursive = b; }
void dir_walk_set_filter_cb(DirWalk *w,
                            bool (*f)(const char *, FileInfo *, void *),
                            void *c) {
  w->filter = f;
  w->ctx = c;
}
bool dir_walk_open(DirWalk *w, const char *p) { return pushdir(w, p); }
DirWalkResult dir_walk_read(DirWalk *w, FuriString *out, FileInfo *info) {
  if (w->head && !strcmp(w->head->path, "/ext/stress/overlong")) {
    char path[1100];
    memset(path, 'x', sizeof(path) - 1);
    path[sizeof(path) - 1] = 0;
    furi_string_set_str(out, path);
    info->dir = false;
    return DirWalkOK;
  }
  if (atomic_load(&delay_us))
    usleep(atomic_load(&delay_us));
  int f = atomic_load(&fail_after);
  if (f == 0)
    return DirWalkError;
  if (f > 0)
    atomic_fetch_sub(&fail_after, 1);
  if (w->descend) {
    w->descend = false;
    if (!pushdir(w, w->pending->data))
      return DirWalkError;
  }
  while (w->head) {
    struct dirent *e = readdir(w->head->dir);
    if (!e) {
      WalkDir *n = w->head;
      w->head = n->parent;
      closedir(n->dir);
      free(n->path);
      free(n);
      continue;
    }
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
      continue;
    furi_string_printf(out, "%s/%s", w->head->path, e->d_name);
    if (storage_common_stat(NULL, out->data, info) != FSE_OK)
      return DirWalkError;
    if (w->filter && !w->filter(out->data, info, w->ctx))
      continue;
    if (info->dir && w->recursive) {
      w->descend = true;
      furi_string_set(w->pending, out);
    }
    return DirWalkOK;
  }
  return DirWalkLast;
}
void path_extract_filename(FuriString *p, FuriString *out, bool strip) {
  const char *slash = strrchr(p->data, '/');
  furi_string_set_str(out, slash ? slash + 1 : p->data);
  if (strip) {
    char *dot = strrchr(out->data, '.');
    if (dot)
      *dot = 0;
  }
}
void path_extract_dirname(const char *p, FuriString *out) {
  const char *slash = strrchr(p, '/');
  furi_string_set_strn(out, p, slash ? (size_t)(slash - p) : 0);
}
void path_extract_extension(FuriString *p, char *out, size_t size) {
  const char *dot = strrchr(p->data, '.');
  strlcpy(out, dot ? dot : "", size);
}

File *storage_file_alloc(Storage *s) {
  UNUSED(s);
  return calloc(1, sizeof(File));
}
void storage_file_free(File *f) {
  storage_file_close(f);
  free(f);
}
bool storage_file_open(File *f, const char *p, int access, int mode) {
  UNUSED(mode);
  if (deny_cache)
    return false;
  char local[4096];
  actual(p, local);
  f->handle = fopen(local, access == FSAM_WRITE ? "wb" : "rb");
  f->error = f->handle ? FSE_OK : FSE_NOT_EXIST;
  return f->handle != NULL;
}
void storage_file_close(File *f) {
  if (f->handle) {
    fclose(f->handle);
    f->handle = NULL;
  }
}
size_t storage_file_read(File *f, void *data, size_t n) {
  size_t count = fread(data, 1, n, f->handle);
  f->error = ferror(f->handle) ? FSE_INTERNAL : FSE_OK;
  return count;
}
size_t storage_file_write(File *f, const void *data, size_t n) {
  size_t count = fwrite(data, 1, n, f->handle);
  f->error = ferror(f->handle) ? FSE_INTERNAL : FSE_OK;
  return count;
}
FS_Error storage_file_get_error(File *f) { return f->error; }
bool storage_simply_mkdir(Storage *s, const char *p) {
  UNUSED(s);
  char local[4096];
  actual(p, local);
  return !mkdir(local, 0700) || errno == EEXIST;
}

// Test fixtures/oracles use the host allocator; the application allocator above
// measures only FAP-owned memory, not the test's reference listing.
#undef malloc
#undef calloc
#undef realloc
#undef free
static FsBrowser* test_browser_alloc(void) {
    FsBrowser* a = calloc(1,sizeof(*a));
    a->browser = fbp_browser_alloc();
    a->dispatcher = view_dispatcher_alloc();
    a->selected = furi_string_alloc();
    fbp_browser_set_callback(a->browser,send_event,a);
    return a;
}
static void test_browser_free(FsBrowser* a) {
    fbp_browser_free(a->browser);
    view_dispatcher_free(a->dispatcher);
    furi_string_free(a->selected);
    free(a);
}
static void wait_scan(FsBrowser *a) {
  if (a->browser->scan_thread)
    furi_thread_join(a->browser->scan_thread);
}
static void drain(FsBrowser *a) {
  ViewDispatcher *v = a->dispatcher;
  while (v->count) {
    uint32_t e = v->events[0];
    memmove(v->events, v->events + 1, --v->count * sizeof(uint32_t));
    browser_event(a, e);
  }
}
static void key(FsBrowser *a, InputKey k, InputType t) {
  InputEvent e = {t, k};
  a->browser->view->input(&e, a->browser);
  drain(a);
}
static void event(FsBrowser *a, uint32_t e) {
  browser_event(a, e);
}
static void invariant(FsBrowser *a) {
  wait_scan(a);
  FbpBrowserViewModel *m = a->browser->view->model;
  size_t n = fbp_files_array_size(m->files);
  assert(n <= FILE_LIST_BUF_LEN);
  assert(!m->folder_loading);
  if (n) {
    assert(m->item_idx >= m->array_offset);
    assert(m->item_idx < m->array_offset + (int)n);
    assert(m->item_idx < (int)m->item_cnt);
    assert(m->list_offset >= m->array_offset);
  } else
    assert(!m->item_cnt);
  Canvas canvas = {0};
  a->browser->view->draw(&canvas, m);
}
static void open_scope(FsBrowser *a, const char *p, bool flat,
                       FbpSortMode sort) {
  fbp_stop_scan(a->browser);
  furi_string_set(a->browser->path, p);
  FbpBrowserViewModel *m = a->browser->view->model;
  m->flattened = flat;
  m->sort_mode = sort;
  fbp_open_dir(a->browser);
  invariant(a);
  assert(!m->error[0]);
}
typedef struct {
  char *path;
  uint32_t ts;
  bool dir;
} Expected;
static Expected *expected;
static size_t nexpected, capacity;
static FbpSortMode order;
static void collect(const char *p, bool flat) {
  char full[4096];
  actual(p, full);
  DIR *d = opendir(full);
  assert(d);
  struct dirent *e;
  while ((e = readdir(d))) {
    if (e->d_name[0] == '.')
      continue;
    char child[4096], local[4096];
    snprintf(child, sizeof(child), "%s/%s", p, e->d_name);
    actual(child, local);
    struct stat st;
    assert(!lstat(local, &st));
    bool dir = S_ISDIR(st.st_mode);
    if (!dir || !flat) {
      if (nexpected == capacity) {
        capacity = capacity ? capacity * 2 : 128;
        expected = realloc(expected, capacity * sizeof(*expected));
        assert(expected);
      }
      expected[nexpected++] = (Expected){
          strdup(child),
          strstr(child, "unknown-date") ? 0 : (uint32_t)st.st_mtime, dir};
    }
    if (dir && flat)
      collect(child, true);
  }
  closedir(d);
}
static int compare_expected(const void *aa, const void *bb) {
  const Expected *a = aa, *b = bb;
  if (a->dir != b->dir)
    return a->dir ? -1 : 1;
  const char *an = strrchr(a->path, '/') + 1, *bn = strrchr(b->path, '/') + 1;
  int n = strcasecmp(an, bn);
  if (!n)
    n = strcmp(a->path, b->path);
  if (order == FbpSortNameAsc)
    return n;
  if (order == FbpSortNameDesc)
    return -n;
  if (!a->ts != !b->ts)
    return a->ts ? -1 : 1;
  if (a->ts != b->ts)
    return order == FbpSortNewest ? (a->ts > b->ts ? -1 : 1)
                                  : (a->ts < b->ts ? -1 : 1);
  return n;
}
static void oracle(const char *p, bool flat, FbpSortMode sort) {
  for (size_t i = 0; i < nexpected; i++)
    free(expected[i].path);
  nexpected = 0;
  collect(p, flat);
  order = sort;
  qsort(expected, nexpected, sizeof(*expected), compare_expected);
}
static size_t comparisons;
static void assert_page(FsBrowser *a) {
  invariant(a);
  FbpBrowserViewModel *m = a->browser->view->model;
  assert(m->item_cnt == nexpected);
  for (size_t i = 0; i < fbp_files_array_size(m->files); i++) {
    FbpFile_t *f = fbp_files_array_get(m->files, i);
    assert(!strcmp(furi_string_get_cstr(f->path),
                   expected[m->array_offset + i].path));
    comparisons++;
  }
}
static void check_all_pages(FsBrowser *a, const char *p, bool flat,
                            FbpSortMode sort) {
  oracle(p, flat, sort);
  open_scope(a, p, flat, sort);
  assert_page(a);
  FbpBrowserViewModel *m = a->browser->view->model;
  while ((size_t)m->array_offset + fbp_files_array_size(m->files) < nexpected) {
    event(a, FbpBrowserEventLoadNextItems);
    assert_page(a);
  }
  while (m->array_offset) {
    event(a, FbpBrowserEventLoadPrevItems);
    assert_page(a);
  }
  if (nexpected) {
    m->item_idx = 0;
    fbp_update_offset(a->browser);
    key(a, InputKeyUp, InputTypeShort);
    assert_page(a);
    assert(m->item_idx == (int)nexpected - 1);
    key(a, InputKeyDown, InputTypeShort);
    assert_page(a);
    assert(!m->item_idx);
  }
}
static size_t scopes;
static void check_folders(FsBrowser *a, const char *p) {
  for (int flat = 0; flat < 2; flat++)
    for (int sort = 0; sort < FbpSortTotal; sort++)
      check_all_pages(a, p, flat, sort);
  scopes++;
  char local[4096];
  actual(p, local);
  DIR *d = opendir(local);
  assert(d);
  struct dirent *e;
  while ((e = readdir(d))) {
    if (e->d_name[0] == '.')
      continue;
    char child[4096];
    snprintf(child, sizeof(child), "%s/%s", p, e->d_name);
    FileInfo info;
    if (storage_common_stat(NULL, child, &info) == FSE_OK && info.dir)
      check_folders(a, child);
  }
  closedir(d);
}
static void set_selected(FsBrowser *a, const char *p) {
  FbpBrowserViewModel *m = a->browser->view->model;
  bool found = false;
  for (size_t i = 0; i < fbp_files_array_size(m->files); i++)
    if (!strcmp(fbp_files_array_get(m->files, i)->path->data, p)) {
      m->item_idx = m->array_offset + i;
      found = true;
    }
  assert(found);
  fbp_update_offset(a->browser);
}
static unsigned modal_case;
static void modal_actions(ViewDispatcher* v) {
    FsBrowser* a=v->ctx;
    invariant(a);
    FbpBrowserViewModel* m=a->browser->view->model;
    assert(!m->flattened && m->sort_mode==FbpSortNameAsc);
    if(modal_case==0) {
        // Empty folder: Back closes only the menu; confirming returns an action.
        assert(!m->item_cnt);
        key(a,InputKeyOk,InputTypeLong);assert(m->menu);
        for(unsigned i=0;i<10;i++)key(a,InputKeyDown,InputTypeShort);
        assert(m->menu_idx==0);
        key(a,InputKeyBack,InputTypeShort);assert(!m->menu && !v->stopped);
        key(a,InputKeyOk,InputTypeLong);key(a,InputKeyOk,InputTypeShort);
        assert(a->result==FsBrowserDirectoryAction && v->stopped);
    } else if(modal_case==1) {
        key(a,InputKeyLeft,InputTypeShort);invariant(a);
        assert(m->flattened && m->item_cnt);
        for(size_t i=0;i<fbp_files_array_size(m->files);i++)
            assert(furi_string_end_withi(fbp_files_array_get(m->files,i)->path,".nfc"));
        // A nested selection must not change the directory action's destination.
        assert(strchr(fbp_get_current_file(a->browser)->path->data+strlen(a->browser->path->data)+1,'/'));
        key(a,InputKeyOk,InputTypeLong);key(a,InputKeyOk,InputTypeShort);
        assert(a->result==FsBrowserDirectoryAction);
    } else if(modal_case==2) {
        assert(m->item_cnt==1);
        key(a,InputKeyOk,InputTypeShort);
        assert(a->result==FsBrowserFileSelected && v->stopped);
    } else {
        key(a,InputKeyBack,InputTypeShort);
        assert(a->result==FsBrowserCancelled && v->stopped);
    }
}
static void check_modal_api(void) {
    FsBrowserConfig config={.root_path="/ext/nfc",.extension=".nfc",.action_label="Read / Unlock",.cache_directory="/ext/apps_data/browser-test"};
    FuriString* dir=furi_string_alloc();FuriString* selected=furi_string_alloc_set_str("untouched");
    modal_script=modal_actions;
    const char* paths[]={"/ext/scenarios/empty","/ext/nfc/tonies","/ext/scenarios/count-1","/ext/invalid"};
    for(modal_case=0;modal_case<4;modal_case++) {
        config.root_path=modal_case==0||modal_case==2?paths[modal_case]:"/ext/nfc";
        furi_string_set_str(dir,paths[modal_case]);
        furi_string_set_str(selected,"untouched");
        FsBrowserResult r=fs_browser_run((Gui*)1,&config,dir,selected);
        if(modal_case<2){assert(r==FsBrowserDirectoryAction);assert(!strcmp(dir->data,paths[modal_case]));assert(!strcmp(selected->data,"untouched"));}
        else if(modal_case==2){assert(r==FsBrowserFileSelected && furi_string_end_withi(selected,".nfc"));}
        else {assert(r==FsBrowserCancelled && !strcmp(dir->data,"/ext/nfc"));}
    }
    furi_string_free(dir);furi_string_free(selected);
    puts("PASS public modal API: empty-folder action, single-option menu/cancel, flat nested selection keeps current directory, NFC filtering, selected-file result, root boundary and clean teardown");
}
int main(int argc, char **argv) {
  assert(argc == 2);
  strlcpy(root, argv[1], sizeof(root));
  FsBrowser *a = test_browser_alloc();
  fbp_open_dir(a->browser);
  invariant(a);
  FbpBrowserViewModel *m = a->browser->view->model;
  assert(m->sort_mode == FbpSortNameAsc && !m->flattened);
  assert(!atomic_load(&date_reads));
  oracle("/ext/nfc", false, FbpSortNameAsc); assert_page(a);
  // No spinner before 700ms; animate after the threshold, and never allow
  // actions against the retained page while a replacement is loading.
  Canvas loading_canvas = {0};
  m->folder_loading = m->list_loading = true; m->loading_started = 1000;
  mock_tick = 1699; loading_labels = 0;
  a->browser->view->draw(&loading_canvas, m); assert(!loading_labels);
  assert(!fbp_is_item_in_array(m, m->item_idx));
  mock_tick = 1700; spinner_signature = 0;
  a->browser->view->draw(&loading_canvas, m); assert(loading_labels == 1);
  unsigned first_phase = spinner_signature;
  mock_tick = 1800; spinner_signature = 0;
  a->browser->view->draw(&loading_canvas, m); assert(loading_labels == 2 && spinner_signature != first_phase);
  m->folder_loading = m->list_loading = false;
  set_selected(a, "/ext/nfc/tonies"); event(a, FbpBrowserEventEnterDir);
  assert(!m->flattened && m->sort_mode == FbpSortNameAsc);
  key(a, InputKeyLeft, InputTypeShort); invariant(a);
  oracle("/ext/nfc/tonies", true, FbpSortNameAsc); assert_page(a);
  assert(!atomic_load(&date_reads));
  key(a, InputKeyRight, InputTypeShort); invariant(a); // Z-A, still no dates.
  assert(!atomic_load(&date_reads));
  key(a, InputKeyRight, InputTypeShort); invariant(a); // Date sort requested.
  assert(atomic_load(&date_reads));
  oracle("/ext/nfc/tonies", true, FbpSortNewest); assert_page(a);
  unsigned dates_before_back = atomic_load(&date_reads);
  key(a, InputKeyBack, InputTypeShort); invariant(a);
  assert(!m->flattened && m->sort_mode == FbpSortNameAsc);
  assert(atomic_load(&date_reads) == dates_before_back);
  oracle("/ext/nfc", false, FbpSortNameAsc); assert_page(a);
  check_folders(a, "/ext/nfc");
  check_folders(a, "/ext/scenarios");
  printf("PASS full library and scenarios: %zu scopes, all 8 view/sort "
         "combinations, %zu "
         "page-entry comparisons\n",
         scopes, comparisons);
  fflush(stdout);
  open_scope(a, "/ext/scenarios/empty", true, FbpSortNewest);
  for (int n = 0; n < 200; n++) {
    key(a, InputKeyUp, InputTypeRepeat);
    key(a, InputKeyDown, InputTypeRepeat);
    key(a, InputKeyOk, InputTypeShort);
    key(a, InputKeyOk, InputTypeLong);
    invariant(a);
    assert(m->menu);
    key(a, InputKeyBack, InputTypeShort);
  }
  assert(!m->item_cnt);
  // Actual view callback crosses page boundaries and wraps correctly.
  open_scope(a, "/ext/scenarios/count-101", true, FbpSortNewest);
  for (int i = 0; i < 303; i++) {
    key(a, InputKeyDown, InputTypeShort);
    invariant(a);
    assert(m->item_idx == (i + 1) % 101);
  }
  for (int i = 0; i < 303; i++) {
    key(a, InputKeyUp, InputTypeRepeat);
    invariant(a);
    assert(m->item_idx == 100 - i % 101);
  }
  // Rapid controls cancel scans while the worker is actually active.
  atomic_store(&delay_us, 300);
  open_scope(a, "/ext/nfc/tonies", true, FbpSortNewest);
  for (int i = 0; i < 200; i++) {
    fbp_refresh_dir(a->browser);
    key(a, i % 2 ? InputKeyLeft : InputKeyRight, InputTypeShort);
    key(a, InputKeyDown, InputTypeShort);
    key(a, InputKeyOk, InputTypeLong);
  }
  atomic_store(&delay_us, 0);
  invariant(a);
  // A read failure and low-memory condition must show errors, not a partial
  // list.
  furi_string_set(a->browser->path, "/ext/nfc/tonies");
  atomic_store(&fail_after, 3);
  fbp_refresh_dir(a->browser);
  invariant(a);
  assert(m->error[0] && !m->item_cnt);
  atomic_store(&fail_after, -1);
  atomic_store(&heap_limit, atomic_load(&allocated) + 1024);
  fbp_open_dir(a->browser);
  invariant(a);
  assert(m->error[0] && !m->item_cnt);
  atomic_store(&heap_limit, 128 * 1024);
  fbp_open_dir(a->browser);
  invariant(a);
  assert(!m->error[0]);
  furi_string_set(a->browser->path, "/ext/missing-folder");
  fbp_open_dir(a->browser);
  invariant(a);
  assert(m->error[0]);
  // A much larger library must retain exactly the same page bound and sort
  // globally.
  for (int sort = 0; sort < FbpSortTotal; sort++) {
    oracle("/ext/stress/large", true, sort);
    open_scope(a, "/ext/stress/large", true, sort);
    assert_page(a);
    assert(m->item_cnt == 10000);
    event(a, FbpBrowserEventLoadNextItems);
    assert_page(a);
    event(a, FbpBrowserEventLoadLastItems);
    assert_page(a);
    assert(m->item_idx == 9999);
    event(a, FbpBrowserEventLoadPrevItems);
    assert_page(a);
  }
  // A corrupted or truncated snapshot must never publish a partial listing.
  char cache_path[4096];
  actual(furi_string_get_cstr(a->browser->cache_path), cache_path);
  FILE *damaged = fopen(cache_path, "r+b");
  assert(damaged);
  assert(!fseek(damaged, 20, SEEK_SET));
  fputc(0, damaged);
  fclose(damaged);
  event(a, FbpBrowserEventLoadNextItems);
  invariant(a);
  assert(m->error[0] && !m->item_cnt);
  fbp_refresh_dir(a->browser);
  invariant(a);
  assert(!m->error[0]);
  assert(!truncate(cache_path, 7));
  event(a, FbpBrowserEventLoadNextItems);
  invariant(a);
  assert(m->error[0]);
  fbp_refresh_dir(a->browser);
  invariant(a);
  assert(!m->error[0]);
  // Read-only/unavailable cache falls back to the same bounded scanner.
  deny_cache = true;
  furi_string_set(a->browser->path, "/ext/scenarios/count-101");
  fbp_refresh_dir(a->browser);
  oracle("/ext/scenarios/count-101", true, m->sort_mode);
  assert_page(a);
  assert(!a->browser->cache_valid);
  event(a, FbpBrowserEventLoadNextItems);
  assert_page(a);
  deny_cache = false;
  furi_string_set(a->browser->path, "/ext/stress/overlong");
  fbp_open_dir(a->browser);
  invariant(a);
  assert(strstr(m->error, "Path too long"));
  // A changed directory may remove the boundary item between pages.
  open_scope(a, "/ext/scenarios/count-101", true, FbpSortNameAsc);
  char vanished[4096];
  strlcpy(vanished, fbp_files_array_get(m->files, 49)->path->data,
          sizeof(vanished));
  assert(storage_common_remove(NULL, vanished) == FSE_OK);
  a->browser->cache_valid = false;
  event(a, FbpBrowserEventLoadNextItems);
  oracle("/ext/scenarios/count-101", true, FbpSortNameAsc);
  assert_page(a);
  assert(m->array_offset == 49);
  // A selection outside the loaded page is never dereferenced, even on OK/menu.
  m->item_idx = -1;
  assert(!fbp_get_current_file(a->browser));
  key(a, InputKeyOk, InputTypeLong);
  assert(m->menu); // Directory menu does not depend on an item selection.
  key(a, InputKeyBack, InputTypeShort);
  open_scope(a, "/ext/scenarios/actions", false, FbpSortNameAsc);
  for (int i = 0; i < 1000; i++) {
    key(a, i % 2 ? InputKeyDown : InputKeyUp, InputTypeRepeat);
    invariant(a);
  }
  // Folder entry/back and storage-root exit use full paths.
  set_selected(a, "/ext/scenarios/actions/nested");
  key(a, InputKeyOk, InputTypeShort);
  invariant(a);
  assert(!strcmp(a->browser->path->data, "/ext/scenarios/actions/nested"));
  key(a, InputKeyBack, InputTypeShort);
  invariant(a);
  assert(!strcmp(a->browser->path->data, "/ext/scenarios/actions"));
  key(a, InputKeyOk, InputTypeLong);
  for (int i = 0; i < 100; i++) {
    key(a, InputKeyDown, InputTypeShort);
    assert(m->menu_idx < MENU_ITEMS);
  }
  key(a, InputKeyBack, InputTypeShort);
  assert(!m->menu);

  // Exit during a live scan joins/cancels before any model or timer is freed.
  atomic_store(&delay_us, 500);
  furi_string_set(a->browser->path, "/ext/nfc/tonies");
  fbp_refresh_dir(a->browser);
  test_browser_free(a);
  atomic_store(&delay_us, 0);
  for (size_t i = 0; i < nexpected; i++)
    free(expected[i].path);
  free(expected);
  check_modal_api();
  printf("PASS empty input, 606 page transitions, 200 rapid switches, "
         "directory-menu/cancel/errors, live-scan exit; 10,000-file stress, "
         "missing anchors, "
         "invalid names; app heap peak %zu bytes, retained %zu bytes\n",
         atomic_load(&peak), atomic_load(&allocated));
  assert(atomic_load(&allocated) == 0);
  return 0;
}
