/*
 * main.c CHUAN cho project "empty" (bare-metal) - Simplicity SDK / xG26.
 * CHEP DE toan bo noi dung nay vao main.c cua project.
 * Diem mau chot: vong while(1) phai goi app_process_action().
 */

#include "sl_component_catalog.h"
#include "sl_main_init.h"            // SDK 2025.12: thay cho sl_system_init.h (doi cu)
#include "app.h"
#if defined(SL_CATALOG_KERNEL_PRESENT)
#include "sl_main_kernel.h"
#else
#include "sl_main_process_action.h"  // SDK 2025.12: thay cho sl_system_process_action.h
#endif

int main(void)
{
  // Khoi tao device, system, services, protocol stacks (ten moi: sl_main_init)
  sl_main_init();

  // Khoi tao ung dung (chay 1 lan) -> 2 dong printf khoi dong o day
  app_init();

#if defined(SL_CATALOG_KERNEL_PRESENT)
  // Neu project co RTOS (kernel): tac vu tao trong app_init() se chay.
  sl_main_kernel_start();
#else
  while (1) {
    // BAT BUOC giu lai: cho cac component cua Silicon Labs chay (ten moi)
    sl_main_process_action();

    // >>> VONG LAP UNG DUNG: goi ham xu ly lap lai (giong loop() Arduino) <<<
    app_process_action();
  }
#endif
}