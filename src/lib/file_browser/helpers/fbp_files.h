#pragma once

#include <m-array.h>
#include <furi.h>
#include <storage/storage.h>

typedef enum {
    FbpFileTypeIButton,
    FbpFileTypeNFC,
    FbpFileTypeSubGhz,
    FbpFileTypeLFRFID,
    FbpFileTypeInfrared,
    FbpFileTypeBadUsb,
    FbpFileTypeU2f,
    FbpFileTypeUpdateManifest,
    FbpFileTypeApplication,
    FbpFileTypeJS,
    FbpFileTypeFolder,
    FbpFileTypeUnknown,
    FbpFileTypeLoading,
} FbpFileTypeEnum;

typedef struct {
    FuriString* path; // full path, or just filename in worker (native) mode
    FuriString* rel_dir; // flattened mode: directory relative to view root ("" if root)
    FbpFileTypeEnum type;
    uint32_t timestamp; // mtime (unix), 0 = unknown/not loaded
} FbpFile_t;

static void FbpFile_t_init(FbpFile_t* obj) {
    obj->path = furi_string_alloc();
    obj->rel_dir = furi_string_alloc();
    obj->type = FbpFileTypeUnknown;
    obj->timestamp = 0;
}

static void FbpFile_t_init_set(FbpFile_t* obj, const FbpFile_t* src) {
    obj->path = furi_string_alloc_set(src->path);
    obj->rel_dir = furi_string_alloc_set(src->rel_dir);
    obj->type = src->type;
    obj->timestamp = src->timestamp;
}

static void FbpFile_t_set(FbpFile_t* obj, const FbpFile_t* src) {
    furi_string_set(obj->path, src->path);
    furi_string_set(obj->rel_dir, src->rel_dir);
    obj->type = src->type;
    obj->timestamp = src->timestamp;
}

static void FbpFile_t_clear(FbpFile_t* obj) {
    furi_string_free(obj->path);
    furi_string_free(obj->rel_dir);
}

ARRAY_DEF(
    fbp_files_array,
    FbpFile_t,
    (INIT(API_2(FbpFile_t_init)),
     SET(API_6(FbpFile_t_set)),
     INIT_SET(API_6(FbpFile_t_init_set)),
     CLEAR(API_2(FbpFile_t_clear))))

void fbp_set_file_type(FbpFile_t* file, const char* dir_path, bool is_folder);
bool fbp_delete_file(const char* path);
