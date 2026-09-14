# Upstream sources

Source: [Next-Flip/Momentum-Firmware](https://github.com/Next-Flip/Momentum-Firmware)

Revision: `d3f89dfe2ef6b01839201598e9be1590cba80322`

License: GNU GPL version 3; see LICENSE here and at the repository root.

The four listener implementation files and two private headers come from
`lib/nfc/protocols/iso15693_3/` and `lib/nfc/protocols/slix/`. Include paths are
adjusted for embedding; `.c` files use `.inc` so the app wrapper can intercept TX.
`data_helpers.inc` contains ten unexported helper functions copied from the same
revision; each function identifies its original filename.

Local hardening: both mandatory-command table lookups check the lower bound,
so reserved command `0x00` cannot index before the handler table. Memory handlers
validate start/count and exact payload sizes before indexing; multi-block writes
index payloads relative to the requested first block. Read/security replies are
bounded to the TX buffer with CRC space reserved. AFI/DSFID writes require their
exact one-byte payload. SLIX counter writes read byte-aligned payloads without a
32-bit pointer cast and reject incompatible block sizes. Other vendor behavior
retains the baseline. The surrounding
`tonie_native.c` adds bounded RX/TX trace records and invokes the Toniebox-specific
policy in `quiet_recovery.h`. Formatting and SD writes run on the logger thread.
