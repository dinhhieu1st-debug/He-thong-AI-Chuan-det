####################################################################
# Automatically-generated file. Do not edit!                       #
# Makefile Version 21                                              #
####################################################################

BASE_SDK_PATH = C:/Users/admin/.silabs/slt/installs/conan/p/simpl35774a752829c/p
BASE_PKG_PATH = C:/Users/admin/.silabs/slt/installs
UNAME:=$(shell $(POSIX_TOOL_PATH)uname -s | $(POSIX_TOOL_PATH)sed -e 's/^\(CYGWIN\).*/\1/' | $(POSIX_TOOL_PATH)sed -e 's/^\(MINGW\).*/\1/')
ifeq ($(UNAME),MINGW)
# Translate "C:/super" into "/C/super" for MinGW make.
SDK_PATH := /$(shell $(POSIX_TOOL_PATH)echo $(BASE_SDK_PATH) | sed s/://)
PKG_PATH := /$(shell $(POSIX_TOOL_PATH)echo $(BASE_PKG_PATH) | sed s/://)
endif
SDK_PATH ?= $(BASE_SDK_PATH)
PKG_PATH ?= $(BASE_PKG_PATH)
COPIED_SDK_PATH ?= simplicity_sdk_2025.12.3

# This uses the explicit build rules below
PROJECT_SOURCE_FILES =

C_SOURCE_FILES   += $(filter %.c, $(PROJECT_SOURCE_FILES))
CXX_SOURCE_FILES += $(filter %.cpp, $(PROJECT_SOURCE_FILES))
CXX_SOURCE_FILES += $(filter %.cc, $(PROJECT_SOURCE_FILES))
ASM_SOURCE_FILES += $(filter %.s, $(PROJECT_SOURCE_FILES))
ASM_SOURCE_FILES += $(filter %.S, $(PROJECT_SOURCE_FILES))
LIB_FILES        += $(filter %.a, $(PROJECT_SOURCE_FILES))

C_DEFS += \
 '-DEFR32MG26B510F3200IM48=1' \
 '-DSL_CODE_COMPONENT_SYSTEM=system' \
 '-DSE_MANAGER_CONFIG_FILE="btl_aes_ctr_stream_block_cfg.h"' \
 '-DBOOTLOADER_ENABLE=1' \
 '-DBOOTLOADER_SECOND_STAGE=1' \
 '-DSL_RAMFUNC_DISABLE=1' \
 '-D__START=main' \
 '-D__STARTUP_CLEAR_BSS=1' \
 '-DSYSTEM_NO_STATIC_MEMORY=1' \
 '-DBOOTLOADER_SUPPORT_INTERNAL_STORAGE=1' \
 '-DBOOTLOADER_SUPPORT_STORAGE=1' \
 '-DHARDWARE_BOARD_DEFAULT_RF_BAND_2400=1' \
 '-DHARDWARE_BOARD_SUPPORTS_1_RF_BAND=1' \
 '-DHARDWARE_BOARD_SUPPORTS_RF_BAND_2400=1' \
 '-DHFXO_FREQ=39000000' \
 '-DSL_BOARD_NAME="BRD2709A"' \
 '-DSL_BOARD_REV="A03"' \
 '-DSL_COMPONENT_CATALOG_PRESENT=1' \
 '-DMBEDTLS_CONFIG_FILE=<sl_mbedtls_trustzone_config.h>' \
 '-DSL_CODE_COMPONENT_MEMORY_MANAGER=memory_manager' \
 '-DMBEDTLS_PSA_CRYPTO_CONFIG_FILE=<psa_crypto_config.h>' \
 '-DSL_CODE_COMPONENT_SE_MANAGER=se_manager' \
 '-DSL_CODE_COMPONENT_CORE=core' \
 '-DSL_CODE_COMPONENT_PSEC_OSAL=psec_osal' \
 '-DSL_TRUSTZONE_SECURE=1'

ASM_DEFS += \
 '-DEFR32MG26B510F3200IM48=1' \
 '-DSL_CODE_COMPONENT_SYSTEM=system' \
 '-DSE_MANAGER_CONFIG_FILE="btl_aes_ctr_stream_block_cfg.h"' \
 '-DBOOTLOADER_ENABLE=1' \
 '-DBOOTLOADER_SECOND_STAGE=1' \
 '-DSL_RAMFUNC_DISABLE=1' \
 '-D__START=main' \
 '-D__STARTUP_CLEAR_BSS=1' \
 '-DSYSTEM_NO_STATIC_MEMORY=1' \
 '-DBOOTLOADER_SUPPORT_INTERNAL_STORAGE=1' \
 '-DBOOTLOADER_SUPPORT_STORAGE=1' \
 '-DHARDWARE_BOARD_DEFAULT_RF_BAND_2400=1' \
 '-DHARDWARE_BOARD_SUPPORTS_1_RF_BAND=1' \
 '-DHARDWARE_BOARD_SUPPORTS_RF_BAND_2400=1' \
 '-DHFXO_FREQ=39000000' \
 '-DSL_BOARD_NAME="BRD2709A"' \
 '-DSL_BOARD_REV="A03"' \
 '-DSL_COMPONENT_CATALOG_PRESENT=1' \
 '-DMBEDTLS_CONFIG_FILE=<sl_mbedtls_trustzone_config.h>' \
 '-DSL_CODE_COMPONENT_MEMORY_MANAGER=memory_manager' \
 '-DMBEDTLS_PSA_CRYPTO_CONFIG_FILE=<psa_crypto_config.h>' \
 '-DSL_CODE_COMPONENT_SE_MANAGER=se_manager' \
 '-DSL_CODE_COMPONENT_CORE=core' \
 '-DSL_CODE_COMPONENT_PSEC_OSAL=psec_osal' \
 '-DSL_TRUSTZONE_SECURE=1'

INCLUDES += \
 -Iconfig \
 -Iautogen \
 -I$(SDK_PATH)/devices/platform/Device/SiliconLabs/EFR32MG26/Include \
 -I$(SDK_PATH)/platform_common/platform/common/inc \
 -I$(SDK_PATH)/bootloader/platform/bootloader \
 -I$(SDK_PATH)/bootloader/platform/bootloader/api \
 -I$(SDK_PATH)/bootloader/platform/bootloader/debug \
 -I$(SDK_PATH)/bootloader/platform/bootloader/parser \
 -I$(SDK_PATH)/bootloader/platform/bootloader/core/flash \
 -I$(SDK_PATH)/bootloader/platform/bootloader/security \
 -I$(SDK_PATH)/cmsis/Core/Include \
 -I$(SDK_PATH)/platform_core/platform/emlib/inc \
 -I$(SDK_PATH)/platform_core/platform/common/errno_error_codes/inc \
 -I$(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/config \
 -I$(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/config/preset \
 -I$(SDK_PATH)/security_mbedtls_source/include \
 -I$(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/inc \
 -I$(SDK_PATH)/security_mbedtls_source/library \
 -I$(SDK_PATH)/platform_core/platform/service/memory_manager/inc \
 -I$(SDK_PATH)/platform_core/platform/service/memory_manager/src \
 -I$(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/inc \
 -I$(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/inc \
 -I$(SDK_PATH)/platform_core/platform/common/inc \
 -I$(SDK_PATH)/security_tfm/lib/fih/inc \
 -I$(SDK_PATH)/security_tfm/platform/include \
 -I$(SDK_PATH)/security_se_manager/platform/security/sl_component/sli_psec_osal/inc

GROUP_START =-Wl,--start-group
GROUP_END =-Wl,--end-group

PROJECT_LIBS = \
 -lgcc \
 -lc \
 -lm \
 -lnosys

LIBS += $(GROUP_START) $(PROJECT_LIBS) $(GROUP_END)

LIB_FILES += $(filter %.a, $(PROJECT_LIBS))

C_FLAGS += \
 -mcpu=cortex-m33 \
 -mthumb \
 -mfpu=fpv5-sp-d16 \
 -mfloat-abi=hard \
 -std=c18 \
 -mcmse \
 -Wall \
 -Wextra \
 -Os \
 -fdata-sections \
 -ffunction-sections \
 -fomit-frame-pointer \
 -g \
 --specs=nano.specs \
 -Wno-ignored-qualifiers \
 -Wno-sign-compare \
 -fno-lto

CXX_FLAGS += \
 -mcpu=cortex-m33 \
 -mthumb \
 -mfpu=fpv5-sp-d16 \
 -mfloat-abi=hard \
 -std=c++17 \
 -fno-rtti \
 -fno-exceptions \
 -mcmse \
 -Wall \
 -Wextra \
 -Os \
 -fdata-sections \
 -ffunction-sections \
 -fomit-frame-pointer \
 -g \
 --specs=nano.specs \
 -Wno-ignored-qualifiers \
 -Wno-sign-compare \
 -fno-lto

ASM_FLAGS += \
 -mcpu=cortex-m33 \
 -mthumb \
 -mfpu=fpv5-sp-d16 \
 -mfloat-abi=hard \
 -x assembler-with-cpp

LD_FLAGS += \
 -mcpu=cortex-m33 \
 -mthumb \
 -mfpu=fpv5-sp-d16 \
 -mfloat-abi=hard \
 -T"autogen/linkerfile.ld" \
 --specs=nano.specs \
 -Wl,-Map=$(OUTPUT_DIR)/$(PROJECTNAME).map \
 -Wl,--wrap=_free_r -Wl,--wrap=_malloc_r -Wl,--wrap=_calloc_r -Wl,--wrap=_realloc_r \
 -fno-lto \
 -Wl,--gc-sections


####################################################################
# Pre/Post Build Rules                                             #
####################################################################
pre-build:
	# No pre-build defined

post-build: $(OUTPUT_DIR)/$(PROJECTNAME).out
ifeq ($(POST_BUILD_EXE),)
		$(error POST_BUILD_EXE is not defined. Post-Build cannot run. Please set the STUDIO_ADAPTER_PACK_PATH to the post-build tool when generating or override the variable for this makefile)
endif
	@$(POSIX_TOOL_PATH)echo 'Running Project Post-Build'
	$(ECHO) @"$(POST_BUILD_EXE)" postbuild "./bootloader-storage-internal-single-3200k.slpb" --parameter build_dir:"$(OUTPUT_DIR)"

####################################################################
# SDK Build Rules                                                  #
####################################################################
$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/core/btl_bootload.o: $(SDK_PATH)/bootloader/platform/bootloader/core/btl_bootload.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/core/btl_bootload.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/core/btl_bootload.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/core/btl_bootload.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/core/btl_bootload.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/core/btl_core.o: $(SDK_PATH)/bootloader/platform/bootloader/core/btl_core.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/core/btl_core.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/core/btl_core.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/core/btl_core.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/core/btl_core.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/core/btl_main.o: $(SDK_PATH)/bootloader/platform/bootloader/core/btl_main.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/core/btl_main.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/core/btl_main.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/core/btl_main.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/core/btl_main.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/core/btl_parse.o: $(SDK_PATH)/bootloader/platform/bootloader/core/btl_parse.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/core/btl_parse.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/core/btl_parse.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/core/btl_parse.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/core/btl_parse.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/core/btl_reset.o: $(SDK_PATH)/bootloader/platform/bootloader/core/btl_reset.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/core/btl_reset.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/core/btl_reset.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/core/btl_reset.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/core/btl_reset.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/core/flash/btl_internal_flash.o: $(SDK_PATH)/bootloader/platform/bootloader/core/flash/btl_internal_flash.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/core/flash/btl_internal_flash.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/core/flash/btl_internal_flash.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/core/flash/btl_internal_flash.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/core/flash/btl_internal_flash.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/debug/btl_debug.o: $(SDK_PATH)/bootloader/platform/bootloader/debug/btl_debug.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/debug/btl_debug.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/debug/btl_debug.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/debug/btl_debug.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/debug/btl_debug.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/debug/btl_debug_swo.o: $(SDK_PATH)/bootloader/platform/bootloader/debug/btl_debug_swo.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/debug/btl_debug_swo.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/debug/btl_debug_swo.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/debug/btl_debug_swo.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/debug/btl_debug_swo.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/driver/btl_driver_util.o: $(SDK_PATH)/bootloader/platform/bootloader/driver/btl_driver_util.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/driver/btl_driver_util.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/driver/btl_driver_util.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/driver/btl_driver_util.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/driver/btl_driver_util.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/parser/gbl/btl_gbl_custom_tags.o: $(SDK_PATH)/bootloader/platform/bootloader/parser/gbl/btl_gbl_custom_tags.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/parser/gbl/btl_gbl_custom_tags.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/parser/gbl/btl_gbl_custom_tags.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/parser/gbl/btl_gbl_custom_tags.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/parser/gbl/btl_gbl_custom_tags.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/parser/gbl/btl_gbl_format.o: $(SDK_PATH)/bootloader/platform/bootloader/parser/gbl/btl_gbl_format.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/parser/gbl/btl_gbl_format.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/parser/gbl/btl_gbl_format.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/parser/gbl/btl_gbl_format.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/parser/gbl/btl_gbl_format.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/parser/gbl/btl_gbl_parser.o: $(SDK_PATH)/bootloader/platform/bootloader/parser/gbl/btl_gbl_parser.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/parser/gbl/btl_gbl_parser.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/parser/gbl/btl_gbl_parser.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/parser/gbl/btl_gbl_parser.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/parser/gbl/btl_gbl_parser.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/btl_crc16.o: $(SDK_PATH)/bootloader/platform/bootloader/security/btl_crc16.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/security/btl_crc16.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/security/btl_crc16.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/btl_crc16.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/btl_crc16.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/btl_crc32.o: $(SDK_PATH)/bootloader/platform/bootloader/security/btl_crc32.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/security/btl_crc32.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/security/btl_crc32.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/btl_crc32.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/btl_crc32.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/btl_security_aes.o: $(SDK_PATH)/bootloader/platform/bootloader/security/btl_security_aes.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/security/btl_security_aes.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/security/btl_security_aes.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/btl_security_aes.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/btl_security_aes.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/btl_security_ecdsa.o: $(SDK_PATH)/bootloader/platform/bootloader/security/btl_security_ecdsa.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/security/btl_security_ecdsa.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/security/btl_security_ecdsa.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/btl_security_ecdsa.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/btl_security_ecdsa.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/btl_security_sha256.o: $(SDK_PATH)/bootloader/platform/bootloader/security/btl_security_sha256.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/security/btl_security_sha256.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/security/btl_security_sha256.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/btl_security_sha256.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/btl_security_sha256.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/btl_security_tokens.o: $(SDK_PATH)/bootloader/platform/bootloader/security/btl_security_tokens.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/security/btl_security_tokens.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/security/btl_security_tokens.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/btl_security_tokens.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/btl_security_tokens.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/ecc/ecc.o: $(SDK_PATH)/bootloader/platform/bootloader/security/ecc/ecc.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/security/ecc/ecc.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/security/ecc/ecc.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/ecc/ecc.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/ecc/ecc.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/sha/btl_sha256.o: $(SDK_PATH)/bootloader/platform/bootloader/security/sha/btl_sha256.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/security/sha/btl_sha256.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/security/sha/btl_sha256.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/sha/btl_sha256.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/sha/btl_sha256.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/sha/crypto_sha.o: $(SDK_PATH)/bootloader/platform/bootloader/security/sha/crypto_sha.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/security/sha/crypto_sha.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/security/sha/crypto_sha.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/sha/crypto_sha.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/sha/crypto_sha.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/sha/cryptoacc_sha.o: $(SDK_PATH)/bootloader/platform/bootloader/security/sha/cryptoacc_sha.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/security/sha/cryptoacc_sha.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/security/sha/cryptoacc_sha.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/sha/cryptoacc_sha.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/sha/cryptoacc_sha.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/sha/se_sha.o: $(SDK_PATH)/bootloader/platform/bootloader/security/sha/se_sha.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/security/sha/se_sha.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/security/sha/se_sha.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/sha/se_sha.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/security/sha/se_sha.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/storage/bootloadinfo/btl_storage_bootloadinfo.o: $(SDK_PATH)/bootloader/platform/bootloader/storage/bootloadinfo/btl_storage_bootloadinfo.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/storage/bootloadinfo/btl_storage_bootloadinfo.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/storage/bootloadinfo/btl_storage_bootloadinfo.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/storage/bootloadinfo/btl_storage_bootloadinfo.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/storage/bootloadinfo/btl_storage_bootloadinfo.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/storage/btl_storage.o: $(SDK_PATH)/bootloader/platform/bootloader/storage/btl_storage.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/storage/btl_storage.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/storage/btl_storage.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/storage/btl_storage.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/storage/btl_storage.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/storage/btl_storage_library.o: $(SDK_PATH)/bootloader/platform/bootloader/storage/btl_storage_library.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/storage/btl_storage_library.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/storage/btl_storage_library.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/storage/btl_storage_library.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/storage/btl_storage_library.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/storage/internal_flash/btl_storage_internal_flash.o: $(SDK_PATH)/bootloader/platform/bootloader/storage/internal_flash/btl_storage_internal_flash.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/storage/internal_flash/btl_storage_internal_flash.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/storage/internal_flash/btl_storage_internal_flash.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/storage/internal_flash/btl_storage_internal_flash.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/storage/internal_flash/btl_storage_internal_flash.o

$(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/storage/internal_flash/btl_storage_internal_flash_raw.o: $(SDK_PATH)/bootloader/platform/bootloader/storage/internal_flash/btl_storage_internal_flash_raw.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/bootloader/platform/bootloader/storage/internal_flash/btl_storage_internal_flash_raw.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/bootloader/platform/bootloader/storage/internal_flash/btl_storage_internal_flash_raw.c
CDEPS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/storage/internal_flash/btl_storage_internal_flash_raw.d
OBJS += $(OUTPUT_DIR)/sdk/bootloader/platform/bootloader/storage/internal_flash/btl_storage_internal_flash_raw.o

$(OUTPUT_DIR)/sdk/devices/platform/Device/SiliconLabs/EFR32MG26/Source/startup_efr32mg26.o: $(SDK_PATH)/devices/platform/Device/SiliconLabs/EFR32MG26/Source/startup_efr32mg26.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/devices/platform/Device/SiliconLabs/EFR32MG26/Source/startup_efr32mg26.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/devices/platform/Device/SiliconLabs/EFR32MG26/Source/startup_efr32mg26.c
CDEPS += $(OUTPUT_DIR)/sdk/devices/platform/Device/SiliconLabs/EFR32MG26/Source/startup_efr32mg26.d
OBJS += $(OUTPUT_DIR)/sdk/devices/platform/Device/SiliconLabs/EFR32MG26/Source/startup_efr32mg26.o

$(OUTPUT_DIR)/sdk/devices/platform/Device/SiliconLabs/EFR32MG26/Source/system_efr32mg26.o: $(SDK_PATH)/devices/platform/Device/SiliconLabs/EFR32MG26/Source/system_efr32mg26.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/devices/platform/Device/SiliconLabs/EFR32MG26/Source/system_efr32mg26.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/devices/platform/Device/SiliconLabs/EFR32MG26/Source/system_efr32mg26.c
CDEPS += $(OUTPUT_DIR)/sdk/devices/platform/Device/SiliconLabs/EFR32MG26/Source/system_efr32mg26.d
OBJS += $(OUTPUT_DIR)/sdk/devices/platform/Device/SiliconLabs/EFR32MG26/Source/system_efr32mg26.o

$(OUTPUT_DIR)/sdk/platform_common/platform/common/src/sl_assert.o: $(SDK_PATH)/platform_common/platform/common/src/sl_assert.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_common/platform/common/src/sl_assert.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_common/platform/common/src/sl_assert.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_common/platform/common/src/sl_assert.d
OBJS += $(OUTPUT_DIR)/sdk/platform_common/platform/common/src/sl_assert.o

$(OUTPUT_DIR)/sdk/platform_common/platform/common/src/sl_syscalls.o: $(SDK_PATH)/platform_common/platform/common/src/sl_syscalls.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_common/platform/common/src/sl_syscalls.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_common/platform/common/src/sl_syscalls.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_common/platform/common/src/sl_syscalls.d
OBJS += $(OUTPUT_DIR)/sdk/platform_common/platform/common/src/sl_syscalls.o

$(OUTPUT_DIR)/sdk/platform_core/platform/common/src/sl_core_cortexm.o: $(SDK_PATH)/platform_core/platform/common/src/sl_core_cortexm.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/common/src/sl_core_cortexm.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/common/src/sl_core_cortexm.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/common/src/sl_core_cortexm.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/common/src/sl_core_cortexm.o

$(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_acmp.o: $(SDK_PATH)/platform_core/platform/emlib/src/em_acmp.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/emlib/src/em_acmp.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/emlib/src/em_acmp.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_acmp.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_acmp.o

$(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_burtc.o: $(SDK_PATH)/platform_core/platform/emlib/src/em_burtc.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/emlib/src/em_burtc.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/emlib/src/em_burtc.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_burtc.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_burtc.o

$(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_cmu.o: $(SDK_PATH)/platform_core/platform/emlib/src/em_cmu.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/emlib/src/em_cmu.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/emlib/src/em_cmu.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_cmu.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_cmu.o

$(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_dbg.o: $(SDK_PATH)/platform_core/platform/emlib/src/em_dbg.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/emlib/src/em_dbg.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/emlib/src/em_dbg.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_dbg.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_dbg.o

$(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_emu.o: $(SDK_PATH)/platform_core/platform/emlib/src/em_emu.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/emlib/src/em_emu.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/emlib/src/em_emu.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_emu.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_emu.o

$(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_eusart.o: $(SDK_PATH)/platform_core/platform/emlib/src/em_eusart.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/emlib/src/em_eusart.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/emlib/src/em_eusart.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_eusart.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_eusart.o

$(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_gpcrc.o: $(SDK_PATH)/platform_core/platform/emlib/src/em_gpcrc.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/emlib/src/em_gpcrc.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/emlib/src/em_gpcrc.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_gpcrc.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_gpcrc.o

$(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_gpio.o: $(SDK_PATH)/platform_core/platform/emlib/src/em_gpio.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/emlib/src/em_gpio.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/emlib/src/em_gpio.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_gpio.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_gpio.o

$(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_i2c.o: $(SDK_PATH)/platform_core/platform/emlib/src/em_i2c.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/emlib/src/em_i2c.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/emlib/src/em_i2c.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_i2c.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_i2c.o

$(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_iadc.o: $(SDK_PATH)/platform_core/platform/emlib/src/em_iadc.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/emlib/src/em_iadc.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/emlib/src/em_iadc.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_iadc.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_iadc.o

$(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_lcd.o: $(SDK_PATH)/platform_core/platform/emlib/src/em_lcd.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/emlib/src/em_lcd.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/emlib/src/em_lcd.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_lcd.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_lcd.o

$(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_ldma.o: $(SDK_PATH)/platform_core/platform/emlib/src/em_ldma.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/emlib/src/em_ldma.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/emlib/src/em_ldma.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_ldma.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_ldma.o

$(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_letimer.o: $(SDK_PATH)/platform_core/platform/emlib/src/em_letimer.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/emlib/src/em_letimer.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/emlib/src/em_letimer.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_letimer.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_letimer.o

$(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_msc.o: $(SDK_PATH)/platform_core/platform/emlib/src/em_msc.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/emlib/src/em_msc.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/emlib/src/em_msc.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_msc.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_msc.o

$(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_opamp.o: $(SDK_PATH)/platform_core/platform/emlib/src/em_opamp.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/emlib/src/em_opamp.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/emlib/src/em_opamp.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_opamp.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_opamp.o

$(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_pcnt.o: $(SDK_PATH)/platform_core/platform/emlib/src/em_pcnt.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/emlib/src/em_pcnt.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/emlib/src/em_pcnt.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_pcnt.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_pcnt.o

$(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_prs.o: $(SDK_PATH)/platform_core/platform/emlib/src/em_prs.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/emlib/src/em_prs.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/emlib/src/em_prs.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_prs.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_prs.o

$(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_rmu.o: $(SDK_PATH)/platform_core/platform/emlib/src/em_rmu.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/emlib/src/em_rmu.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/emlib/src/em_rmu.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_rmu.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_rmu.o

$(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_system.o: $(SDK_PATH)/platform_core/platform/emlib/src/em_system.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/emlib/src/em_system.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/emlib/src/em_system.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_system.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_system.o

$(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_timer.o: $(SDK_PATH)/platform_core/platform/emlib/src/em_timer.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/emlib/src/em_timer.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/emlib/src/em_timer.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_timer.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_timer.o

$(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_usart.o: $(SDK_PATH)/platform_core/platform/emlib/src/em_usart.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/emlib/src/em_usart.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/emlib/src/em_usart.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_usart.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_usart.o

$(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_vdac.o: $(SDK_PATH)/platform_core/platform/emlib/src/em_vdac.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/emlib/src/em_vdac.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/emlib/src/em_vdac.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_vdac.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_vdac.o

$(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_wdog.o: $(SDK_PATH)/platform_core/platform/emlib/src/em_wdog.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/emlib/src/em_wdog.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/emlib/src/em_wdog.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_wdog.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/emlib/src/em_wdog.o

$(OUTPUT_DIR)/sdk/platform_core/platform/service/memory_manager/src/sl_memory_manager.o: $(SDK_PATH)/platform_core/platform/service/memory_manager/src/sl_memory_manager.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/service/memory_manager/src/sl_memory_manager.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/service/memory_manager/src/sl_memory_manager.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/service/memory_manager/src/sl_memory_manager.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/service/memory_manager/src/sl_memory_manager.o

$(OUTPUT_DIR)/sdk/platform_core/platform/service/memory_manager/src/sl_memory_manager_dynamic_reservation.o: $(SDK_PATH)/platform_core/platform/service/memory_manager/src/sl_memory_manager_dynamic_reservation.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/service/memory_manager/src/sl_memory_manager_dynamic_reservation.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/service/memory_manager/src/sl_memory_manager_dynamic_reservation.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/service/memory_manager/src/sl_memory_manager_dynamic_reservation.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/service/memory_manager/src/sl_memory_manager_dynamic_reservation.o

$(OUTPUT_DIR)/sdk/platform_core/platform/service/memory_manager/src/sl_memory_manager_pool.o: $(SDK_PATH)/platform_core/platform/service/memory_manager/src/sl_memory_manager_pool.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/service/memory_manager/src/sl_memory_manager_pool.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/service/memory_manager/src/sl_memory_manager_pool.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/service/memory_manager/src/sl_memory_manager_pool.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/service/memory_manager/src/sl_memory_manager_pool.o

$(OUTPUT_DIR)/sdk/platform_core/platform/service/memory_manager/src/sl_memory_manager_pool_common.o: $(SDK_PATH)/platform_core/platform/service/memory_manager/src/sl_memory_manager_pool_common.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/service/memory_manager/src/sl_memory_manager_pool_common.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/service/memory_manager/src/sl_memory_manager_pool_common.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/service/memory_manager/src/sl_memory_manager_pool_common.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/service/memory_manager/src/sl_memory_manager_pool_common.o

$(OUTPUT_DIR)/sdk/platform_core/platform/service/memory_manager/src/sl_memory_manager_region.o: $(SDK_PATH)/platform_core/platform/service/memory_manager/src/sl_memory_manager_region.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/service/memory_manager/src/sl_memory_manager_region.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/service/memory_manager/src/sl_memory_manager_region.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/service/memory_manager/src/sl_memory_manager_region.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/service/memory_manager/src/sl_memory_manager_region.o

$(OUTPUT_DIR)/sdk/platform_core/platform/service/memory_manager/src/sl_memory_manager_retarget.o: $(SDK_PATH)/platform_core/platform/service/memory_manager/src/sl_memory_manager_retarget.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/service/memory_manager/src/sl_memory_manager_retarget.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/service/memory_manager/src/sl_memory_manager_retarget.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/service/memory_manager/src/sl_memory_manager_retarget.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/service/memory_manager/src/sl_memory_manager_retarget.o

$(OUTPUT_DIR)/sdk/platform_core/platform/service/memory_manager/src/sli_memory_manager_common.o: $(SDK_PATH)/platform_core/platform/service/memory_manager/src/sli_memory_manager_common.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/platform_core/platform/service/memory_manager/src/sli_memory_manager_common.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/platform_core/platform/service/memory_manager/src/sli_memory_manager_common.c
CDEPS += $(OUTPUT_DIR)/sdk/platform_core/platform/service/memory_manager/src/sli_memory_manager_common.d
OBJS += $(OUTPUT_DIR)/sdk/platform_core/platform/service/memory_manager/src/sli_memory_manager_common.o

$(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/src/se_aes.o: $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/src/se_aes.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/src/se_aes.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/src/se_aes.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/src/se_aes.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/src/se_aes.o

$(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/src/sl_mbedtls.o: $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/src/sl_mbedtls.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/src/sl_mbedtls.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/src/sl_mbedtls.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/src/sl_mbedtls.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/src/sl_mbedtls.o

$(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_psa_driver_common.o: $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_psa_driver_common.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_psa_driver_common.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_psa_driver_common.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_psa_driver_common.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_psa_driver_common.o

$(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_psa_driver_init.o: $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_psa_driver_init.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_psa_driver_init.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_psa_driver_init.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_psa_driver_init.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_psa_driver_init.o

$(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_aead.o: $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_aead.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_aead.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_aead.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_aead.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_aead.o

$(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_builtin_keys.o: $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_builtin_keys.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_builtin_keys.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_builtin_keys.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_builtin_keys.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_builtin_keys.o

$(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_cipher.o: $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_cipher.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_cipher.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_cipher.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_cipher.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_cipher.o

$(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_key_derivation.o: $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_key_derivation.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_key_derivation.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_key_derivation.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_key_derivation.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_key_derivation.o

$(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_key_management.o: $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_key_management.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_key_management.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_key_management.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_key_management.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_key_management.o

$(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_mac.o: $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_mac.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_mac.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_mac.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_mac.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_mac.o

$(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_signature.o: $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_signature.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_signature.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_signature.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_signature.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_signature.o

$(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_driver_aead.o: $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_driver_aead.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_driver_aead.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_driver_aead.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_driver_aead.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_driver_aead.o

$(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_driver_cipher.o: $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_driver_cipher.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_driver_cipher.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_driver_cipher.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_driver_cipher.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_driver_cipher.o

$(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_driver_mac.o: $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_driver_mac.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_driver_mac.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_driver_mac.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_driver_mac.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_driver_mac.o

$(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_key_derivation.o: $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_key_derivation.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_key_derivation.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_key_derivation.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_key_derivation.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_key_derivation.o

$(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_aead.o: $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_aead.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_aead.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_aead.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_aead.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_aead.o

$(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_cipher.o: $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_cipher.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_cipher.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_cipher.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_cipher.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_cipher.o

$(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_hash.o: $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_hash.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_hash.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_hash.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_hash.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_hash.o

$(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_mac.o: $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_mac.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_mac.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_mac.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_mac.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_mac.o

$(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_key_derivation.o: $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_key_derivation.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_key_derivation.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_key_derivation.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_key_derivation.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_key_derivation.o

$(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_version_dependencies.o: $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_version_dependencies.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_version_dependencies.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_version_dependencies.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_version_dependencies.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_version_dependencies.o

$(OUTPUT_DIR)/sdk/security_mbedtls_source/library/aes.o: $(SDK_PATH)/security_mbedtls_source/library/aes.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls_source/library/aes.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls_source/library/aes.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls_source/library/aes.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls_source/library/aes.o

$(OUTPUT_DIR)/sdk/security_mbedtls_source/library/constant_time.o: $(SDK_PATH)/security_mbedtls_source/library/constant_time.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls_source/library/constant_time.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls_source/library/constant_time.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls_source/library/constant_time.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls_source/library/constant_time.o

$(OUTPUT_DIR)/sdk/security_mbedtls_source/library/platform.o: $(SDK_PATH)/security_mbedtls_source/library/platform.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls_source/library/platform.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls_source/library/platform.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls_source/library/platform.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls_source/library/platform.o

$(OUTPUT_DIR)/sdk/security_mbedtls_source/library/platform_util.o: $(SDK_PATH)/security_mbedtls_source/library/platform_util.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls_source/library/platform_util.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls_source/library/platform_util.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls_source/library/platform_util.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls_source/library/platform_util.o

$(OUTPUT_DIR)/sdk/security_mbedtls_source/library/psa_crypto_client.o: $(SDK_PATH)/security_mbedtls_source/library/psa_crypto_client.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls_source/library/psa_crypto_client.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls_source/library/psa_crypto_client.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls_source/library/psa_crypto_client.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls_source/library/psa_crypto_client.o

$(OUTPUT_DIR)/sdk/security_mbedtls_source/library/psa_util.o: $(SDK_PATH)/security_mbedtls_source/library/psa_util.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls_source/library/psa_util.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls_source/library/psa_util.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls_source/library/psa_util.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls_source/library/psa_util.o

$(OUTPUT_DIR)/sdk/security_mbedtls_source/library/threading.o: $(SDK_PATH)/security_mbedtls_source/library/threading.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_mbedtls_source/library/threading.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_mbedtls_source/library/threading.c
CDEPS += $(OUTPUT_DIR)/sdk/security_mbedtls_source/library/threading.d
OBJS += $(OUTPUT_DIR)/sdk/security_mbedtls_source/library/threading.o

$(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager.o: $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager.c
CDEPS += $(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager.d
OBJS += $(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager.o

$(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_attestation.o: $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_attestation.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_attestation.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_attestation.c
CDEPS += $(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_attestation.d
OBJS += $(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_attestation.o

$(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_cipher.o: $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_cipher.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_cipher.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_cipher.c
CDEPS += $(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_cipher.d
OBJS += $(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_cipher.o

$(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_entropy.o: $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_entropy.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_entropy.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_entropy.c
CDEPS += $(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_entropy.d
OBJS += $(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_entropy.o

$(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_hash.o: $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_hash.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_hash.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_hash.c
CDEPS += $(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_hash.d
OBJS += $(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_hash.o

$(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_key_derivation.o: $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_key_derivation.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_key_derivation.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_key_derivation.c
CDEPS += $(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_key_derivation.d
OBJS += $(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_key_derivation.o

$(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_key_handling.o: $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_key_handling.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_key_handling.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_key_handling.c
CDEPS += $(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_key_handling.d
OBJS += $(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_key_handling.o

$(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_signature.o: $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_signature.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_signature.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_signature.c
CDEPS += $(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_signature.d
OBJS += $(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_signature.o

$(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_util.o: $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_util.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_util.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_util.c
CDEPS += $(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_util.d
OBJS += $(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_util.o

$(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sli_se_manager_mailbox.o: $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sli_se_manager_mailbox.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sli_se_manager_mailbox.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_se_manager/platform/security/sl_component/se_manager/src/sli_se_manager_mailbox.c
CDEPS += $(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sli_se_manager_mailbox.d
OBJS += $(OUTPUT_DIR)/sdk/security_se_manager/platform/security/sl_component/se_manager/src/sli_se_manager_mailbox.o

$(OUTPUT_DIR)/sdk/security_tfm/lib/fih/src/fih.o: $(SDK_PATH)/security_tfm/lib/fih/src/fih.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_tfm/lib/fih/src/fih.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_tfm/lib/fih/src/fih.c
CDEPS += $(OUTPUT_DIR)/sdk/security_tfm/lib/fih/src/fih.d
OBJS += $(OUTPUT_DIR)/sdk/security_tfm/lib/fih/src/fih.o

$(OUTPUT_DIR)/sdk/security_tfm/platform/ext/target/siliconlabs/hse/sli_se.o: $(SDK_PATH)/security_tfm/platform/ext/target/siliconlabs/hse/sli_se.c
	@$(POSIX_TOOL_PATH)echo 'Building $(SDK_PATH)/security_tfm/platform/ext/target/siliconlabs/hse/sli_se.c'
	@$(POSIX_TOOL_PATH)mkdir -p $(@D)
	$(ECHO)$(CC) $(CFLAGS) -c -o $@ $(SDK_PATH)/security_tfm/platform/ext/target/siliconlabs/hse/sli_se.c
CDEPS += $(OUTPUT_DIR)/sdk/security_tfm/platform/ext/target/siliconlabs/hse/sli_se.d
OBJS += $(OUTPUT_DIR)/sdk/security_tfm/platform/ext/target/siliconlabs/hse/sli_se.o

# Automatically-generated Simplicity Studio Metadata
# Please do not edit or delete these lines!
# SIMPLICITY_STUDIO_METADATA=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END_SIMPLICITY_STUDIO_METADATA
# END OF METADATA