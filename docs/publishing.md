# Publishing

The public repository is https://github.com/kamilio/flipper-zero-tonies-emulator.

Pushes and pull requests run synthetic tests and build against the pinned Momentum SDK.
A `vMAJOR.MINOR.PATCH` tag triggers the release workflow. The tag must match
`fap_version` in `application.fam`; mismatches fail before publication.

Before tagging:

1. Update the version and changelog.
2. Run `make check`, build the FAP, and run applicable physical checks.
3. Run `python3 tools/package_release.py --fap /path/to/tonie_emulator.fap`.
4. Review staged files, the source ZIP, and validation claims for private data.
5. Commit and push the branch, then push the matching version tag.

The workflow publishes `tonie_emulator.fap`, a corresponding source ZIP, and
`SHA256SUMS`. GitHub's latest-release URL in the README always points to the
release FAP. No device logs, dumps, or local build directories belong in a release.
