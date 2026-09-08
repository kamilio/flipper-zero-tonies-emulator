# Earlier protocol comparison

An earlier external recording exercised inventory, STAY_QUIET, GET_RANDOM and
broadcast RESET_TO_READY. Portable-core replay matched the observed responses,
but did not exercise the later playback cutoff or measure this emulator's transmit
waveform. Those private recordings and separate tooling are not part of this repo.

The active firmware-based engine and the physical cutoff/recovery evidence are
described in [research.md](research.md). Portable-core tests should not be treated
as proof of the active engine's behavior or sustained playback.
