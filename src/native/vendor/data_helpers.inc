/* From slix_i.c */
SlixError slix_process_iso15693_3_error(Iso15693_3Error iso15693_3_error) {
    switch(iso15693_3_error) {
    case Iso15693_3ErrorNone:
        return SlixErrorNone;
    case Iso15693_3ErrorTimeout:
        return SlixErrorTimeout;
    case Iso15693_3ErrorFormat:
        return SlixErrorFormat;
    case Iso15693_3ErrorInternal:
        return SlixErrorInternal;
    default:
        return SlixErrorUnknown;
    }
}

/* From iso15693_3_i.c */
void iso15693_3_append_block(const Iso15693_3Data* data, uint8_t block_num, BitBuffer* buf) {
    furi_assert(block_num < data->system_info.block_count);

    const uint32_t block_offset = block_num * data->system_info.block_size;
    const uint8_t* block_data = simple_array_cget(data->block_data, block_offset);

    bit_buffer_append_bytes(buf, block_data, data->system_info.block_size);
}

/* From iso15693_3_i.c */
void iso15693_3_append_block_security(
    const Iso15693_3Data* data,
    uint8_t block_num,
    BitBuffer* buf) {
    bit_buffer_append_byte(buf, *(uint8_t*)simple_array_cget(data->block_security, block_num));
}

/* From slix_i.c */
void slix_increment_counter(SlixData* data) {
    furi_assert(data);

    const uint8_t* block_data =
        iso15693_3_get_block_data(data->iso15693_3_data, SLIX_COUNTER_BLOCK_NUM);

    SlixCounter counter;
    memcpy(counter.bytes, block_data, SLIX_BLOCK_SIZE);
    counter.value += 1;

    iso15693_3_set_block_data(
        data->iso15693_3_data, SLIX_COUNTER_BLOCK_NUM, counter.bytes, sizeof(SlixCounter));
}

/* From slix_i.c */
void slix_set_privacy_mode(SlixData* data, bool set) {
    furi_assert(data);

    data->privacy = set;
}

/* From iso15693_3_i.c */
void iso15693_3_append_uid(const Iso15693_3Data* data, BitBuffer* buf) {
    for(size_t i = 0; i < ISO15693_3_UID_SIZE; ++i) {
        // Reverse the UID
        bit_buffer_append_byte(buf, data->uid[ISO15693_3_UID_SIZE - i - 1]);
    }
}

/* From iso15693_3_i.c */
void iso15693_3_set_block_locked(Iso15693_3Data* data, uint8_t block_index, bool locked) {
    furi_assert(data);
    furi_assert(block_index < data->system_info.block_count);

    *(uint8_t*)simple_array_get(data->block_security, block_index) = locked ? 1 : 0;
}

/* From slix_i.c */
void slix_set_password(SlixData* data, SlixPasswordType password_type, SlixPassword password) {
    furi_assert(data);
    furi_assert(password_type < SlixPasswordTypeCount);

    data->passwords[password_type] = password;
}

/* From iso15693_3_i.c */
bool iso15693_3_is_equal_uid(const Iso15693_3Data* data, const uint8_t* uid) {
    for(size_t i = 0; i < ISO15693_3_UID_SIZE; ++i) {
        if(data->uid[i] != uid[ISO15693_3_UID_SIZE - i - 1]) return false;
    }
    return true;
}

/* From iso15693_3_i.c */
void iso15693_3_set_block_data(
    Iso15693_3Data* data,
    uint8_t block_num,
    const uint8_t* block_data,
    size_t block_data_size) {
    furi_assert(block_num < data->system_info.block_count);
    furi_assert(block_data_size == data->system_info.block_size);

    const uint32_t block_offset = block_num * data->system_info.block_size;
    uint8_t* block = simple_array_get(data->block_data, block_offset);

    memcpy(block, block_data, block_data_size);
}
