# Changelog

## Unreleased

- Fix emulated multi-block payload offsets, out-of-range block arithmetic,
  oversized security responses, and unaligned SLIX counter reads.
- Match SLI-Writer normal mode: direct ISO15693 poller, SELECT, non-addressed
  `0x02` writes with `0x42` fallback, then Gen2 UID commands. No factory-UID
  restore, UID-family filter or `0x47`. Recover lost acknowledgments by readback,
  finish both UID command attempts, and verify data and UID before success.
- Keep cancellation responsive under input floods and reject duplicate/busy CLI
  requests. Report potentially partial writes from the first attempted block.
- Keep the file-browser entry flow. Up at the top right is Write; Down opens a
  native More menu containing Clear and Read.
- Clear authenticates, saves and verifies a backup, requests confirmation,
  rechecks chip identity/content, and zeroes data while preserving UID and
  security settings. Refuse unsupported geometry and permanently locked data.
- Read continuously with Tommybox authentication and Momentum's native name
  keyboard. Verified saves return directly to scanning. Suppress the previous UID,
  prevent overwrites with safe rename, capture confirmed names against keyboard
  edits, and retain failed saves in RAM. Recreate pollers after read errors.
- Check read lengths, blank data and independent addressed block readback. Suspect
  reads show one informational warning: OK / Save anyway opens naming; left / Skip
  continues scanning without saving. Consistently incorrect tokens cannot be
  detected locally. Bound readback retries and support cancellation.
- Reduce log queue allocation by 12 KiB and reuse GUI scratch storage.

Validation: ASan/UBSan tests cover 65,536 listener block ranges, malformed payloads,
response capacities, counter alignment, 18 writer fault scenarios, 500 writer
reuse cycles, every 16-bit geometry count and response flag, response lengths,
small result buffers, SD failures, and 1,000 scan/save/swap/error cycles. Warning
button mappings and both continuations are tested. The app builds against Momentum
API 87.1. Earlier hardware checks covered emulation/start/stop, Clear cancellation,
a verified 8-block erase and restoration of the original chip data. Installation
and physical validation of the final reader changes await USB reconnection.

## 0.2.1

- Reject reserved command `0x00` before indexing either embedded NFC command table,
  preventing an out-of-bounds handler lookup.
- Test all 256 command values in both dispatchers under address and undefined
  behavior sanitizers; include the regression test in source releases.

Validation: the pre-fix dispatcher reproduced an out-of-bounds access under UBSan.
All host checks pass after the fix. This defect has not been linked to a captured
Toniebox exchange or the reported device crashes; patched hardware playback has
not yet been validated.

## 0.2.0

- Recover the captured Toniebox cutoff caused by a missing reset after QUIET.
- Start emulation immediately when selecting a file; Back returns to files.
- Record NFC exchanges and recovery events in the background.
- Remove the redundant Emulating label from the playback screen.
- Show a steady dim blue LED during emulation; clear it on return to files or exit.
- Add a monochrome launcher icon, app metadata, and license notices.

Validation: approximately five minutes of continuous playback without the
original figurine. Six recovery events each restored an inventory response;
no transmit errors, dropped records, or SD errors. See docs/research.md.
