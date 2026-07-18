####################################################################
# Automatically-generated file. Do not edit!                       #
####################################################################

set(SDK_PATH "/home/huynh/.silabs/slt/installs/conan/p/simpl35774a752829c/p")
set(COPIED_SDK_PATH "simplicity_sdk_2025.12.3")
set(PKG_PATH "/home/huynh/.silabs/slt/installs")

add_library(slc OBJECT
    "../${COPIED_SDK_PATH}/boards/hardware/board/src/sl_board_control_gpio.c"
    "../${COPIED_SDK_PATH}/boards/hardware/board/src/sl_board_init.c"
    "../${COPIED_SDK_PATH}/devices/platform/Device/SiliconLabs/EFR32MG26/Source/startup_efr32mg26.c"
    "../${COPIED_SDK_PATH}/devices/platform/Device/SiliconLabs/EFR32MG26/Source/system_efr32mg26.c"
    "../${COPIED_SDK_PATH}/platform_common/platform/common/src/sl_assert.c"
    "../${COPIED_SDK_PATH}/platform_common/platform/common/src/sl_slist.c"
    "../${COPIED_SDK_PATH}/platform_common/platform/common/src/sl_string.c"
    "../${COPIED_SDK_PATH}/platform_common/platform/common/src/sl_syscalls.c"
    "../${COPIED_SDK_PATH}/platform_core/hardware/driver/configuration_over_swo/src/sl_cos.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/common/src/sl_core_cortexm.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/driver/debug/src/sl_debug_swo.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/driver/gpio/src/sl_gpio.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/driver/i2c/src/sl_i2c.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/driver/i2cspm/src/sl_i2cspm.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/emdrv/dmadrv/src/dmadrv.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/emlib/src/em_burtc.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/emlib/src/em_cmu.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/emlib/src/em_emu.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/emlib/src/em_gpio.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/emlib/src/em_i2c.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/emlib/src/em_ldma.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/emlib/src/em_msc.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/emlib/src/em_prs.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/emlib/src/em_system.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/emlib/src/em_timer.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/emlib/src/em_usart.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/peripheral/src/sl_hal_gpio.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/peripheral/src/sl_hal_i2c.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/peripheral/src/sl_hal_syscfg.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/peripheral/src/sl_hal_sysrtc.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/peripheral/src/sl_hal_sysrtc_subsystem.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/peripheral/src/sl_hal_system.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/clock_manager/src/sl_clock_manager.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/clock_manager/src/sl_clock_manager_hal_s2.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/clock_manager/src/sl_clock_manager_init.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/clock_manager/src/sl_clock_manager_init_hal_s2.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/device_init/src/sl_device_init_dcdc_s2.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/device_init/src/sl_device_init_emu_s2.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/device_manager/clocks/sl_device_clock_efr32xg26.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/device_manager/devices/sl_device_peripheral_hal_efr32xg26.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/device_manager/dma/sl_device_dma_s2.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/device_manager/src/sl_device_clock.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/device_manager/src/sl_device_dma.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/device_manager/src/sl_device_gpio.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/device_manager/src/sl_device_peripheral.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/interrupt_manager/src/sl_interrupt_manager_cortexm.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/iostream/src/sl_iostream.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/iostream/src/sl_iostream_retarget_stdio.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/iostream/src/sl_iostream_uart.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/iostream/src/sl_iostream_usart.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/memory_manager/src/sl_memory_manager_region.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/sl_main/src/sl_main_init.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/sl_main/src/sl_main_init_memory.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/sl_main/src/sl_main_process_action.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/sleeptimer/src/sl_sleeptimer.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/sleeptimer/src/sl_sleeptimer_hal_burtc.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/sleeptimer/src/sl_sleeptimer_hal_sysrtc.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/sleeptimer/src/sl_sleeptimer_hal_timer.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/udelay/src/sl_udelay.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/udelay/src/sl_udelay_armv6m_gcc.S"
    "../${COPIED_SDK_PATH}/printf/printf.c"
    "../${COPIED_SDK_PATH}/printf/src/iostream_printf.c"
    "../app.c"
    "../autogen/sl_board_default_init.c"
    "../autogen/sl_event_handler.c"
    "../autogen/sl_i2cspm_init.c"
    "../autogen/sl_iostream_handles.c"
    "../autogen/sl_iostream_init_usart_instances.c"
    "../main.c"
)

target_include_directories(slc PUBLIC
   "../config"
   "../autogen"
   "../."
    "../${COPIED_SDK_PATH}/devices/platform/Device/SiliconLabs/EFR32MG26/Include"
    "../${COPIED_SDK_PATH}/platform_common/platform/common/inc"
    "../${COPIED_SDK_PATH}/boards/hardware/board/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/clock_manager/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/clock_manager/src"
    "../${COPIED_SDK_PATH}/cmsis/Core/Include"
    "../${COPIED_SDK_PATH}/platform_core/hardware/driver/configuration_over_swo/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/driver/debug/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/device_manager/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/device_init/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/emdrv/dmadrv/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/emdrv/dmadrv/inc/s2_signals"
    "../${COPIED_SDK_PATH}/platform_core/platform/emdrv/common/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/emlib/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/driver/gpio/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/peripheral/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/driver/i2c/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/driver/i2c/src"
    "../${COPIED_SDK_PATH}/platform_core/platform/driver/i2cspm/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/interrupt_manager/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/interrupt_manager/src"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/interrupt_manager/inc/arm"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/iostream/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/memory_manager/inc"
    "../${COPIED_SDK_PATH}/printf"
    "../${COPIED_SDK_PATH}/printf/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/common/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/sl_main/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/sl_main/src"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/sleeptimer/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/sleeptimer/src"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/udelay/inc"
)

target_compile_definitions(slc PUBLIC
    "DEBUG_EFM=1"
    "EFR32MG26B510F3200IM48=1"
    "SL_CODE_COMPONENT_SYSTEM=system"
    "HARDWARE_BOARD_DEFAULT_RF_BAND_2400=1"
    "HARDWARE_BOARD_SUPPORTS_1_RF_BAND=1"
    "HARDWARE_BOARD_SUPPORTS_RF_BAND_2400=1"
    "HFXO_FREQ=39000000"
    "SL_BOARD_NAME=\"BRD2709A\""
    "SL_BOARD_REV=\"A03\""
    "SL_CODE_COMPONENT_CLOCK_MANAGER=clock_manager"
    "SL_COMPONENT_CATALOG_PRESENT=1"
    "SL_CODE_COMPONENT_DEVICE_PERIPHERAL=device_peripheral"
    "SL_CODE_COMPONENT_DMADRV=dmadrv"
    "SL_CODE_COMPONENT_GPIO=gpio"
    "SL_CODE_COMPONENT_HAL_COMMON=hal_common"
    "SL_CODE_COMPONENT_HAL_GPIO=hal_gpio"
    "SL_CODE_COMPONENT_HAL_SYSRTC=hal_sysrtc"
    "SL_CODE_COMPONENT_INTERRUPT_MANAGER=interrupt_manager"
    "CMSIS_NVIC_VIRTUAL=1"
    "CMSIS_NVIC_VIRTUAL_HEADER_FILE=\"cmsis_nvic_virtual.h\""
    "SL_CODE_COMPONENT_CORE=core"
    "SL_CODE_COMPONENT_SLEEPTIMER=sleeptimer"
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
    $<$<COMPILE_LANGUAGE:C>:-fno-builtin-printf>
    $<$<COMPILE_LANGUAGE:C>:-fno-builtin-sprintf>
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
    $<$<COMPILE_LANGUAGE:CXX>:-fno-builtin-printf>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-builtin-sprintf>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-lto>
    $<$<COMPILE_LANGUAGE:ASM>:-mcpu=cortex-m33>
    $<$<COMPILE_LANGUAGE:ASM>:-mthumb>
    $<$<COMPILE_LANGUAGE:ASM>:-mfpu=fpv5-sp-d16>
    $<$<COMPILE_LANGUAGE:ASM>:-mfloat-abi=hard>
    "$<$<COMPILE_LANGUAGE:ASM>:SHELL:-x assembler-with-cpp>"
)

set(post_build_command )
set_property(TARGET slc PROPERTY C_STANDARD 17)
set_property(TARGET slc PROPERTY CXX_STANDARD 17)
set_property(TARGET slc PROPERTY CXX_EXTENSIONS OFF)

target_link_options(slc INTERFACE
    -mcpu=cortex-m33
    -mthumb
    -mfpu=fpv5-sp-d16
    -mfloat-abi=hard
    -T${CMAKE_CURRENT_LIST_DIR}/../autogen/linkerfile.ld
    --specs=nano.specs
    "SHELL:-Xlinker -Map=$<TARGET_FILE_DIR:empty_3>/empty_3.map"
    -fno-lto
    -Wl,--gc-sections
)

# BEGIN_SIMPLICITY_STUDIO_METADATA=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END_SIMPLICITY_STUDIO_METADATA