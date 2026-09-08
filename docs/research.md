# Cutoff investigation

## Observed failure

A Toniebox playback test stopped while the Flipper remained in emulation. At
112.518 seconds, the reader sent STAY_QUIET. Privacy authentication succeeded,
but no RESET_TO_READY was captured. From 112.587 seconds onward the emulator
answered password commands but ignored inventory because it remained QUIET.

Introducing the original figurine let the reader read all eight blocks without
Flipper responses. A subsequent RESET_TO_READY restored Flipper inventory replies.
No lock commands occurred in this capture. This identifies the observed failure
as a state-recovery issue; it does not establish why the reset was missing.

## Compatibility recovery

The active engine embeds the ISO15693/SLIX listener from Momentum `d3f89dfe`.
After successful privacy authentication while QUIET, a fresh, unmasked single-slot
inventory restores READY. The log marks this as QUIET_RECOVERY. Failed password
validation, masked inventories, multislot inventory and EOF do not trigger it.

This is a Toniebox-specific compatibility policy. Standard ISO15693 does not
mandate this transition, and the application does not claim to detect RF field
loss. The installed transparent-mode HAL exposes receive/abort events but not
physical field resets. No firmware update is needed for this workaround.

## Validation

On Momentum `d3f89dfe` / API 87.1, a Cars tag played continuously for 299.293 seconds
with the original figurine removed. The user confirmed audio continued. Six
recovery events each immediately preceded a successful inventory response.

| Measure | Result |
| --- | ---: |
| Recovery events | 6 |
| Received frames | 28,777 |
| Transmitted frames | 22,619 |
| Transmit errors | 0 |
| Dropped log records | 0 |
| SD errors | 0 |

The test was stopped deliberately to retrieve the log. This is evidence for the
observed failure on that setup, not a guarantee for every tag, box firmware, or
indefinite playback. Later icon, LED and label changes passed build/API checks;
the five-minute result specifically validates the recovery build.

Host tests cover the recovery guards, repeated missing-reset sequences, portable
protocol handling, and logger write failures. Test credential values are synthetic.
Private RF captures and NFC dumps are intentionally not distributed.

## Primary references

- [Momentum ISO15693 HAL](https://github.com/Next-Flip/Momentum-Firmware/blob/d3f89dfe2ef6b01839201598e9be1590cba80322/targets/f7/furi_hal/furi_hal_nfc_iso15693.c)
- [NXP ICODE SLIX-L datasheet](https://www.nxp.com/docs/en/data-sheet/SL2S5002_SL2S5102.pdf)
- [ST25R3916 datasheet](https://www.st.com/resource/en/datasheet/st25r3916.pdf)
