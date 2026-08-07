FIRMWARE_DIR := flipperzero-firmware
PLUGIN_DIR := plugin
NFC_PLUGINS_DIR := $(FIRMWARE_DIR)/applications/main/nfc/plugins/supported_cards
TEST_DIR := test

# ufbt build path. The ufbt SDK is distributed prebuilt with -Os and NDEBUG,
# i.e. it already is the DEBUG=0 COMPACT=1 configuration that `build` asks fbt
# for; ufbt does not accept those flags on the command line.
UFBT_HOME ?= $(HOME)/.ufbt
UFBT_PYTHON := $(UFBT_HOME)/toolchain/current/bin/python3
UFBT_STORAGE := $(UFBT_HOME)/current/scripts/storage.py
FLIPPER_PLUGIN_PATH := /ext/apps_data/nfc/plugins/bambu_parser.fal
# Optional. Left empty, storage.py auto-detects the connected Flipper.
# Override with: make ufbt-deploy FLIPPER_PORT=/dev/cu.usbmodemflip_XXXXXXXX
FLIPPER_PORT ?=

.PHONY: build clean copy-plugin test ufbt-build ufbt-deploy

# DEBUG=0 COMPACT=1 is a release build: it defines NDEBUG, so `furi_assert`
# compiles to nothing. Both build paths are release builds, so there is no
# configuration in this repo where an assert survives. Use `furi_check` (or an
# explicit `if`) for anything that has to hold in production.
build: copy-plugin
	cd $(FIRMWARE_DIR) && ./fbt DEBUG=0 COMPACT=1 fap_bambu_parser
	mkdir -p dist
	@FAL_FILE=$$(find $(FIRMWARE_DIR)/build -name "bambu_parser.fal" 2>/dev/null | head -1); \
	if [ -n "$$FAL_FILE" ]; then \
		cp "$$FAL_FILE" dist/; \
		echo "Plugin built: dist/bambu_parser.fal"; \
	else \
		echo "ERROR: bambu_parser.fal not found in build directory"; \
		exit 1; \
	fi

copy-plugin:
	cp $(PLUGIN_DIR)/bambu.c $(NFC_PLUGINS_DIR)/
	cp $(PLUGIN_DIR)/bambu_filaments.h $(NFC_PLUGINS_DIR)/
	cp $(PLUGIN_DIR)/bambu_parser.h $(NFC_PLUGINS_DIR)/
	@if ! grep -q "bambu_parser" $(FIRMWARE_DIR)/applications/main/nfc/application.fam; then \
		echo "" >> $(FIRMWARE_DIR)/applications/main/nfc/application.fam; \
		echo "App(" >> $(FIRMWARE_DIR)/applications/main/nfc/application.fam; \
		echo "    appid=\"bambu_parser\"," >> $(FIRMWARE_DIR)/applications/main/nfc/application.fam; \
		echo "    apptype=FlipperAppType.PLUGIN," >> $(FIRMWARE_DIR)/applications/main/nfc/application.fam; \
		echo "    entry_point=\"bambu_plugin_ep\"," >> $(FIRMWARE_DIR)/applications/main/nfc/application.fam; \
		echo "    targets=[\"f7\"]," >> $(FIRMWARE_DIR)/applications/main/nfc/application.fam; \
		echo "    requires=[\"nfc\"]," >> $(FIRMWARE_DIR)/applications/main/nfc/application.fam; \
		echo "    sources=[\"plugins/supported_cards/bambu.c\"]," >> $(FIRMWARE_DIR)/applications/main/nfc/application.fam; \
		echo "    fap_version=\"1.1\"," >> $(FIRMWARE_DIR)/applications/main/nfc/application.fam; \
		echo ")" >> $(FIRMWARE_DIR)/applications/main/nfc/application.fam; \
	fi

ufbt-build:
	ufbt
	@echo "Plugin built: dist/bambu_parser.fal"

# Uses the SDK's own storage.py rather than `ufbt launch` because ufbt only
# builds a launch target for FlipperAppType.EXTERNAL apps (SDK
# scripts/ufbt/SConstruct:291); this app is a PLUGIN, so that target errors out
# and APPID= does not help. storage.py is a private-ish interface that could
# move — the failure mode is a loud "No such file" in a dev-only target.
ufbt-deploy: ufbt-build
	$(UFBT_PYTHON) $(UFBT_STORAGE) $(if $(FLIPPER_PORT),-p $(FLIPPER_PORT)) \
		send dist/bambu_parser.fal $(FLIPPER_PLUGIN_PATH)
	@echo "Deployed to $(FLIPPER_PLUGIN_PATH) - restart the NFC app to load it."

clean:
	rm -rf $(FIRMWARE_DIR)/build
	rm -rf dist
	rm -f $(NFC_PLUGINS_DIR)/bambu.c
	rm -f $(NFC_PLUGINS_DIR)/bambu_filaments.h
	rm -f $(NFC_PLUGINS_DIR)/bambu_parser.h
	rm -f $(TEST_DIR)/test_bambu
	rm -f .vscode/compile_commands.json
	@rmdir .vscode 2>/dev/null || true
	@# ufbt keeps its object files under $(UFBT_HOME)/build, outside the repo.
	@# Only clean it if an SDK is already unpacked, so `make clean` never
	@# triggers an SDK download on a fresh checkout. It can still fetch the ARM
	@# toolchain, though: `ufbt -c` sources the SDK's fbtenv.sh, which downloads
	@# and unpacks it whenever $(UFBT_HOME)/toolchain/<arch>-<os> is missing, has
	@# no VERSION file, or holds a version the SDK does not want. One-off, but
	@# not instant.
	@if command -v ufbt >/dev/null 2>&1 && [ -f "$(UFBT_HOME)/current/ufbt_state.json" ]; then \
		ufbt -c >/dev/null; \
	fi

test: $(TEST_DIR)/test_bambu
	./$(TEST_DIR)/test_bambu

$(TEST_DIR)/test_bambu: $(TEST_DIR)/test_bambu.c $(PLUGIN_DIR)/bambu_parser.h $(PLUGIN_DIR)/bambu_filaments.h
	gcc -o $@ $< -lm -Wall -Wextra
