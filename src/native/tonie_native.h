#pragma once
#include <nfc/nfc.h>
#include <nfc/nfc_device.h>
typedef struct TonieNative TonieNative;
typedef void (*TonieNativeTrace)(void* context, unsigned kind, const uint8_t* bytes, size_t size, unsigned result);
TonieNative* tonie_native_alloc(Nfc* nfc, const NfcDevice* device, TonieNativeTrace trace, void* context);
void tonie_native_start(TonieNative* native);
void tonie_native_stop(TonieNative* native);
void tonie_native_free(TonieNative* native);
