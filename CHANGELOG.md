# Changelog

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
