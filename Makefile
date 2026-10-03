DOOM_SRC := $(wildcard src/doom/*.c)
PORT_SRC := $(wildcard src/port/*.c) $(wildcard src/audio/*.c) $(wildcard src/platform/*.c) \
	$(wildcard src/launcher/*.c)
HOST_SRC := $(wildcard src/host/*.c)
PS5_SRC := $(wildcard src/ps5/*.c)

include third_party/third_party.mk

INCLUDES := -Isrc/doom -Isrc/port -Isrc/platform -Isrc/audio -Isrc/launcher $(ARCHIVE_INCLUDES)
DOOM_WARNINGS := -Wall -Wno-unused-const-variable -Wno-unused-but-set-variable \
	-Wno-unused-variable -Wno-logical-not-parentheses -Wno-missing-braces
OWN_WARNINGS := -Wall -Wextra -Wno-unused-parameter

HOST_CC := clang
HOST_FLAGS := -O2 -g -fno-strict-aliasing -MMD -MP

HOST_DIR := build/host
HOST_OWN_OBJ := $(patsubst src/%.c,$(HOST_DIR)/%.o,$(DOOM_SRC) $(PORT_SRC) $(HOST_SRC))
HOST_OBJ := $(HOST_OWN_OBJ) $(call third_obj,$(HOST_DIR))

TITLE := PPSA99666
NATIVE := .deps/native-app
SDK := $(NATIVE)/.deps/native/ps5-payload-sdk
NATIVE_TOOL := $(NATIVE)/build/host/ps5-native-tool
RUNTIME := $(NATIVE)/runtime/libc.prx

PS5_DIR := build/ps5
PS5_GEN := $(PS5_DIR)/gen
PS5_STUBS := $(PS5_DIR)/stubs
PS5_OWN_OBJ := $(patsubst src/%.c,$(PS5_DIR)/%.o,$(DOOM_SRC) $(PORT_SRC) $(PS5_SRC))
PS5_OBJ := $(PS5_OWN_OBJ) $(call third_obj,$(PS5_DIR))
AGC_STUBS := $(patsubst src/ps5/stubs/%.txt,$(PS5_STUBS)/%.so,$(wildcard src/ps5/stubs/*.txt))
PS5_CC := clang -target x86_64-sie-ps5 -fvisibility-nodllstorageclass=default \
	-isysroot $(SDK) -isystem $(SDK)/target/include \
	-fno-stack-protector -fno-plt -femulated-tls -ffunction-sections -fdata-sections
PS5_FLAGS := -O2 -fno-strict-aliasing -MMD -MP
APP := dist/$(TITLE)

.PHONY: host ps5 release tools clean

host: $(HOST_DIR)/bin/doom

$(HOST_DIR)/bin/doom: $(HOST_OBJ)
	@mkdir -p $(dir $@)
	$(HOST_CC) -o $@ $^ -lm -lpthread -lcurl

$(HOST_DIR)/doom/%.o: src/doom/%.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu99 $(HOST_FLAGS) $(DOOM_WARNINGS) $(INCLUDES) -c $< -o $@

$(HOST_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 $(HOST_FLAGS) $(OWN_WARNINGS) $(INCLUDES) -c $< -o $@

$(HOST_DIR)/third_party/%.o: $(ARCHIVE_DEPS)/%.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(THIRD_FLAGS) -g -MMD -MP $(call third_flags,$<) -c $< -o $@

$(HOST_OWN_OBJ): | $(ARCHIVE_STAMP)

tools: $(NATIVE_TOOL)

$(NATIVE_TOOL) $(RUNTIME) $(SDK)/target/lib/libkernel.so:
	bash tools/fetch-native-tools.sh

wads/DOOM1.WAD:
	bash tools/fetch-shareware.sh $@

ps5: $(APP)/eboot.bin

$(PS5_STUBS)/.sdk: $(SDK)/target/lib/libkernel.so
	@mkdir -p $(PS5_STUBS)
	cp $(SDK)/target/lib/*.so $(PS5_STUBS)/
	@touch $@

$(PS5_STUBS)/%.so: src/ps5/stubs/%.txt $(PS5_STUBS)/.sdk
	sed 's/.*/void &(void) {}/' $< > $(PS5_STUBS)/$*.c
	$(PS5_CC) -fPIC -c $(PS5_STUBS)/$*.c -o $(PS5_STUBS)/$*.o
	$(SDK)/bin/prospero-lld --shared -soname $*.sprx -o $@ $(PS5_STUBS)/$*.o

$(PS5_GEN)/present_kernel.h: src/ps5/present.cl tools/embed-kernel.py
	@mkdir -p $(PS5_GEN)
	clang -target amdgcn-amd-amdhsa -mcpu=gfx1010 -mwavefrontsize64 -x cl -cl-std=CL1.2 -O3 -nogpulib \
		-c $< -o $(PS5_GEN)/present.o
	ld.lld -shared $(PS5_GEN)/present.o -o $(PS5_GEN)/present.co
	python3 tools/embed-kernel.py $(PS5_GEN)/present.co present $@

$(PS5_DIR)/ps5/gpu.o: $(PS5_GEN)/present_kernel.h

$(PS5_DIR)/doom/%.o: src/doom/%.c $(SDK)/target/lib/libkernel.so
	@mkdir -p $(dir $@)
	$(PS5_CC) -std=gnu99 $(PS5_FLAGS) $(DOOM_WARNINGS) $(INCLUDES) -c $< -o $@

$(PS5_DIR)/%.o: src/%.c $(SDK)/target/lib/libkernel.so
	@mkdir -p $(dir $@)
	$(PS5_CC) -std=gnu11 $(PS5_FLAGS) $(OWN_WARNINGS) $(INCLUDES) -I$(PS5_GEN) -c $< -o $@

$(PS5_DIR)/third_party/%.o: $(ARCHIVE_DEPS)/%.c $(SDK)/target/lib/libkernel.so
	@mkdir -p $(dir $@)
	$(PS5_CC) $(THIRD_FLAGS) -MMD -MP $(call third_flags,$<) -c $< -o $@

$(PS5_OWN_OBJ): | $(ARCHIVE_STAMP)

$(PS5_DIR)/llvm-pie.elf: $(PS5_OBJ) $(AGC_STUBS) src/ps5/symbols.map
	$(SDK)/bin/prospero-lld -T $(NATIVE)/tooling/native/ps5-pie.ld --eh-frame-hdr --gc-sections \
		--version-script src/ps5/symbols.map -e _start -o $@ $(PS5_OBJ) \
		--as-needed $(PS5_STUBS)/*.so

$(PS5_DIR)/eboot.elf: $(PS5_DIR)/llvm-pie.elf $(NATIVE_TOOL)
	$(NATIVE_TOOL) link --in $< --out $@ --stub-dir $(PS5_STUBS) \
		--module-sdk 0x02000009 --companion-sdk 0x08050001 --file-name eboot.elf

$(APP)/eboot.bin: $(PS5_DIR)/eboot.elf $(RUNTIME) sce_sys/param.json sce_sys/icon0.png wads/DOOM1.WAD
	rm -rf $(APP)
	mkdir -p $(APP)/sce_sys $(APP)/sce_module $(APP)/wads
	$(NATIVE_TOOL) self --sign --in $< --out $@ --magic 0x1D3D154F
	cp $(RUNTIME) $(APP)/sce_module/libc.prx
	cp sce_sys/param.json sce_sys/icon0.png $(APP)/sce_sys/
	cp wads/DOOM1.WAD $(APP)/wads/doom1.wad
	mkdir -p $(APP)/licenses
	cp LICENSE $(APP)/licenses/DOOM-GPL-2.0.txt
	cp THIRD_PARTY_NOTICES.md $(APP)/licenses/
	cp $(NATIVE)/LICENSE $(APP)/licenses/libc-prx-GPL-3.0.txt
	cp $(LA_DIR)/../COPYING $(APP)/licenses/libarchive.txt
	cp $(XZ_DIR)/../../COPYING.0BSD $(APP)/licenses/liblzma.txt
	cp $(ZL_DIR)/LICENSE $(APP)/licenses/zlib.txt
	$(NATIVE_TOOL) self --inspect --file $@
	$(NATIVE_TOOL) self --inspect --file $(APP)/sce_module/libc.prx

release: $(APP)/eboot.bin
	rm -f dist/$(TITLE).zip dist/$(TITLE).zip.sha256
	cd dist && python3 -m zipfile -c $(TITLE).zip $(TITLE) && sha256sum $(TITLE).zip > $(TITLE).zip.sha256

-include $(HOST_OBJ:.o=.d) $(PS5_OBJ:.o=.d)

clean:
	rm -rf build dist
