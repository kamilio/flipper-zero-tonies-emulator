#pragma once
#include <toolbox/bit_buffer.h>
typedef struct Iso15693_3Poller Iso15693_3Poller;
typedef enum { Iso15693_3ErrorNone, Iso15693_3ErrorTimeout } Iso15693_3Error;
Iso15693_3Error iso15693_3_poller_send_frame(Iso15693_3Poller* poller, const BitBuffer* tx, BitBuffer* rx, uint32_t fwt);
