# Contributing

Use Python 3 for release packaging and a C11 compiler for `make check`.
Two synthetic tests cover quiet recovery and logger buffering. Private captures
and the broader local test suite are not distributed.

Build the application against the pinned Momentum SDK:

```sh
git clone --recursive https://github.com/Next-Flip/Momentum-Firmware.git momentum
cd momentum
git checkout e1784e7418d8b074e971983ceb6fef0f37e52ae4
git submodule update --init --recursive
ln -s /path/to/flipper-zero-tonies-emulator applications_user/tonie_emulator
./fbt fap_tonie_emulator
```

The output is `build/f7-firmware-C/.extapps/tonie_emulator.fap`. API symbol checks
must pass. Build instructions require internet access for SDK/toolchain downloads.

For protocol changes, run a physical test and validate against synthetic inputs. Keep
portable-core results distinct from the embedded firmware engine's behavior.
Record firmware versions, elapsed playback time, and aggregate recovery counts.
Do not claim indefinite or universal playback from a short test.

Do not commit NFC dumps, audio, account credentials, certificates, real tag UIDs,
raw exchange logs, or local absolute paths. Logs can reveal both tag memory and
password exchanges. Report only sanitized command sequences and aggregate counts.

Vendored code is GPL-3.0. Preserve upstream attribution and document local changes.
