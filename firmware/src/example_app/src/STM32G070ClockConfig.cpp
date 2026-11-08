#include "STM32G070ClockConfig.h"

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV1;
  RCC_OscInitStruct.PLL.PLLN = 8; // 16 MHz / 1 * 8 = 128 MHz VCO
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enables the Clock Security System
  */
  HAL_RCC_EnableCSS();
}


void printSystemClockInfo(void)
{
    // Get clock frequencies
    uint32_t sysclk_freq = HAL_RCC_GetSysClockFreq();
    uint32_t hclk_freq = HAL_RCC_GetHCLKFreq();
    uint32_t pclk1_freq = HAL_RCC_GetPCLK1Freq();

    Serial.println("\n========== STM32G070 Clock Configuration ==========");
    
    // Print SYSCLK frequency
    Serial.print("SYSCLK Frequency: ");
    Serial.print(sysclk_freq / 1000000);
    Serial.print(" MHz (");
    Serial.print(sysclk_freq);
    Serial.println(" Hz)");

    // Print HCLK (AHB) frequency
    Serial.print("HCLK Frequency (AHB): ");
    Serial.print(hclk_freq / 1000000);
    Serial.print(" MHz (");
    Serial.print(hclk_freq);
    Serial.println(" Hz)");

    // Print PCLK1 (APB1) frequency
    Serial.print("PCLK1 Frequency (APB1): ");
    Serial.print(pclk1_freq / 1000000);
    Serial.print(" MHz (");
    Serial.print(pclk1_freq);
    Serial.println(" Hz)");

    // Print PLL configuration info
    Serial.println("\nPLL Configuration:");
    Serial.println("  Input (HSE): 16 MHz");
    Serial.println("  PLLM: /1 (16 MHz)");
    Serial.println("  PLLN: 8 (VCO = 128 MHz)");
    Serial.println("  PLLR: /2 (SYSCLK = 64 MHz)");

    // Print oscillator status
    Serial.println("\nOscillator Status:");
    Serial.print("  HSE: ");
    if ((RCC->CR & RCC_CR_HSERDY) != 0)
    {
        Serial.println("ON (Ready)");
    }
    else
    {
        Serial.println("OFF or Not Ready");
    }

    Serial.print("  PLL: ");
    if ((RCC->CR & RCC_CR_PLLRDY) != 0)
    {
        Serial.println("ON (Ready)");
    }
    else
    {
        Serial.println("OFF or Not Ready");
    }

    Serial.println("===================================================\n");
}
