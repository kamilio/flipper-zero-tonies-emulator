#include "tonie_protocol.h"

#include <string.h>

#define FLAG_INVENTORY 0x04U
#define FLAG_SELECTED 0x10U
#define FLAG_ADDRESSED 0x20U
#define FLAG_OPTION 0x40U
#define FLAG_AFI 0x10U
#define FLAG_ONE_SLOT 0x20U
#define NXP_MANUFACTURER 0x04U

static TonieProtocolResult result_new(uint8_t command) {
    TonieProtocolResult result;
    memset(&result, 0, sizeof(result));
    result.command = command;
    result.code = TonieResultOk;
    return result;
}

static void response_begin(TonieProtocolResult* result) {
    result->action = TonieActionRespondNow;
    result->response[0] = 0x00;
    result->response_len = 1;
}

static TonieProtocolResult error_response(uint8_t command, uint8_t error, TonieResultCode code) {
    TonieProtocolResult result = result_new(command);
    result.action = TonieActionRespondNow;
    result.code = code;
    result.response[0] = 0x01;
    result.response[1] = error;
    result.response_len = 2;
    return result;
}

static TonieProtocolResult ignored(uint8_t command, TonieResultCode code) {
    TonieProtocolResult result = result_new(command);
    result.code = code;
    return result;
}

void tonie_uid_to_wire(const uint8_t internal_uid[8], uint8_t wire_uid[8]) {
    for(size_t i = 0; i < 8; ++i) wire_uid[i] = internal_uid[7U - i];
}

void tonie_uid_from_wire(const uint8_t wire_uid[8], uint8_t internal_uid[8]) {
    tonie_uid_to_wire(wire_uid, internal_uid);
}

static uint8_t uid_wire_bit(const uint8_t uid[8], uint8_t bit) {
    uint8_t wire[8];
    tonie_uid_to_wire(uid, wire);
    return (wire[bit / 8U] >> (bit % 8U)) & 1U;
}

bool tonie_inventory_mask_matches(
    const uint8_t internal_uid[8],
    const uint8_t* mask,
    uint8_t mask_bits) {
    if(mask_bits > 64U || (mask_bits && !mask)) return false;
    for(uint8_t bit = 0; bit < mask_bits; ++bit) {
        const uint8_t mask_bit = (mask[bit / 8U] >> (bit % 8U)) & 1U;
        if(mask_bit != uid_wire_bit(internal_uid, bit)) return false;
    }
    return true;
}

uint8_t tonie_inventory_slot(const uint8_t internal_uid[8], uint8_t mask_bits) {
    uint8_t slot = 0;
    for(uint8_t i = 0; i < 4U; ++i) {
        const uint8_t bit = mask_bits + i;
        if(bit < 64U) slot |= uid_wire_bit(internal_uid, bit) << i;
    }
    return slot;
}

void tonie_session_init(TonieSession* session) {
    memset(session, 0, sizeof(*session));
    session->state = TonieStateReady;
    session->random_seed = 0x15693A5U;
}

void tonie_cancel_pending(TonieSession* session) {
    session->inventory_pending = false;
    session->inventory_responded = false;
    session->write_pending = false;
}

static void clear_volatile(TonieSession* session) {
    session->state = TonieStateReady;
    session->addressed = false;
    session->selected_request = false;
    session->inventory_pending = false;
    session->inventory_responded = false;
    session->inventory_slot = 0;
    session->inventory_eof_count = 0;
    session->inventory_mask_bits = 0;
    session->random = 0;
    memset(session->password_match, 0, sizeof(session->password_match));
    session->auth_failed = false;
    session->write_pending = false;
}

void tonie_field_on(TonieSession* session) {
    clear_volatile(session);
    session->field_present = true;
}

void tonie_field_off(TonieSession* session) {
    clear_volatile(session);
    session->field_present = false;
}

static bool uid_matches_request(const TonieTag* tag, const uint8_t* wire_uid) {
    uint8_t expected[8];
    tonie_uid_to_wire(tag->uid, expected);
    return memcmp(expected, wire_uid, sizeof(expected)) == 0;
}

static bool append(TonieProtocolResult* r, const void* data, size_t size) {
    if(size > sizeof(r->response) - r->response_len) return false;
    memcpy(&r->response[r->response_len], data, size);
    r->response_len += size;
    return true;
}

static TonieProtocolResult inventory(
    TonieSession* session,
    const TonieTag* tag,
    uint8_t flags,
    const uint8_t* data,
    size_t len) {
    TonieProtocolResult r = result_new(0x01);
    session->inventory_pending = false;
    session->inventory_responded = false;
    session->inventory_eof_count = 0;
    if(session->state == TonieStateQuiet || tag->privacy_persistent)
        return ignored(0x01, TonieResultIgnored);

    size_t pos = 0;
    if(flags & FLAG_AFI) {
        if(len < 1) return ignored(0x01, TonieResultMalformed);
        const uint8_t afi = data[pos++];
        if(afi && afi != tag->afi) return ignored(0x01, TonieResultAfiMismatch);
    }
    if(pos >= len) return ignored(0x01, TonieResultMalformed);
    const uint8_t mask_bits = data[pos++];
    if(mask_bits > ((flags & FLAG_ONE_SLOT) ? 64U : 60U))
        return ignored(0x01, TonieResultMalformed);
    const size_t mask_bytes = ((size_t)mask_bits + 7U) / 8U;
    if(len - pos != mask_bytes) return ignored(0x01, TonieResultMalformed);
    r.mask_match = tonie_inventory_mask_matches(tag->uid, &data[pos], mask_bits);
    if(!r.mask_match) return ignored(0x01, TonieResultUidMismatch);

    uint8_t wire[8];
    tonie_uid_to_wire(tag->uid, wire);
    response_begin(&r);
    append(&r, &tag->dsfid, 1);
    append(&r, wire, sizeof(wire));
    if(flags & FLAG_ONE_SLOT) return r;

    session->inventory_mask_bits = mask_bits;
    session->inventory_slot = tonie_inventory_slot(tag->uid, mask_bits);
    r.inventory_slot = session->inventory_slot;
    if(session->inventory_slot == 0) {
        session->inventory_responded = true;
        return r;
    }
    session->inventory_pending = true;
    r.action = TonieActionWaitForInventorySlot;
    r.response_len = 0;
    return r;
}

static bool request_mode_allowed(
    TonieSession* session,
    const TonieTag* tag,
    uint8_t flags,
    uint8_t command,
    const uint8_t** data,
    size_t* len,
    TonieResultCode* reason) {
    const bool selected = flags & FLAG_SELECTED;
    const bool addressed = flags & FLAG_ADDRESSED;
    session->selected_request = selected;
    session->addressed = addressed;
    if(selected && addressed) {
        *reason = TonieResultMalformed;
        return false;
    }
    if(addressed) {
        if(*len < 8U) {
            *reason = TonieResultMalformed;
            return false;
        }
        const bool match = uid_matches_request(tag, *data);
        *data += 8U;
        *len -= 8U;
        if(!match) {
            *reason = TonieResultUidMismatch;
            return false;
        }
        return true; /* Addressed commands wake/operate a QUIET tag as defined by ISO 15693. */
    }
    /* The Toniebox sends broadcast RESET_TO_READY after STAY_QUIET (captured
     * on-device). Accept that explicit reset without waking for ordinary polls. */
    if(session->state == TonieStateQuiet && !(command == 0x26 && !selected)) {
        *reason = TonieResultIgnored;
        return false;
    }
    if(selected && session->state != TonieStateSelected) {
        *reason = TonieResultIgnored;
        return false;
    }
    return true;
}

static bool block_protected(const TonieSession* session, const TonieTag* tag,
                            uint16_t block, bool write) {
    if(!tag->slix || block == 79U) return false;
    const uint8_t bit = (block >= tag->protection_pointer ? 4U : 0U) + (write ? 1U : 0U);
    return (tag->protection_condition & (1U << bit)) && !session->password_match[write ? 1 : 0];
}

static TonieProtocolResult read_single(
    uint8_t command,
    const TonieSession* session,
    const TonieTag* tag,
    uint8_t flags,
    const uint8_t* data,
    size_t len) {
    if(len != 1U) return error_response(command, 0x02, TonieResultMalformed);
    const uint16_t block = data[0];
    if(block >= tag->block_count) return error_response(command, 0x10, TonieResultRange);
    if(block_protected(session, tag, block, false))
        return error_response(command, 0x0F, TonieResultAuth);
    TonieProtocolResult r = result_new(command);
    response_begin(&r);
    if(flags & FLAG_OPTION) append(&r, &tag->security[block], 1);
    append(&r, &tag->blocks[(size_t)block * tag->block_size], tag->block_size);
    return r;
}

static TonieProtocolResult read_multiple(
    uint8_t command,
    const TonieSession* session,
    const TonieTag* tag,
    uint8_t flags,
    const uint8_t* data,
    size_t len) {
    if(len != 2U) return error_response(command, 0x02, TonieResultMalformed);
    const uint16_t first = data[0];
    const uint16_t count = (uint16_t)data[1] + 1U;
    if(first >= tag->block_count || first + count > tag->block_count)
        return error_response(command, 0x10, TonieResultRange);
    for(uint16_t block = first; block < first + count; ++block)
        if(block_protected(session, tag, block, false))
            return error_response(command, 0x0F, TonieResultAuth);
    const size_t bytes = count * ((size_t)tag->block_size + ((flags & FLAG_OPTION) ? 1U : 0U));
    if(bytes + 1U > TONIE_MAX_RESPONSE)
        return error_response(command, 0x0F, TonieResultRange);
    TonieProtocolResult r = result_new(command);
    response_begin(&r);
    for(uint16_t block = first; block < first + count; ++block) {
        if(flags & FLAG_OPTION) append(&r, &tag->security[block], 1);
        append(&r, &tag->blocks[(size_t)block * tag->block_size], tag->block_size);
    }
    return r;
}

static TonieProtocolResult system_info(uint8_t command, const TonieTag* tag, size_t len) {
    if(len) return error_response(command, 0x02, TonieResultMalformed);
    TonieProtocolResult r = result_new(command);
    uint8_t wire[8];
    const uint8_t sys_flags = tag->system_info_flags & 0x0FU;
    const uint8_t memory[2] = {(uint8_t)(tag->block_count - 1U), (uint8_t)(tag->block_size - 1U)};
    tonie_uid_to_wire(tag->uid, wire);
    response_begin(&r);
    append(&r, &sys_flags, 1);
    append(&r, wire, sizeof(wire));
    if(sys_flags & 1U) append(&r, &tag->dsfid, 1);
    if(sys_flags & 2U) append(&r, &tag->afi, 1);
    if(sys_flags & 4U) append(&r, memory, sizeof(memory));
    if(sys_flags & 8U) append(&r, &tag->ic_ref, 1);
    return r;
}

static uint32_t load_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static uint32_t reverse_u32(uint32_t x) {
    return ((x & 0xffU) << 24U) | ((x & 0xff00U) << 8U) | ((x >> 8U) & 0xff00U) |
           ((x >> 24U) & 0xffU);
}

static int password_type(uint8_t id) {
    for(int i = 0; i < 5; ++i)
        if(id == (uint8_t)(1U << i)) return i;
    return -1;
}

static TonieProtocolResult slix_command(
    TonieSession* session,
    TonieTag* tag,
    uint8_t command,
    const uint8_t* data,
    size_t len) {
    if(!tag->slix) return ignored(command, TonieResultUnsupported);
    TonieProtocolResult r = result_new(command);
    response_begin(&r);
    switch(command) {
    case 0xAB: {
        if(len) return error_response(command, 0x02, TonieResultMalformed);
        const uint8_t info[7] = {tag->protection_pointer, tag->protection_condition,
                                 tag->lock_bits, 0x7F, 0x35, 0, 0};
        append(&r, info, sizeof(info));
        return r;
    }
    case 0xB2:
        if(len) return error_response(command, 0x02, TonieResultMalformed);
        session->random_seed = session->random_seed * 1664525U + 1013904223U;
        session->random = (uint16_t)(session->random_seed >> 8U);
        r.response[r.response_len++] = (uint8_t)session->random;
        r.response[r.response_len++] = (uint8_t)(session->random >> 8U);
        return r;
    case 0xB3: {
        if(len != 5U) return error_response(command, 0x02, TonieResultMalformed);
        const int type = password_type(data[0]);
        if(type < 0) return error_response(command, 0x0F, TonieResultAuth);
        if(!(tag->password_mask & data[0])) return ignored(command, TonieResultUnsupported);
        const uint32_t xored = load_u32(&data[1]);
        const uint32_t repeated = (uint32_t)session->random | ((uint32_t)session->random << 16U);
        const uint32_t password = reverse_u32(xored ^ repeated);
        if(tag->accept_all_passwords || password == tag->passwords[type]) {
            session->password_match[type] = true;
            if(type == 2) tag->privacy_persistent = false;
            return r;
        }
        session->password_match[type] = false;
        session->auth_failed = true;
        return ignored(command, TonieResultAuth);
    }
    case 0xB4: {
        if(len != 5U) return error_response(command, 0x02, TonieResultMalformed);
        const int type = password_type(data[0]);
        if(type >= 0 && !(tag->password_mask & data[0]))
            return ignored(command, TonieResultUnsupported);
        if(type < 0 || !session->password_match[type])
            return error_response(command, 0x0F, TonieResultAuth);
        if(tag->password_lock_mask & data[0])
            return error_response(command, 0x0F, TonieResultAuth);
        tag->passwords[type] = load_u32(&data[1]);
        session->password_match[type] = false;
        return r;
    }
    case 0xB5: {
        /* SLIX-L: LOCK PASSWORD; B5 has different semantics on SLIX2. */
        if(tag->uid[2] != 0x03) return ignored(command, TonieResultUnsupported);
        if(len != 1U) return ignored(command, TonieResultMalformed);
        const int type = password_type(data[0]);
        if(type < 0 || !(tag->password_mask & data[0]) || !session->password_match[type])
            return error_response(command, 0x0F, TonieResultAuth);
        tag->password_lock_mask |= data[0];
        return r;
    }
    case 0xB6:
        if(len != 2U) return error_response(command, 0x02, TonieResultMalformed);
        if((tag->lock_bits & 8U) || data[0] >= 79U ||
           !session->password_match[0] || !session->password_match[1])
            return error_response(command, 0x0F, TonieResultAuth);
        tag->protection_pointer = data[0];
        tag->protection_condition = data[1];
        return r;
    case 0xBA: {
        if(len != 4U) return error_response(command, 0x02, TonieResultMalformed);
        const uint32_t repeated = (uint32_t)session->random | ((uint32_t)session->random << 16U);
        const uint32_t password = reverse_u32(load_u32(data) ^ repeated);
        if(!tag->accept_all_passwords && password != tag->passwords[2]) {
            session->auth_failed = true;
            return ignored(command, TonieResultAuth);
        }
        session->password_match[2] = true;
        tag->privacy_persistent = true;
        return r;
    }
    case 0xBD:
        if(len) return error_response(command, 0x02, TonieResultMalformed);
        append(&r, tag->signature, sizeof(tag->signature));
        return r;
    default:
        return ignored(command, TonieResultUnsupported);
    }
}

static TonieProtocolResult defer_write_reply(
    TonieSession* session, TonieProtocolResult r, uint8_t flags) {
    if((flags & FLAG_OPTION) && r.action == TonieActionRespondNow) {
        session->write_pending = true;
        session->pending_command = r.command;
        session->pending_response_len = r.response_len;
        memcpy(session->pending_response, r.response, r.response_len);
        r.action = TonieActionWaitForEof;
        r.response_len = 0;
    }
    return r;
}

/* Writes modify only the emulation copy. Option-flag replies wait for reader EOF,
 * including errors, as in the firmware listener. Never forge the dump's lock bits. */
static TonieProtocolResult write_command(
    TonieSession* session, TonieTag* tag, uint8_t command, uint8_t flags,
    const uint8_t* data, size_t len) {
    TonieProtocolResult r = result_new(command);
    response_begin(&r);
    const bool block = command == 0x21 || command == 0x22;
    const bool write = command == 0x21 || command == 0x27 || command == 0x29;
    const size_t expected = block ? (command == 0x21 ? 1U + tag->block_size : 1U) :
                                   (write ? 1U : 0U);
    if(len != expected) return ignored(command, TonieResultMalformed);
    if(block) {
        if(data[0] >= tag->block_count) r = error_response(command, 0x10, TonieResultRange);
        else if(tag->security[data[0]] & 1U)
            r = error_response(command, command == 0x22 ? 0x11 : 0x12, TonieResultAuth);
        else if(block_protected(session, tag, data[0], false) ||
                block_protected(session, tag, data[0], true))
            r = error_response(command, 0x0F, TonieResultAuth);
        else if(command == 0x21)
            memcpy(&tag->blocks[(size_t)data[0] * tag->block_size], data + 1, tag->block_size);
        else tag->security[data[0]] = 1;
    } else {
        const uint8_t lock = (command == 0x27 || command == 0x28) ? 1U : 4U;
        if(tag->lock_bits & lock) r = error_response(command, 0x12, TonieResultAuth);
        else if(lock == 1U && tag->slix && !session->password_match[4])
            r = error_response(command, 0x0F, TonieResultAuth);
        else if(!write) tag->lock_bits |= lock;
        else if(lock == 1U) tag->afi = data[0];
        else tag->dsfid = data[0];
    }
    return defer_write_reply(session, r, flags);
}

TonieProtocolResult tonie_handle_frame(
    TonieSession* session,
    TonieTag* tag,
    const uint8_t* request,
    size_t request_len) {
    if(session) tonie_cancel_pending(session);
    if(!session || !tag || !request || request_len < 2U)
        return ignored(0, TonieResultMalformed);
    const uint8_t flags = request[0];
    const uint8_t command = request[1];
    const uint8_t* data = &request[2];
    size_t len = request_len - 2U;

    if(!tag->block_count || tag->block_count > TONIE_MAX_BLOCKS || !tag->block_size ||
       tag->block_size > TONIE_MAX_BLOCK_SIZE || (flags & 0x88U))
        return ignored(command, TonieResultMalformed);
    if(session->auth_failed) return ignored(command, TonieResultAuth);
    if(tag->slix && tag->privacy_persistent && command != 0xB2 && command != 0xB3)
        return ignored(command, TonieResultIgnored);
    if(flags & FLAG_INVENTORY) {
        if(command != 0x01) return ignored(command, TonieResultMalformed);
        return inventory(session, tag, flags, data, len);
    }
    if(command == 0x01) return ignored(command, TonieResultMalformed);
    /* Manufacturer precedes the optional UID in custom commands. */
    if(command >= 0xA0U) {
        if(!len) return ignored(command, TonieResultMalformed);
        if(*data++ != NXP_MANUFACTURER) return ignored(command, TonieResultIgnored);
        --len;
    }
    TonieResultCode reason = TonieResultIgnored;
    if(!request_mode_allowed(session, tag, flags, command, &data, &len, &reason)) {
        if(command == 0x25 && reason == TonieResultUidMismatch &&
           session->state == TonieStateSelected) session->state = TonieStateReady;
        return ignored(command, reason);
    }

    switch(command) {
    case 0x02:
        if(len || !(flags & FLAG_ADDRESSED))
            return error_response(command, 0x02, TonieResultMalformed);
        session->state = TonieStateQuiet;
        return ignored(command, TonieResultOk);
    case 0x20:
        return read_single(command, session, tag, flags, data, len);
    case 0x21:
    case 0x22:
    case 0x27:
    case 0x28:
    case 0x29:
    case 0x2A:
        return write_command(session, tag, command, flags, data, len);
    case 0x23:
        return read_multiple(command, session, tag, flags, data, len);
    case 0x25:
        if(len || !(flags & FLAG_ADDRESSED))
            return error_response(command, 0x02, TonieResultMalformed);
        session->state = TonieStateSelected;
        {
            TonieProtocolResult r = result_new(command);
            response_begin(&r);
            return r;
        }
    case 0x26: {
        if(len) return error_response(command, 0x02, TonieResultMalformed);
        session->state = TonieStateReady;
        TonieProtocolResult r = result_new(command);
        response_begin(&r);
        return r;
    }
    case 0x2B:
        return system_info(command, tag, len);
    case 0x2C: {
        if(len != 2U) return error_response(command, 0x02, TonieResultMalformed);
        const uint16_t first = data[0];
        const uint16_t count = (uint16_t)data[1] + 1U;
        if(first + count > tag->block_count)
            return error_response(command, 0x10, TonieResultRange);
        if(count + 1U > TONIE_MAX_RESPONSE)
            return error_response(command, 0x0F, TonieResultRange);
        TonieProtocolResult r = result_new(command);
        response_begin(&r);
        append(&r, &tag->security[first], count);
        return r;
    }
    default:
        if(command >= 0xA0U) {
            if(tag->slix && tag->uid[2] == 0x03 &&
               (command == 0xB4 || (command == 0xB3 && len && data[0] != 4)) &&
               !(flags & (FLAG_ADDRESSED | FLAG_SELECTED)))
                return ignored(command, TonieResultMalformed);
            TonieProtocolResult r = slix_command(session, tag, command, data, len);
            if(command == 0xB4 || command == 0xB5) return defer_write_reply(session, r, flags);
            return r;
        }
        return ignored(command, TonieResultUnsupported);
    }
}

TonieProtocolResult tonie_handle_eof(TonieSession* session, const TonieTag* tag) {
    TonieProtocolResult r = result_new(0x01);
    if(session && session->write_pending) {
        r = result_new(session->pending_command);
        response_begin(&r);
        memcpy(r.response, session->pending_response, session->pending_response_len);
        r.response_len = session->pending_response_len;
        session->write_pending = false;
        return r;
    }
    if(!session || !tag || !session->inventory_pending || session->inventory_responded)
        return ignored(0x01, TonieResultIgnored);
    if(session->inventory_eof_count < 15U) ++session->inventory_eof_count;
    r.inventory_slot = session->inventory_slot;
    if(session->inventory_eof_count != session->inventory_slot)
        return ignored(0x01, TonieResultOk);
    uint8_t wire[8];
    tonie_uid_to_wire(tag->uid, wire);
    response_begin(&r);
    append(&r, &tag->dsfid, 1);
    append(&r, wire, sizeof(wire));
    session->inventory_responded = true;
    session->inventory_pending = false;
    return r;
}

const char* tonie_state_name(TonieState state) {
    switch(state) {
    case TonieStateReady: return "READY";
    case TonieStateSelected: return "SELECTED";
    case TonieStateQuiet: return "QUIET";
    default: return "INVALID";
    }
}

const char* tonie_command_name(uint8_t command) {
    switch(command) {
    case 0x01: return "INVENTORY";
    case 0x02: return "STAY_QUIET";
    case 0x20: return "READ_SINGLE";
    case 0x21: return "WRITE_SINGLE";
    case 0x22: return "LOCK_BLOCK";
    case 0x27: return "WRITE_AFI";
    case 0x28: return "LOCK_AFI";
    case 0x29: return "WRITE_DSFID";
    case 0x2A: return "LOCK_DSFID";
    case 0x23: return "READ_MULTIPLE";
    case 0x25: return "SELECT";
    case 0x26: return "RESET_READY";
    case 0x2B: return "SYSTEM_INFO";
    case 0x2C: return "BLOCK_SECURITY";
    case 0xAB: return "NXP_SYSTEM_INFO";
    case 0xB2: return "GET_RANDOM";
    case 0xB3: return "SET_PASSWORD";
    case 0xB4: return "WRITE_PASSWORD";
    case 0xB5: return "LOCK_PASSWORD_SLIX_L";
    case 0xB6: return "PROTECT_PAGE";
    case 0xBA: return "ENABLE_PRIVACY";
    case 0xBD: return "READ_SIGNATURE";
    default: return "UNKNOWN";
    }
}
