# Tonie Emulator for Flipper Zero

Fixes the Toniebox 2 playback cutoff after roughly 90 seconds when emulating
a Tonie with Flipper Zero.

**[Download FAP](https://github.com/kamilio/flipper-zero-tonies-emulator/releases/latest/download/tonie_emulator.fap)**

Copy to `/ext/apps/NFC/`. Requires Momentum FAP API 87.1.

**Dev install:** `make install` builds the FAP, uploads it to the connected
Flipper over USB and starts it. It auto-detects a Momentum checkout that has
this repo linked as `applications_user/tonie_emulator` (checks `../momentum`
and `../../toy-blocks/.tools/momentum`; override with `SDK_DIR=...`). Close
the app on the Flipper before reinstalling, or use `make build` and copy the
FAP manually.

**Controls:** **Up** (top-right hint) writes the loaded file. **Down** opens
**More**, with **Clear** and **Read**. **Back/Left** returns or cancels.

**Clear** authenticates using the firmware's Tommybox privacy passwords, backs up
the detected chip under `/ext/apps_data/tonie_emulator/backups/`, reloads and checks
the backup, then asks for confirmation. It checks the chip again and zeroes its
data using addressed writes, preserving the UID and security settings. Permanently
locked blocks are refused. Interrupted erases may be partial; the backup remains.
**More → Read** starts continuous SLIX reading with Momentum's Tommybox privacy authentication.
Before naming, the app checks data length and reads every block back with CRC-checked,
UID-addressed commands. Missing, blank (all-zero/all-FF), or inconsistent data shows
**Possibly corrupted** with **Skip** and **Save anyway**. Skip resumes scanning;
**OK / Save anyway** opens the name keyboard and resumes scanning after the save.
This detects incomplete/unstable reads, not every invalid Tonie token: consistently
wrong non-blank data can still pass. Each verified read opens Momentum's **Name the card** keyboard. Naming is the only
save confirmation: the app verifies the saved dump under `/ext/nfc/`, then starts
reading the next chip immediately. Swap chips after saving; the previous UID is
ignored to prevent repeated prompts while that chip remains on the Flipper.
Existing names are refused, including a file created during the save. A save failure keeps the scan in RAM for retry.
**Back** in the name editor skips that scan; **Back** while reading exits.
Authentication does not erase data or remove permanent lock bits.

**Chip writing (experimental, unreleased):** uses
[SLI-Writer normal mode](https://github.com/Julienbxl/SLI-Writer/blob/59c8689d84922c5e55fda2c24ac9d5fc61a73de8/Flipper/sli_writer.c):
ISO15693 inventory and SELECT, non-addressed block writes (`0x02`, retry `0x42`),
then target UID commands `0x40`/`0x41`. It does not restore a factory UID or send
the layout command `0x47`. Existing chip UIDs are not filtered by manufacturer or
model, so previously written clones can be rewritten.

To write, with a file loaded, press **Up**
using the standard top-edge arrow,
and hold a rewritable magic SLI/SLIX chip to the Flipper.
The screen shows block progress; **Back/Left** cancels, and **OK** dismisses
the result to resume emulation. Success requires block readback and UID verification.
Lost UID acknowledgments are checked by inventory after attempting both halves.
Cancellation during the short UID update finishes both halves and verifies them
before returning. Keep only one chip at the Flipper and hold it still.
An interrupted write can leave the chip partially written. The app writes the
dump's blocks and sets the chip UID to the dump UID (Gen2 magic commands).
Loaded dumps are sanitized automatically: reported lock bits are cleared (they
would permanently burn clone blocks) and AcceptAllPasswords is set. Needs a
chip with a changeable UID; standard SLIX2 chips cannot take a Tonie UID.

[What’s different](docs/differences.md)
