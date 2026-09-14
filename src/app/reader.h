#pragma once
#include <gui/gui.h>
#include <nfc/nfc.h>
#include <storage/storage.h>
#include <notification/notification_messages.h>

/* Owns NFC until Back. Each complete read goes straight to the native name editor. */
void tonie_reader_run(Gui* gui, Nfc* nfc, Storage* storage, NotificationApp* notifications);
