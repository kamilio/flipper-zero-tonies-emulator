# Differences from Momentum's built-in emulator

The active engine starts from Momentum revision
`d3f89dfe2ef6b01839201598e9be1590cba80322`. It embeds that revision's ISO15693
and SLIX listeners inside the FAP so changes can ship as an application update.
NFC hardware control, modulation, CRC helpers, file parsing and system services
still come from the installed firmware. This is not a full firmware replacement.

## Behavior comparison

| Area | Upstream listener | This application |
| --- | --- | --- |
| Missing reset after STAY_QUIET | Can remain QUIET and ignore subsequent inventory polls | Recovers on a fresh unmasked single-slot inventory after successful privacy authentication while QUIET |
| RF field-loss detection | Transparent-mode HAL does not expose physical field reset events | Same limitation; the recovery does not claim to detect field loss |
| Protocol implementation | Installed firmware listener | Embedded copy of the same listener, with the recovery implemented by the wrapper |
| Diagnostics | Public callback does not expose every exchange | Records raw RX, actual TX results, worker commands and recovery events |
| Logging workload | No app-specific exchange recorder | Copies bounded binary records into a queue; a separate thread formats and writes them |
| User flow | Firmware NFC application flow | Selecting a file starts emulation; Back returns to files |
| Main screen | Firmware NFC interface | Tonie name and Back control; no redundant Emulating label or RF counters |
| Indicator | Firmware/application notification behavior | Steady dim blue LED during emulation; cleared when returning to files |
| Saved dump | Firmware-dependent application handling | Loaded data is copied; emulated changes are not saved to the source file |

## Exact scope of the recovery

The recovery requires all of the following:

1. The embedded SLIX listener is in QUIET.
2. It has successfully validated the broadcast privacy-password command in that quiet session.
3. The next recovery candidate is a CRC-valid, unmasked, single-slot inventory
   with payload `26 01 00` (excluding transport CRC).

The wrapper changes the listener to READY before processing that inventory and
records `QUIET_RECOVERY`. A new quiet command, an explicit reset or failed
privacy authentication invalidates the tracked authentication condition.
Masked inventory, multislot inventory and empty EOF frames do not trigger it.

This deliberately differs from strict ISO15693 quiet-state behavior. It is a
Toniebox compatibility workaround for the captured missing-reset sequence,
not a general improvement to anticollision behavior for arbitrary NFC readers.
It does not force lock bits to locked, bypass password validation, or use a timer
to impersonate RF field loss.

## Separate experimental protocol core

`src/protocol/` retains the earlier standalone parser for development. Its protocol
fixes and synthetic coverage do not all apply to the active embedded listener.
There is no mode switch or experimental timing control in the public UI.
Only two synthetic test suites are published; private captures and local replay
tools are excluded.

## Evidence and limits

The observed cutoff sequence was exercised six times during a 299.293-second
physical playback test without the original figurine; each recovery restored a
successful inventory reply. Playback continued, with no transmit errors or lost
log records. See [research.md](research.md) for aggregate results.

This validates the observed failure on the tested setup. It does not establish
universal compatibility, indefinite playback, or correct physical field-reset
handling.
