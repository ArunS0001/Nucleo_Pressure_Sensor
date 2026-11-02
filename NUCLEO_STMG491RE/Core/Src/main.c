/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ad7732_pressure.h"
#include <stdbool.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define AD7732_REG_REVISION 0x02U
#define AD7732_REG_TEST     0x03U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

COM_InitTypeDef BspCOMInit;
__IO uint32_t BspButtonState = BUTTON_RELEASED;
FDCAN_HandleTypeDef hfdcan1;

SPI_HandleTypeDef hspi1;

/* USER CODE BEGIN PV */
static bool ad7732Zeroed = false;     /* set true after the zero offset is stored */

FDCAN_TxHeaderTypeDef TxHeader;
FDCAN_RxHeaderTypeDef RxHeader; // <-- Ensure this is exactly 'RxHeader'
FDCAN_FilterTypeDef   sFilterConfig;


uint8_t TxData[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44};
uint8_t RxData[8];              // <-- Ensure this is an array of 8 bytes

// Global signaling flag: volatile prevents the compiler from optimizing it out
volatile uint8_t newMessageReady = 0;
volatile uint8_t ad7732ReadyFlag = 0;
volatile uint32_t ad7732InterruptCount = 0U;
uint32_t ad7732LastInterruptCount = 0U;
uint32_t buttonNextCanTxMs = 0U;
uint32_t canLedOffMs = 0U;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_FDCAN1_Init(void);
static void MX_SPI1_Init(void);
/* USER CODE BEGIN PFP */
static bool AD7732_Detect(uint8_t *revision);
static bool AD7732_ReadRevision(uint8_t *revision);
static bool AD7732_ReadRegister(uint8_t registerAddress, uint8_t *value);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_FDCAN1_Init();
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */

  // 1. Configure the Filter to strictly match ID 0x456
  sFilterConfig.IdType = FDCAN_STANDARD_ID;
  sFilterConfig.FilterIndex = 0;
  sFilterConfig.FilterType = FDCAN_FILTER_MASK;          // Switch from RANGE to MASK
  sFilterConfig.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
  sFilterConfig.FilterID1 = 0x456;                      // The target ID we want
  sFilterConfig.FilterID2 = 0x7FF;                      // The mask (0x7FF means look at every bit)

  if (HAL_FDCAN_ConfigFilter(&hfdcan1, &sFilterConfig) != HAL_OK)
  {
      Error_Handler();
  }
  /*
  // 1. Configure Rx Filter to accept all standard message IDs (0x000 to 0x7FF)
  sFilterConfig.IdType = FDCAN_STANDARD_ID;
  sFilterConfig.FilterIndex = 0;
  sFilterConfig.FilterType = FDCAN_FILTER_RANGE;
  sFilterConfig.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
  sFilterConfig.FilterID1 = 0x000;
  sFilterConfig.FilterID2 = 0x7FF;
  HAL_FDCAN_ConfigFilter(&hfdcan1, &sFilterConfig);*/

  // 2. Format the Transmission Header strictly for Classic CAN mode
  TxHeader.Identifier = 0x12;                  // CAN ID visible to Waveshare
  TxHeader.IdType = FDCAN_STANDARD_ID;
  TxHeader.TxFrameType = FDCAN_DATA_FRAME;
  TxHeader.DataLength = FDCAN_DLC_BYTES_8;       // Sending 8 bytes
  TxHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  TxHeader.BitRateSwitch = FDCAN_BRS_OFF;        // CRITICAL: Disable CAN-FD speed switching
  TxHeader.FDFormat = FDCAN_CLASSIC_CAN;         // CRITICAL: Force Classic CAN mode
  TxHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  TxHeader.MessageMarker = 0;

  // 3. Kickstart the CAN peripheral hardware
  HAL_FDCAN_Start(&hfdcan1);

  // Change this line to use _NEW_MESSAGE as well
  if (HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) != HAL_OK)
  {
      Error_Handler();
  }
  /* USER CODE END 2 */

  /* Initialize USER push-button, will be used to trigger an interrupt each time it's pressed.*/
  BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

  /* Initialize COM1 port (115200, 8 bits (7-bit data + 1 stop bit), no parity */
  BspCOMInit.BaudRate   = 115200;
  BspCOMInit.WordLength = COM_WORDLENGTH_8B;
  BspCOMInit.StopBits   = COM_STOPBITS_1;
  BspCOMInit.Parity     = COM_PARITY_NONE;
  BspCOMInit.HwFlowCtl  = COM_HWCONTROL_NONE;
  if (BSP_COM_Init(COM1, &BspCOMInit) != BSP_ERROR_NONE)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN BSP */

  uint8_t ad7732Revision = 0;
  if (AD7732_Detect(&ad7732Revision))
  {
    printf("AD7732 detected (revision register: 0x%02X)\r\n", ad7732Revision);

    uint8_t ad7732TestRegister = 0;
    if (AD7732_ReadRegister(AD7732_REG_TEST, &ad7732TestRegister))
    {
      printf("AD7732 manufacturing test register (0x03): 0x%02X\r\n",
             ad7732TestRegister);
    }
    else
    {
      printf("Failed to read AD7732 manufacturing test register\r\n");
    }
  }
  else
  {
    printf("AD7732 not detected; check power, wiring, CS, and SPI settings\r\n");
  }

  /* -- Sample board code to send message over COM1 port ---- */
  printf("Welcome to STM32 world --- !\n\r");

  /* -- Sample board code to switch on led ---- */

  bool ad7732Initialized = ad7732_init();
  if (ad7732Initialized)
  {
	  printf ("AD7732 initialization successful \r\n");
  }
  else
  {
	  printf ("AD7732 initialization fail \r\n");
  }
  BSP_LED_On(LED_GREEN);

  uint32_t ad7732LastReadMs = HAL_GetTick();
  /* USER CODE END BSP */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  uint32_t nowMs = HAL_GetTick();
	  if ((canLedOffMs != 0U) &&
	      ((uint32_t)(nowMs - canLedOffMs) >= 2000U))
	  {
		  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
		  canLedOffMs = 0U;
	  }
	  if (ad7732Initialized && ((uint32_t)(nowMs - ad7732LastReadMs) >= 1000U))
	  {
		  ad7732_stats_t st;
		  ad7732LastReadMs = nowMs;

		  if (ad7732_take_stats(&st))
		  {
			  uint32_t interruptCount = ad7732InterruptCount;
			  uint32_t interruptDelta = interruptCount - ad7732LastInterruptCount;
			  ad7732LastInterruptCount = interruptCount;

			  /* Optional: take the first window as 0 psi.
	         	 Only valid if the sensor is at zero pressure and has warmed up. */
			  if (!ad7732Zeroed)
			  {
				  ad7732_set_zero(st.raw_mean);
				  ad7732Zeroed = true;
			  }

			  long minMpsi  = (long)(ad7732_raw_to_psi(st.raw_min)  * 1000.0);
			  long maxMpsi  = (long)(ad7732_raw_to_psi(st.raw_max)  * 1000.0);
			  long meanMpsi = (long)(ad7732_raw_to_psi(st.raw_mean) * 1000.0);

			  printf("AD7732 irq=%lu, delta=%lu, n=%lu, min=%ld, max=%ld, mean=%ld mpsi, pp=%ld mpsi, ADCpk=%ld mV, flags=0x%02X\r\n",
	             (unsigned long)interruptCount, (unsigned long)interruptDelta, (unsigned long)st.n,
	             minMpsi, maxMpsi, meanMpsi,
	             maxMpsi - minMpsi,
	             (long)(ad7732_raw_to_volts(st.raw_max) * 1000.0),
	             st.flags);
		  }
		  else
		  {
			  printf("AD7732: no conversions received (check RDY wiring and EXTI)\r\n");
		  }
	  }

    /* -- Sample board code for User push-button in interrupt mode ---- */
    if (BspButtonState == BUTTON_PRESSED)
    {
      /* Update button state */
      BspButtonState = BUTTON_RELEASED;
      if ((int32_t)(HAL_GetTick() - buttonNextCanTxMs) < 0)
      {
        continue;
      }
      /* -- Sample board code to toggle led ---- */
      BSP_LED_Toggle(LED_GREEN);

      printf ("CAN HELLO \r\n");

      // Diagnostic safety block: If HSI clock drifts and forces a Bus-Off, reset the line
      if (hfdcan1.Instance->PSR & FDCAN_PSR_BO)
      {
          HAL_FDCAN_Stop(&hfdcan1);
          HAL_FDCAN_Start(&hfdcan1);
      }

      // Push the data frame onto the physical CAN bus lines
      //in Waveshare configure set filter id = 0x12, and mask id as 0x7FF.
      //On pressing User blue button, aa bb cc dd 11 22 33 44 will be visible in Waveshare.
      if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &TxHeader, TxData) == HAL_OK)
      {
          HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5); // Toggle Nucleo Green LED on a successful queue
          buttonNextCanTxMs = HAL_GetTick() + 1000U;
      }
      /* ..... Perform your action ..... */
    }

    // Check if the background interrupt flag has signaled a fresh arrival
        if (newMessageReady == 1)
        {
            // Lower the flag immediately so we don't process the same packet twice
            newMessageReady = 0;

            // Example logic: Extract payload parameters safely inside main context
            uint8_t commandByte = RxData[0];
            uint8_t valueByte   = RxData[1];

            // If the Waveshare adapter sent 0x99 as the first data byte, do something special
            //From Waveshare, configure 1 Mbits (Set and Start) in must be configured,
            //and enter id(Hex) = 456 and data 99 11 22 33 44 55 66 77 in manually send.
            if (commandByte == 0x99)
            {
                // You can use regular safe delays or intensive logic here!
                HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);
                canLedOffMs = HAL_GetTick();
                printf ("New CAN message \r\n");
            }
        }

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief FDCAN1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_FDCAN1_Init(void)
{

  /* USER CODE BEGIN FDCAN1_Init 0 */

  /* USER CODE END FDCAN1_Init 0 */

  /* USER CODE BEGIN FDCAN1_Init 1 */

  /* USER CODE END FDCAN1_Init 1 */
  hfdcan1.Instance = FDCAN1;
  hfdcan1.Init.ClockDivider = FDCAN_CLOCK_DIV1;
  hfdcan1.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
  hfdcan1.Init.Mode = FDCAN_MODE_NORMAL;
  hfdcan1.Init.AutoRetransmission = DISABLE;
  hfdcan1.Init.TransmitPause = DISABLE;
  hfdcan1.Init.ProtocolException = DISABLE;
  hfdcan1.Init.NominalPrescaler = 1;
  hfdcan1.Init.NominalSyncJumpWidth = 2;
  hfdcan1.Init.NominalTimeSeg1 = 12;
  hfdcan1.Init.NominalTimeSeg2 = 3;
  hfdcan1.Init.DataPrescaler = 1;
  hfdcan1.Init.DataSyncJumpWidth = 1;
  hfdcan1.Init.DataTimeSeg1 = 1;
  hfdcan1.Init.DataTimeSeg2 = 1;
  hfdcan1.Init.StdFiltersNbr = 0;
  hfdcan1.Init.ExtFiltersNbr = 0;
  hfdcan1.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
  if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN FDCAN1_Init 2 */

  /* USER CODE END FDCAN1_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_HIGH;
  hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(ADC_CS_GPIO_Port, ADC_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIO_1_GPIO_Port, GPIO_1_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : ADC_CS_Pin */
  GPIO_InitStruct.Pin = ADC_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ADC_CS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : ADC_RDY_Pin */
  GPIO_InitStruct.Pin = ADC_RDY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(ADC_RDY_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : GPIO_1_Pin */
  GPIO_InitStruct.Pin = GPIO_1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIO_1_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
static bool AD7732_ReadRevision(uint8_t *revision)
{
  return AD7732_ReadRegister(AD7732_REG_REVISION, revision);
}

static bool AD7732_ReadRegister(uint8_t registerAddress, uint8_t *value)
{
  uint8_t tx[2] = {(uint8_t)(0x40U | (registerAddress & 0x3FU)), 0x00U};
  uint8_t rx[2] = {0U, 0U};
  HAL_StatusTypeDef status;

  if (value == NULL)
  {
    return false;
  }

  HAL_GPIO_WritePin(ADC_CS_GPIO_Port, ADC_CS_Pin, GPIO_PIN_RESET);
  status = HAL_SPI_TransmitReceive(&hspi1, tx, rx, sizeof(tx), 100U);
  HAL_GPIO_WritePin(ADC_CS_GPIO_Port, ADC_CS_Pin, GPIO_PIN_SET);

  if (status != HAL_OK)
  {
    printf("AD7732 SPI register 0x%02X read failed (HAL status %d)\r\n",
           registerAddress, status);
    return false;
  }

  *value = rx[1];
  return true;
}

static bool AD7732_Detect(uint8_t *revision)
{
  uint8_t resetBytes[4] = {0xFFU, 0xFFU, 0xFFU, 0xFFU};
  uint8_t firstRevision;
  uint8_t secondRevision;

  if (revision == NULL)
  {
    return false;
  }

  HAL_GPIO_WritePin(ADC_CS_GPIO_Port, ADC_CS_Pin, GPIO_PIN_RESET);
  HAL_StatusTypeDef status = HAL_SPI_Transmit(&hspi1, resetBytes, sizeof(resetBytes), 100U);
  HAL_GPIO_WritePin(ADC_CS_GPIO_Port, ADC_CS_Pin, GPIO_PIN_SET);
  if (status != HAL_OK)
  {
    printf("AD7732 SPI interface reset failed (HAL status %d)\r\n", status);
    return false;
  }
  HAL_Delay(1U);

  for (uint8_t attempt = 0U; attempt < 3U; attempt++)
  {
    if (AD7732_ReadRevision(&firstRevision) &&
        AD7732_ReadRevision(&secondRevision) &&
        (firstRevision == secondRevision) &&
        ((firstRevision & 0x0FU) == 0x04U))
    {
      *revision = firstRevision;
      return true;
    }

    HAL_Delay(10U);
  }

  return false;
}


void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == ADC_RDY_Pin)
  {
    ad7732ReadyFlag = 1;
    ad7732InterruptCount++;
    ad7732_rdy_isr();
  }
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != 0)
    {
        if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &RxHeader, RxData) == HAL_OK)
        {
        	newMessageReady = 1;
        }
    }
}
/* USER CODE END 4 */

/**
  * @brief BSP Push Button callback
  * @param Button Specifies the pressed button
  * @retval None
  */
void BSP_PB_Callback(Button_TypeDef Button)
{
  if (Button == BUTTON_USER)
  {
    BspButtonState = BUTTON_PRESSED;
  }
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
