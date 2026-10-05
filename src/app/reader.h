#pragma once
#include <gui/gui.h>
#include <nfc/nfc.h>
#include <storage/storage.h>
#include <notification/notification_messages.h>

/* Saves in the supplied directory (copied on entry). Owns NFC until Back. Automatically names and saves reads; identical recent dumps are skipped. */
void tonie_reader_run(Gui* gui, Nfc* nfc, Storage* storage, NotificationApp* notifications, const char* directory);
