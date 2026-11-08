Copied CMSIS headers so the bootloader builds on any PC without depending on
installed package versions (premortem item 10, 2026-11-08).

- `stm32g070xx.h`, `system_stm32g0xx.h`: ST CMSIS device headers, from
  framework-arduinoststm32 4.21200.0 (STM32 Arduino core 2.12). License: LICENSE-ST.md (Apache-2.0).
- `core_cm0plus.h`, `cmsis_compiler.h`, `cmsis_gcc.h`, `cmsis_version.h`, `mpu_armv7.h`:
  Arm CMSIS 5.9.0 core headers, from framework-cmsis 2.50900.0. License: LICENSE-ARM.txt (Apache-2.0).
