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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define BUTTON_DEBOUNCE_MS 200U
#define UART_LOG_QUEUE_DEPTH 16U
#define UART_LOG_MSG_MAX_LEN 96U
#define UART_RX_FIFO_DEPTH 32U
#define UART_CONSOLE_LINE_MAX_LEN 64U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

UART_HandleTypeDef huart3;

/* USER CODE BEGIN PV */
volatile uint32_t g_blink_ms = 500;
volatile uint8_t g_button_event = 0;
volatile uint32_t g_next_toggle_tick = 0;
volatile uint32_t g_last_button_tick = 0;
static char g_uart_log_queue[UART_LOG_QUEUE_DEPTH][UART_LOG_MSG_MAX_LEN];
volatile uint8_t g_uart_log_head = 0;
volatile uint8_t g_uart_log_tail = 0;
volatile uint8_t g_uart_tx_busy = 0;
volatile uint8_t g_uart_rx_byte = 0;
static uint8_t g_uart_rx_fifo[UART_RX_FIFO_DEPTH];
volatile uint8_t g_uart_rx_head = 0;
volatile uint8_t g_uart_rx_tail = 0;
volatile uint8_t g_uart_rx_overflow = 0;
static char g_console_line[UART_CONSOLE_LINE_MAX_LEN];
static char g_console_cmd[UART_CONSOLE_LINE_MAX_LEN];
uint8_t g_console_line_len = 0;
volatile uint8_t g_console_cmd_ready = 0;
uint32_t g_console_baud_rate = 115200U;
const char *g_console_format_name = "8N1";
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_ICACHE_Init(void);
static void MX_USART3_UART_Init(void);
/* USER CODE BEGIN PFP */
static uint8_t UartLogNextIndex(uint8_t index);
static uint8_t UartRxNextIndex(uint8_t index);
static void UartLogStartTxIfIdle(void);
static void UartWaitForTxDrain(void);
static void LogUart(const char *msg);
static void StartConsoleRx(void);
static uint8_t UartRxPopByte(uint8_t *byte);
static void ConsolePrintPrompt(void);
static void ConsolePrintHelp(void);
static void ConsolePrintCurrentUart(void);
static void ConsoleHandleByte(uint8_t byte);
static void ConsoleProcessRx(void);
static void ConsoleProcessCommand(void);
static uint8_t ConsoleTryParseFormat(const char *token,
                                     uint32_t *word_length,
                                     uint32_t *parity,
                                     uint32_t *stop_bits,
                                     const char **format_name);
static HAL_StatusTypeDef ConsoleApplyUartConfig(uint32_t baud_rate,
                                                uint32_t word_length,
                                                uint32_t parity,
                                                uint32_t stop_bits);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static uint8_t UartLogNextIndex(uint8_t index)
{
  return (uint8_t)((index + 1U) % UART_LOG_QUEUE_DEPTH);
}

static uint8_t UartRxNextIndex(uint8_t index)
{
  return (uint8_t)((index + 1U) % UART_RX_FIFO_DEPTH);
}

static void UartLogStartTxIfIdle(void)
{
  uint8_t tail_local;
  uint16_t len;
  HAL_StatusTypeDef status;

  __disable_irq();
  if ((g_uart_tx_busy != 0U) || (g_uart_log_head == g_uart_log_tail))
  {
    __enable_irq();
    return;
  }

  g_uart_tx_busy = 1U;
  tail_local = g_uart_log_tail;
  len = (uint16_t)strlen(g_uart_log_queue[tail_local]);
  __enable_irq();

  status = HAL_UART_Transmit_IT(&huart3, (uint8_t *)g_uart_log_queue[tail_local], len);
  if (status != HAL_OK)
  {
    __disable_irq();
    g_uart_tx_busy = 0U;
    __enable_irq();
  }
}

static void LogUart(const char *msg)
{
  uint8_t head_local;
  uint8_t next_head;

  if (msg == NULL)
  {
    return;
  }

  __disable_irq();
  head_local = g_uart_log_head;
  next_head = UartLogNextIndex(head_local);
  if (next_head == g_uart_log_tail)
  {
    __enable_irq();
    return;
  }

  (void)strncpy(g_uart_log_queue[head_local], msg, UART_LOG_MSG_MAX_LEN - 1U);
  g_uart_log_queue[head_local][UART_LOG_MSG_MAX_LEN - 1U] = '\0';
  g_uart_log_head = next_head;
  __enable_irq();

  UartLogStartTxIfIdle();
}

static void UartWaitForTxDrain(void)
{
  while (1)
  {
    uint8_t tx_idle;

    __disable_irq();
    tx_idle = (uint8_t)(((g_uart_tx_busy == 0U) && (g_uart_log_head == g_uart_log_tail)) ? 1U : 0U);
    __enable_irq();

    if (tx_idle != 0U)
    {
      break;
    }

    UartLogStartTxIfIdle();
  }
}

static void StartConsoleRx(void)
{
  if (HAL_UART_Receive_IT(&huart3, (uint8_t *)&g_uart_rx_byte, 1U) != HAL_OK)
  {
    Error_Handler();
  }
}

static uint8_t UartRxPopByte(uint8_t *byte)
{
  uint8_t tail_local;

  if (byte == NULL)
  {
    return 0U;
  }

  __disable_irq();
  if (g_uart_rx_head == g_uart_rx_tail)
  {
    __enable_irq();
    return 0U;
  }

  tail_local = g_uart_rx_tail;
  *byte = g_uart_rx_fifo[tail_local];
  g_uart_rx_tail = UartRxNextIndex(tail_local);
  __enable_irq();

  return 1U;
}

static void ConsolePrintPrompt(void)
{
  LogUart("> ");
}

static void ConsolePrintHelp(void)
{
  LogUart("Commands:\r\n");
  LogUart("  help\r\n");
  LogUart("  uart show\r\n");
  LogUart("  uart set <baud> <8n1|8e1|8o1|8n2>\r\n");
}

static void ConsolePrintCurrentUart(void)
{
  char msg[UART_LOG_MSG_MAX_LEN];

  (void)snprintf(msg,
                 sizeof(msg),
                 "UART console: %lu %s\r\n",
                 (unsigned long)g_console_baud_rate,
                 g_console_format_name);
  LogUart(msg);
}

static void ConsoleHandleByte(uint8_t byte)
{
  char echo[2];

  if ((byte == '\r') || (byte == '\n'))
  {
    if (g_console_line_len != 0U)
    {
      g_console_line[g_console_line_len] = '\0';
      if (g_console_cmd_ready == 0U)
      {
        (void)strncpy(g_console_cmd, g_console_line, sizeof(g_console_cmd) - 1U);
        g_console_cmd[sizeof(g_console_cmd) - 1U] = '\0';
        g_console_cmd_ready = 1U;
      }
      else
      {
        LogUart("\r\nCommand dropped: previous command still pending.\r\n");
      }

      g_console_line_len = 0U;
    }
    else
    {
      LogUart("\r\n");
      ConsolePrintPrompt();
      return;
    }

    LogUart("\r\n");
    return;
  }

  if ((byte == '\b') || (byte == 0x7FU))
  {
    if (g_console_line_len != 0U)
    {
      g_console_line_len--;
      LogUart("\b \b");
    }

    return;
  }

  if (isprint((int)byte) == 0)
  {
    return;
  }

  if (g_console_line_len >= (UART_CONSOLE_LINE_MAX_LEN - 1U))
  {
    return;
  }

  g_console_line[g_console_line_len] = (char)byte;
  g_console_line_len++;

  echo[0] = (char)byte;
  echo[1] = '\0';
  LogUart(echo);
}

static uint8_t ConsoleTryParseFormat(const char *token,
                                     uint32_t *word_length,
                                     uint32_t *parity,
                                     uint32_t *stop_bits,
                                     const char **format_name)
{
  char normalized[8];
  size_t idx;

  if ((token == NULL) || (word_length == NULL) || (parity == NULL) || (stop_bits == NULL) || (format_name == NULL))
  {
    return 0U;
  }

  for (idx = 0U; idx < (sizeof(normalized) - 1U); idx++)
  {
    if (token[idx] == '\0')
    {
      break;
    }

    normalized[idx] = (char)tolower((int)(unsigned char)token[idx]);
  }
  normalized[idx] = '\0';

  if (strcmp(normalized, "8n1") == 0)
  {
    *word_length = UART_WORDLENGTH_8B;
    *parity = UART_PARITY_NONE;
    *stop_bits = UART_STOPBITS_1;
    *format_name = "8N1";
    return 1U;
  }

  if (strcmp(normalized, "8e1") == 0)
  {
    *word_length = UART_WORDLENGTH_9B;
    *parity = UART_PARITY_EVEN;
    *stop_bits = UART_STOPBITS_1;
    *format_name = "8E1";
    return 1U;
  }

  if (strcmp(normalized, "8o1") == 0)
  {
    *word_length = UART_WORDLENGTH_9B;
    *parity = UART_PARITY_ODD;
    *stop_bits = UART_STOPBITS_1;
    *format_name = "8O1";
    return 1U;
  }

  if (strcmp(normalized, "8n2") == 0)
  {
    *word_length = UART_WORDLENGTH_8B;
    *parity = UART_PARITY_NONE;
    *stop_bits = UART_STOPBITS_2;
    *format_name = "8N2";
    return 1U;
  }

  return 0U;
}

static HAL_StatusTypeDef ConsoleApplyUartConfig(uint32_t baud_rate,
                                                uint32_t word_length,
                                                uint32_t parity,
                                                uint32_t stop_bits)
{
  (void)HAL_UART_AbortReceive_IT(&huart3);

  if (HAL_UART_DeInit(&huart3) != HAL_OK)
  {
    return HAL_ERROR;
  }

  huart3.Instance = USART3;
  huart3.Init.BaudRate = baud_rate;
  huart3.Init.WordLength = word_length;
  huart3.Init.StopBits = stop_bits;
  huart3.Init.Parity = parity;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (HAL_UARTEx_SetTxFifoThreshold(&huart3, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (HAL_UARTEx_SetRxFifoThreshold(&huart3, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (HAL_UARTEx_DisableFifoMode(&huart3) != HAL_OK)
  {
    return HAL_ERROR;
  }

  StartConsoleRx();
  return HAL_OK;
}

static void ConsoleProcessCommand(void)
{
  char *command;
  char *subcommand;
  char *baud_token;
  char *format_token;
  char *extra_token;
  char *end_ptr;
  char msg[UART_LOG_MSG_MAX_LEN];
  uint32_t baud_rate;
  uint32_t word_length;
  uint32_t parity;
  uint32_t stop_bits;
  const char *format_name;

  command = strtok(g_console_cmd, " ");
  if (command == NULL)
  {
    return;
  }

  if (strcmp(command, "help") == 0)
  {
    ConsolePrintHelp();
    return;
  }

  if (strcmp(command, "uart") != 0)
  {
    LogUart("Unknown command. Type 'help'.\r\n");
    return;
  }

  subcommand = strtok(NULL, " ");
  if (subcommand == NULL)
  {
    LogUart("Usage: uart show | uart set <baud> <format>\r\n");
    return;
  }

  if (strcmp(subcommand, "show") == 0)
  {
    ConsolePrintCurrentUart();
    return;
  }

  if (strcmp(subcommand, "set") != 0)
  {
    LogUart("Usage: uart show | uart set <baud> <format>\r\n");
    return;
  }

  baud_token = strtok(NULL, " ");
  format_token = strtok(NULL, " ");
  extra_token = strtok(NULL, " ");
  if ((baud_token == NULL) || (format_token == NULL) || (extra_token != NULL))
  {
    LogUart("Usage: uart set <baud> <8n1|8e1|8o1|8n2>\r\n");
    return;
  }

  baud_rate = strtoul(baud_token, &end_ptr, 10);
  if ((*baud_token == '\0') || (*end_ptr != '\0') || (baud_rate < 1200U) || (baud_rate > 1000000U))
  {
    LogUart("Baud must be an integer in the range 1200..1000000.\r\n");
    return;
  }

  if (ConsoleTryParseFormat(format_token, &word_length, &parity, &stop_bits, &format_name) == 0U)
  {
    LogUart("Format must be one of: 8n1, 8e1, 8o1, 8n2.\r\n");
    return;
  }

  (void)snprintf(msg,
                 sizeof(msg),
                 "Applying UART console: %lu %s. Update terminal now.\r\n",
                 (unsigned long)baud_rate,
                 format_name);
  LogUart(msg);
  UartWaitForTxDrain();

  if (ConsoleApplyUartConfig(baud_rate, word_length, parity, stop_bits) != HAL_OK)
  {
    Error_Handler();
  }

  g_console_baud_rate = baud_rate;
  g_console_format_name = format_name;
  LogUart("\r\nUART reconfigured.\r\n");
  ConsolePrintCurrentUart();
}

static void ConsoleProcessRx(void)
{
  uint8_t byte;

  while (UartRxPopByte(&byte) != 0U)
  {
    ConsoleHandleByte(byte);
  }

  __disable_irq();
  if (g_uart_rx_overflow != 0U)
  {
    g_uart_rx_overflow = 0U;
    __enable_irq();
    LogUart("\r\nRX overflow: input dropped.\r\n");
  }
  else
  {
    __enable_irq();
  }
}

void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == B1_Pin)
  {
    uint32_t now = HAL_GetTick();
    if ((now - g_last_button_tick) < BUTTON_DEBOUNCE_MS)
    {
      return;
    }

    g_last_button_tick = now;
    g_blink_ms = (g_blink_ms == 500U) ? 100U : 500U;
    g_button_event = 1U;
  }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance != USART3)
  {
    return;
  }

  __disable_irq();
  g_uart_log_tail = UartLogNextIndex(g_uart_log_tail);
  g_uart_tx_busy = 0U;
  __enable_irq();

  UartLogStartTxIfIdle();
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  uint8_t next_head;

  if (huart->Instance != USART3)
  {
    return;
  }

  next_head = UartRxNextIndex(g_uart_rx_head);
  if (next_head == g_uart_rx_tail)
  {
    g_uart_rx_overflow = 1U;
  }
  else
  {
    g_uart_rx_fifo[g_uart_rx_head] = g_uart_rx_byte;
    g_uart_rx_head = next_head;
  }

  if (HAL_UART_Receive_IT(&huart3, (uint8_t *)&g_uart_rx_byte, 1U) != HAL_OK)
  {
    Error_Handler();
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance != USART3)
  {
    return;
  }

  if (HAL_UART_Receive_IT(&huart3, (uint8_t *)&g_uart_rx_byte, 1U) != HAL_OK)
  {
    Error_Handler();
  }
}
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

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

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
  MX_ICACHE_Init();
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */
  g_next_toggle_tick = HAL_GetTick() + g_blink_ms;
  LogUart("\r\nLAB4 started: non-blocking UART logging with interrupt queue.\r\n");
  LogUart("UART mode: HAL_UART_Transmit_IT (USART3 IRQ).\r\n");
  LogUart("Debounce window: 200 ms.\r\n");
  LogUart("Press USER button to change blink speed.\r\n");
  LogUart("Console commands: help, uart show, uart set <baud> <format>\r\n");
  StartConsoleRx();
  ConsolePrintPrompt();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint32_t now = HAL_GetTick();

    ConsoleProcessRx();

    if ((int32_t)(now - g_next_toggle_tick) >= 0)
    {
      HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
      g_next_toggle_tick = now + g_blink_ms;
    }

    if (g_button_event != 0U)
    {
      g_button_event = 0U;
      g_next_toggle_tick = now + g_blink_ms;
      if (g_blink_ms == 100U)
      {
        LogUart("Button event -> fast blink (100 ms)\r\n");
      }
      else
      {
        LogUart("Button event -> slow blink (500 ms)\r\n");
      }
    }

    if (g_console_cmd_ready != 0U)
    {
      g_console_cmd_ready = 0U;
      ConsoleProcessCommand();
      ConsolePrintPrompt();
    }

    UartLogStartTxIfIdle();
    __WFI();
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
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLL1_SOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 250;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1_VCIRANGE_1;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1_VCORANGE_WIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the programming delay
  */
  __HAL_FLASH_SET_PROGRAM_DELAY(FLASH_PROGRAMMING_DELAY_2);
}

/**
  * @brief ICACHE Initialization Function
  * @param None
  * @retval None
  */
static void MX_ICACHE_Init(void)
{

  /* USER CODE BEGIN ICACHE_Init 0 */

  /* USER CODE END ICACHE_Init 0 */

  /* USER CODE BEGIN ICACHE_Init 1 */

  /* USER CODE END ICACHE_Init 1 */

  /** Enable instruction cache in 1-way (direct mapped cache)
  */
  if (HAL_ICACHE_ConfigAssociativityMode(ICACHE_1WAY) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_ICACHE_Enable() != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ICACHE_Init 2 */

  /* USER CODE END ICACHE_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart3, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart3, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */
  HAL_NVIC_SetPriority(USART3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(USART3_IRQn);

  /* USER CODE END USART3_Init 2 */

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
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : RMII_MDC_Pin RMII_RXD0_Pin RMII_RXD1_Pin */
  GPIO_InitStruct.Pin = RMII_MDC_Pin|RMII_RXD0_Pin|RMII_RXD1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : RMII_REF_CLK_Pin RMII_MDIO_Pin RMII_CRS_DV_Pin */
  GPIO_InitStruct.Pin = RMII_REF_CLK_Pin|RMII_MDIO_Pin|RMII_CRS_DV_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : VBUS_SENSE_Pin */
  GPIO_InitStruct.Pin = VBUS_SENSE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(VBUS_SENSE_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : UCPD_CC1_Pin UCPD_CC2_Pin */
  GPIO_InitStruct.Pin = UCPD_CC1_Pin|UCPD_CC2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : RMII_TXD1_Pin */
  GPIO_InitStruct.Pin = RMII_TXD1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
  HAL_GPIO_Init(RMII_TXD1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : UCPD_FLT_Pin */
  GPIO_InitStruct.Pin = UCPD_FLT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(UCPD_FLT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : USB_FS_N_Pin USB_FS_P_Pin */
  GPIO_InitStruct.Pin = USB_FS_N_Pin|USB_FS_P_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF10_USB;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : RMII_TXT_EN_Pin RMI_TXD0_Pin */
  GPIO_InitStruct.Pin = RMII_TXT_EN_Pin|RMI_TXD0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /*Configure GPIO pins : ARD_D1_TX_Pin ARD_D0_RX_Pin */
  GPIO_InitStruct.Pin = ARD_D1_TX_Pin|ARD_D0_RX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF8_LPUART1;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI13_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI13_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};
  MPU_Attributes_InitTypeDef MPU_AttributesInit = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region 0 and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x08FFF000;
  MPU_InitStruct.LimitAddress = 0x08FFFFFF;
  MPU_InitStruct.AttributesIndex = MPU_ATTRIBUTES_NUMBER0;
  MPU_InitStruct.AccessPermission = MPU_REGION_ALL_RO;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Attribute 0 and the memory to be protected
  */
  MPU_AttributesInit.Number = MPU_ATTRIBUTES_NUMBER0;
  MPU_AttributesInit.Attributes = INNER_OUTER(MPU_NOT_CACHEABLE);

  HAL_MPU_ConfigMemoryAttributes(&MPU_AttributesInit);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

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
