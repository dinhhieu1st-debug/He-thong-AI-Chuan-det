####################################################################
# Automatically-generated file. Do not edit!                       #
####################################################################

set(SDK_PATH "C:/Users/admin/.silabs/slt/installs/conan/p/simpl35774a752829c/p")
set(COPIED_SDK_PATH "simplicity_sdk_2025.12.3")
set(PKG_PATH "C:/Users/admin/.silabs/slt/installs")

add_library(slc OBJECT
    "${SDK_PATH}/bootloader/platform/bootloader/core/btl_bootload.c"
    "${SDK_PATH}/bootloader/platform/bootloader/core/btl_core.c"
    "${SDK_PATH}/bootloader/platform/bootloader/core/btl_main.c"
    "${SDK_PATH}/bootloader/platform/bootloader/core/btl_parse.c"
    "${SDK_PATH}/bootloader/platform/bootloader/core/btl_reset.c"
    "${SDK_PATH}/bootloader/platform/bootloader/core/flash/btl_internal_flash.c"
    "${SDK_PATH}/bootloader/platform/bootloader/debug/btl_debug.c"
    "${SDK_PATH}/bootloader/platform/bootloader/debug/btl_debug_swo.c"
    "${SDK_PATH}/bootloader/platform/bootloader/driver/btl_driver_util.c"
    "${SDK_PATH}/bootloader/platform/bootloader/parser/gbl/btl_gbl_custom_tags.c"
    "${SDK_PATH}/bootloader/platform/bootloader/parser/gbl/btl_gbl_format.c"
    "${SDK_PATH}/bootloader/platform/bootloader/parser/gbl/btl_gbl_parser.c"
    "${SDK_PATH}/bootloader/platform/bootloader/security/btl_crc16.c"
    "${SDK_PATH}/bootloader/platform/bootloader/security/btl_crc32.c"
    "${SDK_PATH}/bootloader/platform/bootloader/security/btl_security_aes.c"
    "${SDK_PATH}/bootloader/platform/bootloader/security/btl_security_ecdsa.c"
    "${SDK_PATH}/bootloader/platform/bootloader/security/btl_security_sha256.c"
    "${SDK_PATH}/bootloader/platform/bootloader/security/btl_security_tokens.c"
    "${SDK_PATH}/bootloader/platform/bootloader/security/ecc/ecc.c"
    "${SDK_PATH}/bootloader/platform/bootloader/security/sha/btl_sha256.c"
    "${SDK_PATH}/bootloader/platform/bootloader/security/sha/crypto_sha.c"
    "${SDK_PATH}/bootloader/platform/bootloader/security/sha/cryptoacc_sha.c"
    "${SDK_PATH}/bootloader/platform/bootloader/security/sha/se_sha.c"
    "${SDK_PATH}/bootloader/platform/bootloader/storage/bootloadinfo/btl_storage_bootloadinfo.c"
    "${SDK_PATH}/bootloader/platform/bootloader/storage/btl_storage.c"
    "${SDK_PATH}/bootloader/platform/bootloader/storage/btl_storage_library.c"
    "${SDK_PATH}/bootloader/platform/bootloader/storage/internal_flash/btl_storage_internal_flash.c"
    "${SDK_PATH}/bootloader/platform/bootloader/storage/internal_flash/btl_storage_internal_flash_raw.c"
    "${SDK_PATH}/devices/platform/Device/SiliconLabs/EFR32MG26/Source/startup_efr32mg26.c"
    "${SDK_PATH}/devices/platform/Device/SiliconLabs/EFR32MG26/Source/system_efr32mg26.c"
    "${SDK_PATH}/platform_common/platform/common/src/sl_assert.c"
    "${SDK_PATH}/platform_common/platform/common/src/sl_syscalls.c"
    "${SDK_PATH}/platform_core/platform/common/src/sl_core_cortexm.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_acmp.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_burtc.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_cmu.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_dbg.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_emu.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_eusart.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_gpcrc.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_gpio.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_i2c.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_iadc.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_lcd.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_ldma.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_letimer.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_msc.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_opamp.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_pcnt.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_prs.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_rmu.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_system.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_timer.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_usart.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_vdac.c"
    "${SDK_PATH}/platform_core/platform/emlib/src/em_wdog.c"
    "${SDK_PATH}/platform_core/platform/service/memory_manager/src/sl_memory_manager.c"
    "${SDK_PATH}/platform_core/platform/service/memory_manager/src/sl_memory_manager_dynamic_reservation.c"
    "${SDK_PATH}/platform_core/platform/service/memory_manager/src/sl_memory_manager_pool.c"
    "${SDK_PATH}/platform_core/platform/service/memory_manager/src/sl_memory_manager_pool_common.c"
    "${SDK_PATH}/platform_core/platform/service/memory_manager/src/sl_memory_manager_region.c"
    "${SDK_PATH}/platform_core/platform/service/memory_manager/src/sl_memory_manager_retarget.c"
    "${SDK_PATH}/platform_core/platform/service/memory_manager/src/sli_memory_manager_common.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/src/se_aes.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/src/sl_mbedtls.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_psa_driver_common.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_psa_driver_init.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_aead.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_builtin_keys.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_cipher.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_key_derivation.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_key_management.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_mac.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_driver_signature.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_driver_aead.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_driver_cipher.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_driver_mac.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_opaque_key_derivation.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_aead.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_cipher.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_hash.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_driver_mac.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_transparent_key_derivation.c"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/src/sli_se_version_dependencies.c"
    "${SDK_PATH}/security_mbedtls_source/library/aes.c"
    "${SDK_PATH}/security_mbedtls_source/library/constant_time.c"
    "${SDK_PATH}/security_mbedtls_source/library/platform.c"
    "${SDK_PATH}/security_mbedtls_source/library/platform_util.c"
    "${SDK_PATH}/security_mbedtls_source/library/psa_crypto_client.c"
    "${SDK_PATH}/security_mbedtls_source/library/psa_util.c"
    "${SDK_PATH}/security_mbedtls_source/library/threading.c"
    "${SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager.c"
    "${SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_attestation.c"
    "${SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_cipher.c"
    "${SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_entropy.c"
    "${SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_hash.c"
    "${SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_key_derivation.c"
    "${SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_key_handling.c"
    "${SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_signature.c"
    "${SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_util.c"
    "${SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sli_se_manager_mailbox.c"
    "${SDK_PATH}/security_tfm/lib/fih/src/fih.c"
    "${SDK_PATH}/security_tfm/platform/ext/target/siliconlabs/hse/sli_se.c"
)

target_include_directories(slc PUBLIC
   "../config"
   "../autogen"
    "${SDK_PATH}/devices/platform/Device/SiliconLabs/EFR32MG26/Include"
    "${SDK_PATH}/platform_common/platform/common/inc"
    "${SDK_PATH}/bootloader/platform/bootloader"
    "${SDK_PATH}/bootloader/platform/bootloader/api"
    "${SDK_PATH}/bootloader/platform/bootloader/debug"
    "${SDK_PATH}/bootloader/platform/bootloader/parser"
    "${SDK_PATH}/bootloader/platform/bootloader/core/flash"
    "${SDK_PATH}/bootloader/platform/bootloader/security"
    "${SDK_PATH}/cmsis/Core/Include"
    "${SDK_PATH}/platform_core/platform/emlib/inc"
    "${SDK_PATH}/platform_core/platform/common/errno_error_codes/inc"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/config"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/config/preset"
    "${SDK_PATH}/security_mbedtls_source/include"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_mbedtls_support/inc"
    "${SDK_PATH}/security_mbedtls_source/library"
    "${SDK_PATH}/platform_core/platform/service/memory_manager/inc"
    "${SDK_PATH}/platform_core/platform/service/memory_manager/src"
    "${SDK_PATH}/security_mbedtls/platform/security/sl_component/sl_psa_driver/inc"
    "${SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/inc"
    "${SDK_PATH}/platform_core/platform/common/inc"
    "${SDK_PATH}/security_tfm/lib/fih/inc"
    "${SDK_PATH}/security_tfm/platform/include"
    "${SDK_PATH}/security_se_manager/platform/security/sl_component/sli_psec_osal/inc"
)

target_compile_definitions(slc PUBLIC
    "EFR32MG26B510F3200IM48=1"
    "SL_CODE_COMPONENT_SYSTEM=system"
    "SE_MANAGER_CONFIG_FILE=\"btl_aes_ctr_stream_block_cfg.h\""
    "BOOTLOADER_ENABLE=1"
    "BOOTLOADER_SECOND_STAGE=1"
    "SL_RAMFUNC_DISABLE=1"
    "__START=main"
    "__STARTUP_CLEAR_BSS=1"
    "SYSTEM_NO_STATIC_MEMORY=1"
    "BOOTLOADER_SUPPORT_INTERNAL_STORAGE=1"
    "BOOTLOADER_SUPPORT_STORAGE=1"
    "HARDWARE_BOARD_DEFAULT_RF_BAND_2400=1"
    "HARDWARE_BOARD_SUPPORTS_1_RF_BAND=1"
    "HARDWARE_BOARD_SUPPORTS_RF_BAND_2400=1"
    "HFXO_FREQ=39000000"
    "SL_BOARD_NAME=\"BRD2709A\""
    "SL_BOARD_REV=\"A03\""
    "SL_COMPONENT_CATALOG_PRESENT=1"
    "MBEDTLS_CONFIG_FILE=<sl_mbedtls_trustzone_config.h>"
    "SL_CODE_COMPONENT_MEMORY_MANAGER=memory_manager"
    "MBEDTLS_PSA_CRYPTO_CONFIG_FILE=<psa_crypto_config.h>"
    "SL_CODE_COMPONENT_SE_MANAGER=se_manager"
    "SL_CODE_COMPONENT_CORE=core"
    "SL_CODE_COMPONENT_PSEC_OSAL=psec_osal"
    "SL_TRUSTZONE_SECURE=1"
)

target_link_libraries(slc PUBLIC
    "-Wl,--start-group"
    "gcc"
    "c"
    "m"
    "nosys"
    "-Wl,--end-group"
)
target_compile_options(slc PUBLIC
    $<$<COMPILE_LANGUAGE:C>:-mcpu=cortex-m33>
    $<$<COMPILE_LANGUAGE:C>:-mthumb>
    $<$<COMPILE_LANGUAGE:C>:-mfpu=fpv5-sp-d16>
    $<$<COMPILE_LANGUAGE:C>:-mfloat-abi=hard>
    $<$<COMPILE_LANGUAGE:C>:-mcmse>
    $<$<COMPILE_LANGUAGE:C>:-Wall>
    $<$<COMPILE_LANGUAGE:C>:-Wextra>
    $<$<COMPILE_LANGUAGE:C>:-Os>
    $<$<COMPILE_LANGUAGE:C>:-fdata-sections>
    $<$<COMPILE_LANGUAGE:C>:-ffunction-sections>
    $<$<COMPILE_LANGUAGE:C>:-fomit-frame-pointer>
    $<$<COMPILE_LANGUAGE:C>:-g>
    $<$<COMPILE_LANGUAGE:C>:--specs=nano.specs>
    $<$<COMPILE_LANGUAGE:C>:-Wno-ignored-qualifiers>
    $<$<COMPILE_LANGUAGE:C>:-Wno-sign-compare>
    $<$<COMPILE_LANGUAGE:C>:-fno-lto>
    $<$<COMPILE_LANGUAGE:CXX>:-mcpu=cortex-m33>
    $<$<COMPILE_LANGUAGE:CXX>:-mthumb>
    $<$<COMPILE_LANGUAGE:CXX>:-mfpu=fpv5-sp-d16>
    $<$<COMPILE_LANGUAGE:CXX>:-mfloat-abi=hard>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-mcmse>
    $<$<COMPILE_LANGUAGE:CXX>:-Wall>
    $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
    $<$<COMPILE_LANGUAGE:CXX>:-Os>
    $<$<COMPILE_LANGUAGE:CXX>:-fdata-sections>
    $<$<COMPILE_LANGUAGE:CXX>:-ffunction-sections>
    $<$<COMPILE_LANGUAGE:CXX>:-fomit-frame-pointer>
    $<$<COMPILE_LANGUAGE:CXX>:-g>
    $<$<COMPILE_LANGUAGE:CXX>:--specs=nano.specs>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-ignored-qualifiers>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-sign-compare>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-lto>
    $<$<COMPILE_LANGUAGE:ASM>:-mcpu=cortex-m33>
    $<$<COMPILE_LANGUAGE:ASM>:-mthumb>
    $<$<COMPILE_LANGUAGE:ASM>:-mfpu=fpv5-sp-d16>
    $<$<COMPILE_LANGUAGE:ASM>:-mfloat-abi=hard>
    "$<$<COMPILE_LANGUAGE:ASM>:SHELL:-x assembler-with-cpp>"
)

set(post_build_command ${POST_BUILD_EXE} postbuild "./bootloader-storage-internal-single-3200k.slpb" --parameter build_dir:"$<TARGET_FILE_DIR:bootloader-storage-internal-single-3200k>")
set_property(TARGET slc PROPERTY C_STANDARD 17)
set_property(TARGET slc PROPERTY CXX_STANDARD 17)
set_property(TARGET slc PROPERTY CXX_EXTENSIONS OFF)

target_link_options(slc INTERFACE
    -mcpu=cortex-m33
    -mthumb
    -mfpu=fpv5-sp-d16
    -mfloat-abi=hard
    "-T${CMAKE_CURRENT_LIST_DIR}/../autogen/linkerfile.ld"
    --specs=nano.specs
    -Wl,-Map=$<TARGET_FILE_DIR:bootloader-storage-internal-single-3200k>/bootloader-storage-internal-single-3200k.map
    "SHELL:-Wl,--wrap=_free_r -Wl,--wrap=_malloc_r -Wl,--wrap=_calloc_r -Wl,--wrap=_realloc_r"
    -fno-lto
    -Wl,--gc-sections
)

# BEGIN_SIMPLICITY_STUDIO_METADATA=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END_SIMPLICITY_STUDIO_METADATA