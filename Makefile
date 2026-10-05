PYTHON ?= python3
FAP := $(SDK_DIR)/build/f7-firmware-C/.extapps/tonie_emulator.fap

# No SDK_DIR given: use the first checkout that already knows this app.
# A checkout "knows" the app when applications_user/tonie_emulator points here
# (fbt builds the linked source in place, so no copying or syncing is needed).
CANDIDATES := ../momentum ../../toy-blocks/.tools/momentum ../toy-blocks/.tools/momentum
SDK_DIR ?= $(firstword $(foreach d,$(CANDIDATES),$(patsubst %/applications_user/tonie_emulator,%,$(wildcard $(d)/applications_user/tonie_emulator))))
ifeq ($(SDK_DIR),)
ifneq ($(filter-out check check-browser,$(or $(MAKECMDGOALS),build)),)
$(error No Momentum checkout found. Clone Momentum and link this repo into it:\n\
	git clone --depth 1 https://github.com/Next-Flip/Momentum-Firmware ../momentum\n\
	ln -s $(abspath .) ../momentum/applications_user/tonie_emulator)
endif
endif
SDK_DIR := $(abspath $(SDK_DIR))

.PHONY: build install release
build:
	cd "$(SDK_DIR)" && ./fbt fap_tonie_emulator

# Build, upload over USB and start on the connected Flipper (fbt picks the port).
install:
	cd "$(SDK_DIR)" && ./fbt launch APPSRC=tonie_emulator

release: SDK_DIR ?= ../momentum
release:
	$(PYTHON) tools/package_release.py --fap "$(abspath $(SDK_DIR))/build/f7-firmware-C/.extapps/tonie_emulator.fap"

CC ?= cc
CHECK_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -g
.PHONY: check
check:
	mkdir -p build-host
	$(CC) $(CHECK_FLAGS) tests/test_quiet_recovery.c -o build-host/test_quiet_recovery
	$(CC) $(CHECK_FLAGS) tests/test_log_sink.c -o build-host/test_log_sink
	$(CC) $(CHECK_FLAGS) tests/test_sanitize.c -o build-host/test_sanitize
	$(CC) $(CHECK_FLAGS) -Itests/stubs tests/test_writer.c -o build-host/test_writer
	./build-host/test_writer
	./build-host/test_quiet_recovery
	./build-host/test_log_sink
	./build-host/test_sanitize
	$(PYTHON) tests/test_native_dispatch.py
	$(PYTHON) tests/test_listener_memory.py
	$(PYTHON) tests/test_write_controls.py
	$(PYTHON) tests/test_reader.py
	$(PYTHON) tests/test_read_check.py

.PHONY: check-browser
check-browser:
	sh ./tests/run_browser_tests.sh
