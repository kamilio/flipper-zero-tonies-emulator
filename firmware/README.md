# Momentum mntm-012 NFC-V patch

`mntm-012-nfcv.patch` contains selected changes from [PR #566](https://github.com/Next-Flip/Momentum-Firmware/pull/566) plus state-reset corrections described in [the research notes](../docs/research.md). It targets `mntm-012` (`e1784e7418d8b074e971983ceb6fef0f37e52ae4`). It changes firmware, not an SD-card app. No firmware is flashed by the build commands.

In a clean, recursive Momentum checkout at that tag:

```sh
git apply --check /absolute/path/flipper-tonie-emulator/firmware/mntm-012-nfcv.patch
git apply /absolute/path/flipper-tonie-emulator/firmware/mntm-012-nfcv.patch
./fbt updater_package
```

Install the resulting updater package using the normal firmware update workflow. Keep the installed version identifiable as a custom build for testing. The FAP also runs on stock API 87.1 firmware, but a stock HAL still ignores low-rate response requests.

This patch leaves the RF response-delay timer and single-subcarrier waveform generator unchanged. Hardware playback and timing validation remain outstanding. It does not provide a general guarantee of every SLIX command or every Box firmware revision working.
