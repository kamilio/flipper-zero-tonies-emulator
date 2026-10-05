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

**Browser:** opens `/ext/nfc` in regular A–Z folder view with Momentum-style rows and the sort pill. **Left** toggles flat view inside the current folder; **Right** cycles A–Z, Z–A, newest and oldest. Entering or leaving a folder resets regular A–Z view. Lists use 50-entry pages; a spinner appears only after 700 ms.

Hold **OK** in any folder (including an empty folder) for the single **Read / Unlock** action. Reads save into that browsed folder, including when flat view shows files from subfolders. **Back** from reading returns to the same folder with a fresh listing.

Long names on the emulation screen scroll smoothly, pausing at each end; short names stay centered.

**Emulation controls:** **Up** (top-right hint) writes the loaded file. **Down** opens
**More**, with **Clear** and **Read**. **Back/Left** returns or cancels.

**Clear** authenticates using the firmware's Tommybox privacy passwords, backs up
the detected chip under `/ext/apps_data/tonie_emulator/backups/`, reloads and checks
the backup, then asks for confirmation. It checks the chip again and zeroes its
data using addressed writes, preserving the UID and security settings. Permanently
locked blocks are refused. Interrupted erases may be partial; the backup remains.
**More → Read** and the browser's **Read / Unlock** action run continuous SLIX reading with Momentum's Tommybox privacy authentication. There is no naming keyboard or confirmation between chips. Files are named `SLIX_<UID>.nfc` in the invoking browser folder; numeric suffixes preserve existing files.

The LED stays blue while scanning. After a dump is saved and reloaded successfully, the Flipper vibrates and flashes green, then continues scanning automatically. The display shows the saved count. **Back** exits to the invoking screen.

The last 16 saved scans are checked for duplicates: matching UID is only a lookup hint; the entire saved device's data and metadata must match before a scan is skipped. Changed data from the same chip is kept as another file. History stores filenames and loads one prior dump at a time, using less than 1 KB for the history itself. History resets when leaving Read / Unlock.

Reads still receive independent CRC-checked, UID-addressed block readback. Blank, incomplete or inconsistent snapshots are preserved with `_unchecked` in their filenames and an on-screen notice. A later verified version is kept separately even if its data matches an unchecked version. Green confirms that the dump was saved, not that an unchecked dump is valid. Authentication does not erase data or remove permanent lock bits.

SD/save failures retain the snapshot and retry once per second automatically; further chips wait until saving succeeds. Success feedback and duplicate history update only after verified publication. Back exits and discards any pending unsaved snapshot.

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
