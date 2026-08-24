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
    "SLI_BUILT_WITH_LLVM=1"
    "SL_TRUSTZONE_SECURE=1"
)

target_link_libraries(slc PUBLIC
    "-Wl,--start-group"
    "c"
    "m"
    "nosys"
    "crt0-nosys"
    "-Wl,--end-group"
)
target_compile_options(slc PUBLIC
    "$<$<COMPILE_LANGUAGE:C>:SHELL:--target=arm-none-eabi -mcpu=cortex-m33>"
    $<$<COMPILE_LANGUAGE:C>:-mthumb>
    $<$<COMPILE_LANGUAGE:C>:-mfpu=fpv5-sp-d16>
    $<$<COMPILE_LANGUAGE:C>:-mfloat-abi=hard>
    $<$<COMPILE_LANGUAGE:C>:-mcmse>
    $<$<COMPILE_LANGUAGE:C>:-Wall>
    $<$<COMPILE_LANGUAGE:C>:-Wextra>
    $<$<COMPILE_LANGUAGE:C>:-Oz>
    $<$<COMPILE_LANGUAGE:C>:-fdata-sections>
    $<$<COMPILE_LANGUAGE:C>:-ffunction-sections>
    $<$<COMPILE_LANGUAGE:C>:-fomit-frame-pointer>
    $<$<COMPILE_LANGUAGE:C>:-g>
    $<$<COMPILE_LANGUAGE:C>:--config=newlib-nano.cfg>
    "$<$<COMPILE_LANGUAGE:CXX>:SHELL:--target=arm-none-eabi -mcpu=cortex-m33>"
    $<$<COMPILE_LANGUAGE:CXX>:-mthumb>
    $<$<COMPILE_LANGUAGE:CXX>:-mfpu=fpv5-sp-d16>
    $<$<COMPILE_LANGUAGE:CXX>:-mfloat-abi=hard>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-mcmse>
    $<$<COMPILE_LANGUAGE:CXX>:-Wall>
    $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
    $<$<COMPILE_LANGUAGE:CXX>:-Oz>
    $<$<COMPILE_LANGUAGE:CXX>:-fdata-sections>
    $<$<COMPILE_LANGUAGE:CXX>:-ffunction-sections>
    $<$<COMPILE_LANGUAGE:CXX>:-fomit-frame-pointer>
    $<$<COMPILE_LANGUAGE:CXX>:-g>
    $<$<COMPILE_LANGUAGE:CXX>:--config=newlib-nano.cfg>
    "$<$<COMPILE_LANGUAGE:ASM>:SHELL:--target=arm-none-eabi -mcpu=cortex-m33>"
    $<$<COMPILE_LANGUAGE:ASM>:-mthumb>
    $<$<COMPILE_LANGUAGE:ASM>:-mfpu=fpv5-sp-d16>
    $<$<COMPILE_LANGUAGE:ASM>:-mfloat-abi=hard>
    $<$<COMPILE_LANGUAGE:ASM>:--config=newlib-nano.cfg>
    "$<$<COMPILE_LANGUAGE:ASM>:SHELL:-x assembler-with-cpp>"
)

set(post_build_command ${POST_BUILD_EXE} postbuild "./bootloader-storage-internal-single-3200k.slpb" --parameter build_dir:"$<TARGET_FILE_DIR:bootloader-storage-internal-single-3200k>")
set_property(TARGET slc PROPERTY C_STANDARD 17)
set_property(TARGET slc PROPERTY CXX_STANDARD 17)
set_property(TARGET slc PROPERTY CXX_EXTENSIONS OFF)

target_link_options(slc INTERFACE
    "SHELL:--target=arm-none-eabi -mcpu=cortex-m33"
    -mthumb
    -mfpu=fpv5-sp-d16
    -mfloat-abi=hard
    "-T${CMAKE_CURRENT_LIST_DIR}/../autogen/linkerfile.ld"
    --config=newlib-nano.cfg
    -Oz
    -Wl,-Map=$<TARGET_FILE_DIR:bootloader-storage-internal-single-3200k>/bootloader-storage-internal-single-3200k.map
    -nostartfiles
    "SHELL:-Wl,--wrap=_free_r -Wl,--wrap=_malloc_r -Wl,--wrap=_calloc_r -Wl,--wrap=_realloc_r"
    "SHELL:-Xlinker --gc-sections"
)

# BEGIN_SIMPLICITY_STUDIO_METADATA=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=END_SIMPLICITY_STUDIO_METADATA