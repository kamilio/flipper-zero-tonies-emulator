#include "fbp_cache.h"

#define CACHE_BUFFER_SIZE 4096
struct FbpCache {
    File* file;
    bool write;
    bool ok;
    uint32_t count;
    uint32_t checksum;
    size_t position;
    size_t size;
    uint8_t buffer[CACHE_BUFFER_SIZE];
};

FbpCache* fbp_cache_open(Storage* storage, const char* path, bool write) {
    FbpCache* cache = calloc(1, sizeof(FbpCache));
    cache->file = storage_file_alloc(storage);
    cache->write = write;
    cache->checksum = 2166136261u;
    cache->ok = storage_file_open(cache->file, path, write ? FSAM_WRITE : FSAM_READ, write ? FSOM_CREATE_ALWAYS : FSOM_OPEN_EXISTING);
    if(!cache->ok) { storage_file_free(cache->file); free(cache); return NULL; }
    return cache;
}

static bool flush(FbpCache* cache) {
    if(cache->size && storage_file_write(cache->file, cache->buffer, cache->size) != cache->size) cache->ok = false;
    cache->size = 0;
    return cache->ok;
}

static bool write_bytes(FbpCache* cache, const void* data, size_t size) {
    const uint8_t* bytes = data;
    while(size && cache->ok) {
        size_t count = MIN(size, CACHE_BUFFER_SIZE - cache->size);
        memcpy(cache->buffer + cache->size, bytes, count);
        cache->size += count;
        bytes += count;
        size -= count;
        if(cache->size == CACHE_BUFFER_SIZE) flush(cache);
    }
    return cache->ok;
}

static bool read_bytes(FbpCache* cache, void* data, size_t size) {
    uint8_t* bytes = data;
    while(size) {
        if(cache->position == cache->size) {
            cache->position = 0;
            cache->size = storage_file_read(cache->file, cache->buffer, CACHE_BUFFER_SIZE);
            if(!cache->size) return false;
        }
        size_t count = MIN(size, cache->size - cache->position);
        memcpy(bytes, cache->buffer + cache->position, count);
        cache->position += count;
        bytes += count;
        size -= count;
    }
    return true;
}

static void hash_bytes(FbpCache* cache, const void* data, size_t size) {
    const uint8_t* bytes = data;
    for(size_t i = 0; i < size; ++i) cache->checksum = (cache->checksum ^ bytes[i]) * 16777619u;
}
static uint32_t decode32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

bool fbp_cache_write(FbpCache* cache, const FbpFile_t* item) {
    size_t size = furi_string_size(item->path);
    uint32_t ts = item->timestamp;
    if(!size || size >= 1024) return false;
    const uint8_t header[] = {size & 255, size >> 8, item->type, ts & 255, (ts >> 8) & 255, (ts >> 16) & 255, ts >> 24};
    hash_bytes(cache, header, sizeof(header));
    hash_bytes(cache, furi_string_get_cstr(item->path), size);
    cache->count++;
    return write_bytes(cache, header, sizeof(header)) && write_bytes(cache, furi_string_get_cstr(item->path), size);
}

int fbp_cache_read(FbpCache* cache, FbpFile_t* item) {
    uint8_t header[7];
    if(!read_bytes(cache, header, sizeof(header))) { cache->ok = false; return -1; }
    size_t size = header[0] | ((size_t)header[1] << 8);
    if(!size && header[2] == 255) {
        uint8_t checksum[4];
        if(!read_bytes(cache, checksum, sizeof(checksum)) || decode32(header + 3) != cache->count || decode32(checksum) != cache->checksum) {
            cache->ok = false;
            return -1;
        }
        return 0;
    }
    if(!size || size >= 1024 || header[2] >= FbpFileTypeLoading) { cache->ok = false; return -1; }
    char* path = malloc(size + 1);
    if(!read_bytes(cache, path, size)) { free(path); cache->ok = false; return -1; }
    path[size] = '\0';
    hash_bytes(cache, header, sizeof(header));
    hash_bytes(cache, path, size);
    cache->count++;
    if(strlen(path) != size || strncmp(path, "/ext/", 5)) { free(path); cache->ok = false; return -1; }
    furi_string_set_str(item->path, path);
    free(path);
    item->type = header[2];
    item->timestamp = (uint32_t)header[3] | ((uint32_t)header[4] << 8) | ((uint32_t)header[5] << 16) | ((uint32_t)header[6] << 24);
    return 1;
}

bool fbp_cache_close(FbpCache* cache) {
    if(!cache) return false;
    if(cache->write && cache->ok) {
        uint32_t n = cache->count, hash = cache->checksum;
        const uint8_t footer[] = {0, 0, 255, n & 255, (n >> 8) & 255, (n >> 16) & 255, n >> 24,
            hash & 255, (hash >> 8) & 255, (hash >> 16) & 255, hash >> 24};
        write_bytes(cache, footer, sizeof(footer));
        flush(cache);
    }
    bool ok = cache->ok;
    storage_file_close(cache->file);
    storage_file_free(cache->file);
    free(cache);
    return ok;
}
