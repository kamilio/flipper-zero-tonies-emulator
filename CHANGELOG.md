# Changelog

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
