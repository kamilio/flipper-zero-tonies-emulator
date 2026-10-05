#pragma once
#include "fbp_files.h"

typedef struct FbpCache FbpCache;
// A buffered, disposable metadata snapshot. No NFC file contents are read.
FbpCache* fbp_cache_open(Storage* storage, const char* path, bool write);
bool fbp_cache_write(FbpCache* cache, const FbpFile_t* item);
// 1 = entry, 0 = EOF, -1 = corrupt/unreadable cache.
int fbp_cache_read(FbpCache* cache, FbpFile_t* item);
bool fbp_cache_close(FbpCache* cache);
