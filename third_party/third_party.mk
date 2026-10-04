THIRD_DEPS := .deps/third-party
THIRD_STAMP := $(THIRD_DEPS)/QR-Code-generator-1.8.0/.complete
LA_DIR := $(THIRD_DEPS)/libarchive-3.8.9/libarchive
XZ_DIR := $(THIRD_DEPS)/xz-5.8.4/src/liblzma
ZL_DIR := $(THIRD_DEPS)/zlib-1.3.2
QR_DIR := $(THIRD_DEPS)/QR-Code-generator-1.8.0/c

LA_SRC := $(addprefix $(LA_DIR)/, archive_read.c archive_read_set_options.c archive_options.c \
	archive_util.c archive_string.c archive_string_sprintf.c archive_entry.c archive_check_magic.c \
	archive_virtual.c archive_read_support_format_zip.c archive_read_support_format_7zip.c \
	archive_read_support_format_rar.c archive_read_support_format_rar5.c archive_ppmd7.c \
	archive_ppmd8.c archive_blake2s_ref.c archive_blake2sp_ref.c archive_cryptor.c archive_hmac.c \
	archive_read_add_passphrase.c archive_time.c archive_acl.c archive_entry_xattr.c \
	archive_entry_sparse.c archive_rb.c archive_random.c)
XZ_SRC := $(addprefix $(XZ_DIR)/, common/common.c common/alone_decoder.c common/block_decoder.c \
	common/block_header_decoder.c common/block_util.c common/filter_common.c \
	common/filter_decoder.c common/filter_flags_decoder.c common/index_hash.c \
	common/stream_decoder.c common/stream_flags_common.c common/stream_flags_decoder.c \
	common/vli_decoder.c common/vli_size.c common/easy_preset.c check/check.c check/crc32_fast.c \
	check/crc64_fast.c check/sha256.c lz/lz_decoder.c lzma/lzma_decoder.c lzma/lzma2_decoder.c \
	lzma/lzma_encoder_presets.c delta/delta_common.c delta/delta_decoder.c simple/simple_coder.c \
	simple/simple_decoder.c simple/x86.c simple/arm.c simple/armthumb.c simple/arm64.c \
	simple/ia64.c simple/powerpc.c simple/sparc.c simple/riscv.c)
ZL_SRC := $(addprefix $(ZL_DIR)/, adler32.c crc32.c inffast.c inflate.c inftrees.c zutil.c)
THIRD_SRC := $(LA_SRC) $(XZ_SRC) $(ZL_SRC) $(QR_DIR)/qrcodegen.c

THIRD_INCLUDES := -I$(LA_DIR) -I$(QR_DIR)
LA_FLAGS := -DHAVE_CONFIG_H -Ithird_party/libarchive -I$(LA_DIR) -I$(XZ_DIR)/api -I$(ZL_DIR)
XZ_FLAGS := -DHAVE_CONFIG_H -Ithird_party/xz -I$(XZ_DIR)/api -I$(XZ_DIR)/common -I$(XZ_DIR)/check \
	-I$(XZ_DIR)/lz -I$(XZ_DIR)/rangecoder -I$(XZ_DIR)/lzma -I$(XZ_DIR)/delta -I$(XZ_DIR)/simple \
	-I$(XZ_DIR)/../common
ZL_FLAGS := -I$(ZL_DIR)
QR_FLAGS := -I$(QR_DIR)
THIRD_FLAGS := -std=gnu11 -O2 -w -fno-strict-aliasing -DNDEBUG -Dmalloc=import_malloc \
	-Dcalloc=import_calloc -Drealloc=import_realloc -Dfree=import_free -Dstrdup=import_strdup

third_obj = $(patsubst $(THIRD_DEPS)/%.c,$(1)/third_party/%.o,$(THIRD_SRC))
third_flags = $(if $(findstring /libarchive-,$(1)),$(LA_FLAGS),$(if $(findstring /xz-,$(1)),$(XZ_FLAGS),$(if $(findstring /QR-Code-,$(1)),$(QR_FLAGS),$(ZL_FLAGS))))

$(THIRD_SRC): | $(THIRD_STAMP)

$(THIRD_STAMP):
	bash tools/fetch-third-party.sh $(THIRD_DEPS)
