PYTHON ?= python3
SDK_DIR ?= ../momentum
FAP := $(SDK_DIR)/build/f7-firmware-C/.extapps/tonie_emulator.fap

.PHONY: build release
build:
	cd "$(SDK_DIR)" && ./fbt fap_tonie_emulator

release:
	$(PYTHON) tools/package_release.py --fap "$(FAP)"

CC ?= cc
CHECK_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -g
.PHONY: check
check:
	mkdir -p build-host
	$(CC) $(CHECK_FLAGS) tests/test_quiet_recovery.c -o build-host/test_quiet_recovery
	$(CC) $(CHECK_FLAGS) tests/test_log_sink.c -o build-host/test_log_sink
	./build-host/test_quiet_recovery
	./build-host/test_log_sink
