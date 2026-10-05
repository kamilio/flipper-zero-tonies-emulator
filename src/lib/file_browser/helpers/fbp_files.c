#include "fbp_files.h"
#include "fbp_browser.h"

#define TAG "FilemanPlus"

void fbp_set_file_type(FbpFile_t* file, const char* dir_path, bool is_folder) {
    furi_assert(file);
    UNUSED(dir_path);

    if(is_folder) {
        file->type = FbpFileTypeFolder;
        return;
    }

    for(size_t i = 0; i < COUNT_OF(known_ext); i++) {
        if((known_ext[i][0] == '?') || (known_ext[i][0] == '*')) continue;
        if(furi_string_end_withi(file->path, known_ext[i])) {
            if(i == FbpFileTypeBadUsb) {
                if(furi_string_search(file->path, EXT_PATH("badusb/")) == 0) {
                    file->type = i;
                    return; // *.txt is a BadUSB script only inside the BadUSB folder
                }
            } else {
                file->type = i;
                return;
            }
        }
    }

    file->type = FbpFileTypeUnknown;
}

bool fbp_delete_file(const char* path) {
    furi_assert(path);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FileInfo info;
    FS_Error status = storage_common_stat(storage, path, &info);
    bool ok = status == FSE_NOT_EXIST;
    if(status == FSE_OK) {
        ok = file_info_is_dir(&info) ? storage_simply_remove_recursive(storage, path) :
                                      storage_common_remove(storage, path) == FSE_OK;
    }
    furi_record_close(RECORD_STORAGE);
    return ok;
}
