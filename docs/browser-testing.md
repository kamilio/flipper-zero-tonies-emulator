# Browser and batch reader validation

Run `make check` for reader, writer and NFC protocol regressions. Run `make check-browser` for the embedded browser and FAT date parser. Browser tests require clang and Flipper mlib headers; set `FLIPPER_SDK_HEADERS` to a Momentum checkout or exported SDK root containing `lib/mlib`.

Both suites compile production C with AddressSanitizer and UndefinedBehaviorSanitizer. Test fixtures are synthetic; no personal NFC dumps or device captures are included.

## Automated coverage

- Browser: 141 scopes, all eight regular/flat and ordering combinations, 80,386 page-entry comparisons, 606 page transitions, 200 rapid switches, and 10,000-file stress.
- Empty folders, Unicode/long/duplicate names, corrupted caches, missing anchors, storage errors, low memory, cancellation and scanner teardown.
- Single-option directory action, NFC filtering, modal return values, scoped navigation, 700 ms spinner threshold and no date reads for alphabetical ordering.
- FAT12/16/32 parsing, Unicode LFN, invalid metadata, I/O faults and 1,500 metadata mutations.
- Batch reader: 1,000 unattended scan/save/repeat cycles, sixteen-entry history/eviction, full data/metadata duplicate comparison, changed same-UID scans, and unchecked/verified variants.
- Changes to every data/metadata byte, deleted or modified prior files, automatic filename suffixes, collision races, staging-file ownership, SD/save/load/verify/rename failures and retained retry.
- Exact retry timing across 32-bit tick wrap and cancellation during save retry or callback completion.
- Independent readback: malformed replies, per-byte corruption, addressed UID binding, RF loss and all supported block geometries.

## Device checks and limits

The FAP builds for Momentum API 87.1. Device checks covered the regular browser and sort pill, populated/empty-folder Read / Unlock menu, reader entry/Back, invalid-file recovery, valid-file emulation, a 50-entry page boundary, scoped flat mode and all four orderings. The full emulation title scrolls without the previous 95-byte limit.

Broad recursive scans remain slow; regular and folder-scoped browsing avoid them. The browser holds bounded pages; peak tracked browser allocations in the host suite were 48,207 bytes, with zero retained allocations. This is not total firmware heap usage.

A physical single-chip batch test saved two different unchecked snapshots and a verified snapshot in the invoking test directory. On-device hashes confirmed that the unchecked snapshots differed; the verified snapshot matched one earlier unchecked snapshot and was retained separately as intended. The reader displayed “Duplicate skipped” and the saved-file count stayed unchanged during repeated checks with the same chip. No NFC payloads were exported for this verification.

A second physical chip saved a verified dump automatically. After the swap back to the first chip, the reader displayed “Duplicate skipped — Saved: 4”; the folder contained exactly two verified chip dumps and the two earlier unchecked variants. Thus the live sequence exercised automatic continuation and recent-history matching across different chips. LED/vibration sequences are implemented and covered by the save-success path; their physical appearance was not captured by the screen-stream checks. Automated fault tests do not establish every RF failure mode or guarantee absence of every crash. Green feedback confirms verified file publication; `_unchecked` scans still require review.
