# Flipper Tonie Emulator

![Launcher icon](tonie_10px.png)

Independent, GPL-3.0-licensed project. Not affiliated with tonies.

**[Download latest FAP](https://github.com/kamilio/flipper-zero-tonies-emulator/releases/latest/download/tonie_emulator.fap)** · [All releases](https://github.com/kamilio/flipper-zero-tonies-emulator/releases)

Select a saved Tonie to start emulating immediately. **Back** returns to files.
The screen shows the Tonie name and Back control. Errors appear only if emulation cannot start.
A steady dim blue LED indicates active emulation and clears when returning to files.

## Install

Download `tonie_emulator.fap` from this repository’s GitHub Releases and copy it to `/ext/apps/NFC/tonie_emulator.fap`, then open
**Apps → NFC → Tonie Emulator**. Uses Momentum FAP API 87.1; tested on firmware
`d3f89dfe`. A saved `/ext/.../*.nfc` filename can also be passed as a launch argument.

See [what differs from the built-in emulator](docs/differences.md) for the exact
behavior changes and limitations.

## Cutoff fix

The recorded failure was a tag stuck in QUIET after a missing RESET_TO_READY.
It still answered password commands but stopped answering inventory, so the box
lost the tag. Adding the real figurine allowed the box to read it and send a reset.
No lock commands occurred in that capture.

The app embeds the installed firmware's ISO15693/SLIX listener code, with a narrow
Toniebox recovery: after successful privacy authentication while QUIET, a fresh
unmasked single-slot inventory restores READY. This is a compatibility workaround,
not standard ISO15693 behavior or a detected RF field reset. Failed authentication,
masked searches and EOF do not trigger recovery. Saved NFC files are unchanged.

**Physically verified:** approximately five minutes (299.293 seconds) of continuous
playback with the original figurine removed. The missing-reset sequence occurred
six times; each recovery restored a successful inventory response. The recording
contained 28,777 receives, 22,619 transmits, no transmit errors, no dropped records,
and no SD errors. Longer runs and other tags/firmware remain separate test cases.

## Logs

Logs are on the Flipper at `/ext/apps_data/tonie_emulator/logs/`.
RX frames include CRC; TX records contain actual transmit results. Recovery events
are marked `QUIET_RECOVERY`. A separate thread formats and writes bounded trace
records; the footer reports dropped records and SD errors.

Stop emulation with Back before retrieving the active log over USB; reading the
same file while it is open for writing can block the storage command.

## Development

Run `make check` for synthetic recovery and logger tests (with address/undefined
behavior sanitizers). CI runs these tests, builds the FAP, and checks SDK API
compatibility. Private captures and local replay tools are excluded.

`src/native/` embeds listener sources from Momentum `d3f89dfe` (GPL-3.0; license
and provenance in `src/native/vendor/`). `quiet_recovery.h` implements the tested
compatibility policy. The separate portable core in `src/protocol/` remains for
protocol research and host testing; its features are not all part of the active
firmware-based playback engine.

To build, place this directory in `applications_user/tonie_emulator` in Momentum
`mntm-012` and run `./fbt fap_tonie_emulator`. The build checks API symbol compatibility.

The optional [firmware patch](firmware/README.md) contains selected changes from
[Momentum PR #566](https://github.com/Next-Flip/Momentum-Firmware/pull/566).
It has not been flashed onto the test device and is not needed for this recovery.
The stock HAL still limits reply rates and does not report physical field resets.

See [research and device evidence](docs/research.md) for details. See [CONTRIBUTING.md](CONTRIBUTING.md) for builds and
[the publishing guide](docs/publishing.md) for releases.
