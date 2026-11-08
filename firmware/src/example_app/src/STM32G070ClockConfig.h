#pragma once

#include <Arduino.h>

/**
 * @brief Configure STM32G070RBT6 system clock to its maximum 64MHz
 * 
 * Configuration:
 * - HSE: 16MHz external crystal (HSE_VALUE set in platformio.ini)
 * - PLLM (divider): /1
 * - PLLN (multiplier): 8
 * - PLLR (output divider): /2
 * - Result: 16MHz -> 16MHz -> 128MHz (VCO) -> 64MHz (SYSCLK)
 * - AHB/APB: /1, voltage scaling: Range 1, Flash: 2 wait states
 * - Called automatically by the STM32 Arduino core before setup()
 */
void SystemClock_Config(void);

/**
 * @brief Print current system clock frequencies
 * 
 * Outputs:
 * - HSE status
 * - SYSCLK frequency
 * - HCLK frequency
 * - PCLK1 frequency
 */
void printSystemClockInfo(void);
