#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TONIE_MAX_BLOCKS 256U
#define TONIE_MAX_BLOCK_SIZE 32U
#define TONIE_MAX_RESPONSE 253U /* HAL sequence limit: 255 bytes including CRC. */

typedef enum { TonieStateReady, TonieStateSelected, TonieStateQuiet } TonieState;
typedef enum {
    TonieActionNone,
    TonieActionRespondNow,
    TonieActionWaitForInventorySlot,
    TonieActionWaitForEof,
} TonieAction;
typedef enum {
    TonieResultOk,
    TonieResultIgnored,
    TonieResultMalformed,
    TonieResultUnsupported,
    TonieResultUidMismatch,
    TonieResultAfiMismatch,
    TonieResultRange,
    TonieResultAuth,
} TonieResultCode;

typedef struct {
    /* Canonical form is the order printed in a Flipper .nfc UID field. */
    uint8_t uid[8];
    uint8_t dsfid;
    uint8_t afi;
    uint8_t ic_ref;
    uint8_t system_info_flags;
    uint16_t block_count;
    uint8_t block_size;
    uint8_t blocks[TONIE_MAX_BLOCKS * TONIE_MAX_BLOCK_SIZE];
    uint8_t security[TONIE_MAX_BLOCKS];
    uint8_t signature[32];
    uint8_t protection_pointer;
    uint8_t protection_condition;
    uint8_t lock_bits;
    uint32_t passwords[5];
    uint8_t password_mask;
    uint8_t password_lock_mask;
    bool slix;
    bool accept_all_passwords;
    bool privacy_persistent; /* EEPROM privacy state, retained across RF field cycles. */
} TonieTag;

typedef struct {
    TonieState state;
    bool addressed;
    bool selected_request;
    bool field_present;
    bool inventory_pending;
    bool inventory_responded;
    uint8_t inventory_slot;
    uint8_t inventory_eof_count;
    uint8_t inventory_mask_bits;
    uint16_t random;
    bool password_match[5];
    bool auth_failed; /* Wrong password: silent until the next RF field cycle. */
    bool write_pending;
    uint8_t pending_response[2];
    size_t pending_response_len;
    uint8_t pending_command;
    uint32_t random_seed;
} TonieSession;

typedef struct {
    TonieAction action;
    TonieResultCode code;
    uint8_t response[TONIE_MAX_RESPONSE];
    size_t response_len;
    uint8_t inventory_slot;
    uint8_t command;
    bool mask_match;
} TonieProtocolResult;

void tonie_uid_to_wire(const uint8_t internal_uid[8], uint8_t wire_uid[8]);
void tonie_uid_from_wire(const uint8_t wire_uid[8], uint8_t internal_uid[8]);
bool tonie_inventory_mask_matches(
    const uint8_t internal_uid[8],
    const uint8_t* mask,
    uint8_t mask_bits);
uint8_t tonie_inventory_slot(const uint8_t internal_uid[8], uint8_t mask_bits);
void tonie_session_init(TonieSession* session);
void tonie_cancel_pending(TonieSession* session);
void tonie_field_on(TonieSession* session);
void tonie_field_off(TonieSession* session);
TonieProtocolResult tonie_handle_frame(
    TonieSession* session,
    TonieTag* tag,
    const uint8_t* request,
    size_t request_len);
TonieProtocolResult tonie_handle_eof(TonieSession* session, const TonieTag* tag);
const char* tonie_state_name(TonieState state);
const char* tonie_command_name(uint8_t command);
