#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Toniebox compatibility: the transparent HAL cannot report RF power resets.
 * Recover only after an authenticated privacy exchange followed by a fresh,
 * unmasked single-slot inventory. Do not reinterpret inventory slot EOFs,
 * masked searches, or unauthenticated polls as a reset. */
typedef struct { bool authenticated; } TonieQuietRecovery;
static inline bool tonie_quiet_recover(
    TonieQuietRecovery* recovery, const uint8_t* frame, size_t size, bool quiet) {
    if(quiet && recovery->authenticated && size==3 &&
       frame[0]==0x26 && frame[1]==0x01 && frame[2]==0x00) {
        recovery->authenticated=false;
        return true;
    }
    return false;
}
static inline void tonie_quiet_observe(
    TonieQuietRecovery* recovery, const uint8_t* frame, size_t size,
    bool quiet, bool privacy_password_matched) {
    if(!quiet || (size>=2 && (frame[1]==0x02 || frame[1]==0x26)))
        recovery->authenticated=false;
    if(quiet && size==8 && frame[0]==0x02 && frame[1]==0xB3 && frame[2]==0x04 && frame[3]==0x04)
        recovery->authenticated=privacy_password_matched;
}
