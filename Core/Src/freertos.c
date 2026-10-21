/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>          // snprintf: builds text strings
#include "i2c.h"            // hi2c1, the I2C1 handle CubeMX created
#include "tim.h"            // htim1 (fan PWM) and htim2 (tach capture)
#include "ssd1306.h"        // OLED driver
#include "ssd1306_fonts.h"  // OLED fonts
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
// Temperature to fan speed.
//
// A desk fan does not cool the room, it blows air at whoever is sitting in
// front of it, so there is no loop to close and a PID has nothing to converge
// to. What this needs is a comfort curve: this warm means this fast. Set these
// four numbers to what feels right to you, that is the correct way to tune
// them.
#define FAN_T_ON          21.0f    // start blowing at or above this
#define FAN_T_OFF         20.0f   // and stop again only once it has dropped to here
#define FAN_T_FULL        25.0f  // full speed at or above this
#define FAN_DUTY_START    30 // the slowest duty at which the fan actually turns
#define FAN_DUTY_MAX      100
#define FAN_SLEW_PER_S    10 // the most the duty may change in one second
#define FAN_KICK_MS       400 // full power for this long when starting from standstill

// How we behave when we do not have a temperature we believe in.
#define TEMP_STALE_MS     5000   // a reading older than this is not a reading any more
#define FAN_SAFE_DUTY     50     // what the fan runs at when it has nothing to go on

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
volatile float temperature_c = 0; // latest temperature in C
volatile float humidity_pct = 0;  // latest humidity in %

volatile uint32_t temp_last_ms = 0; // tick of the last reading we believed
volatile uint8_t temp_ok = 0; // 1 once the sensor has answered at least once
volatile uint32_t temp_errors = 0; // how many times it has not, since boot

volatile uint8_t  fan_duty_pct = 0;  // current fan speed, 0-100 %, set automatically from the temperature
volatile uint32_t fan_rpm = 0;       // measured fan speed
volatile uint32_t tach_pulses = 0;   // pulses counted by the TIM2 interrupt since the last reading
/* USER CODE END Variables */

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void SensorTask(void *argument);    // our own task: reads the AHT20 and draws on the OLED
void FanTask(void *argument);       // our own task: drives the fan PWM and measures its RPM
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void configureTimerForRunTimeStats(void);
unsigned long getRunTimeCounterValue(void);
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);
void vApplicationMallocFailedHook(void);

/* USER CODE BEGIN 1 */
/* Functions needed when configGENERATE_RUN_TIME_STATS is on */
__weak void configureTimerForRunTimeStats(void)
{

}

__weak unsigned long getRunTimeCounterValue(void)
{
return 0;
}
/* USER CODE END 1 */

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
   /* Run time stack overflow checking is performed if
   configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
   called if a stack overflow is detected. */
}
/* USER CODE END 4 */

/* USER CODE BEGIN 5 */
void vApplicationMallocFailedHook(void)
{
   /* vApplicationMallocFailedHook() will only be called if
   configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h. It is a hook
   function that will get called if a call to pvPortMalloc() fails.
   pvPortMalloc() is called internally by the kernel whenever a task, queue,
   timer or semaphore is created. It is also called by various parts of the
   demo application. If heap_1.c or heap_2.c are used, then the size of the
   heap available to pvPortMalloc() is defined by configTOTAL_HEAP_SIZE in
   FreeRTOSConfig.h, and the xPortGetFreeHeapSize() API function can be used
   to query the size of free heap space that remains (although it does not
   provide information on how the remaining heap might be fragmented). */
}
/* USER CODE END 5 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  // Create our task with the native FreeRTOS API.
  // 512 = stack size in words (2 KB); priority 1 = just above the idle task.
  xTaskCreate(SensorTask, "sensor", 512, NULL, tskIDLE_PRIORITY + 1, NULL);
  // Fan task: small stack (256 words = 1 KB), higher priority (2) because it is timing-related.
  xTaskCreate(FanTask, "fan", 256, NULL, tskIDLE_PRIORITY + 2, NULL);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  for (;;)
  {
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13); // blink = "system is alive"
    vTaskDelay(pdMS_TO_TICKS(500));         // sleep 500 ms, CPU free for other tasks
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
void SensorTask(void *argument)
{
  uint8_t cmd[3];            // bytes we send to the sensor
  uint8_t rx[7];             // bytes we get back
  char line[20];             // one line of text for the screen

  vTaskDelay(pdMS_TO_TICKS(40));                             // AHT20 needs 40 ms after power-up

  HAL_I2C_Master_Receive(&hi2c1, 0x38 << 1, rx, 1, 100);     // read the AHT20 status byte
  if ((rx[0] & 0x08) == 0)                                   // bit 3 = calibrated? if not...
  {
    cmd[0] = 0xBE; cmd[1] = 0x08; cmd[2] = 0x00;             // ...the "initialize" command
    HAL_I2C_Master_Transmit(&hi2c1, 0x38 << 1, cmd, 3, 100); // send it
    vTaskDelay(pdMS_TO_TICKS(10));                           // give it 10 ms
  }

  ssd1306_Init();                                            // wake the OLED up and configure it

  for (;;)
  {
    uint8_t ok = 0; // did this round produce a reading?

    cmd[0] = 0xAC;
    cmd[1] = 0x33;
    cmd[2] = 0x00; // the "start measurement" command
    if (HAL_I2C_Master_Transmit(&hi2c1, 0x38 << 1, cmd, 3, 100) == HAL_OK) {
      vTaskDelay(
          pdMS_TO_TICKS(80)); // measuring takes ~80 ms; CPU is free meanwhile
      if (HAL_I2C_Master_Receive(&hi2c1, 0x38 << 1, rx, 7, 100) == HAL_OK) {
        if ((rx[0] & 0x80) == 0) // bit 7 = busy; 0 means the data is ready
        {
          uint32_t raw_h =
              ((uint32_t)rx[1] << 12) | ((uint32_t)rx[2] << 4) | (rx[3] >> 4);
          uint32_t raw_t =
              (((uint32_t)rx[3] & 0x0F) << 16) | ((uint32_t)rx[4] << 8) | rx[5];
          float h =
              raw_h * 100.0f / 1048576.0f; // datasheet formula (1048576 = 2^20)
          float t = raw_t * 200.0f / 1048576.0f - 50.0f; // datasheet formula

          // A stuck bus can hand back a byte pattern that decodes into a
          // perfectly well formed but impossible number. Check before
          // believing.
          if (t > -20.0f && t < 80.0f && h >= 0.0f && h <= 100.0f) {
            humidity_pct = h;
            temperature_c = t;
            temp_last_ms = HAL_GetTick(); // this is the important line
            temp_ok = 1;
            ok = 1;
          }
        }
      }
    }

    if (!ok) {
      temp_errors++; // do not touch temperature_c. Let it go stale on its own,
    } // and let whoever uses it notice that it did.

    uint8_t temp_fresh =  (temp_ok && (HAL_GetTick() - temp_last_ms) < TEMP_STALE_MS);
    int t10 = (int)(temperature_c * 10);                     // 23.4 -> 234, so we can print without floats
    int h10 = (int)(humidity_pct * 10);                      // same for humidity

    ssd1306_Fill(Black);                                             // clear the drawing buffer
    ssd1306_SetCursor(0, 0);                                         // top-left corner
    if (temp_fresh)
      snprintf(line, sizeof(line), "T %d.%d C", t10 / 10,
              t10 % 10); // e.g. "T 23.4 C"
    else
      snprintf(line, sizeof(line), 
              "T --.- C"); // say so, do not show a stale number
    ssd1306_WriteString(line, Font_11x18, White);                    // draw it into the buffer
    ssd1306_SetCursor(0, 22);                                        // second line
    snprintf(line, sizeof(line), "H %d.%d %%", h10 / 10, h10 % 10);  // e.g. "H 45.2 %"
    ssd1306_WriteString(line, Font_11x18, White);                    // draw it
    ssd1306_SetCursor(0, 44);                                        // third line
    snprintf(line, sizeof(line), "F %lu %u%%", (unsigned long)fan_rpm, fan_duty_pct); // e.g. "F 1500 40%"
    ssd1306_WriteString(line, Font_11x18, White);                    // draw it
    ssd1306_UpdateScreen();                                          // send the whole buffer to the OLED

    vTaskDelay(pdMS_TO_TICKS(1000));                         // one reading per second
  }
}

void FanTask(void *argument)
{
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);        // start the 25 kHz PWM signal on PA8
  HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_1);      // start tach capture on PA5, interrupt on every pulse

  uint8_t fan_on = 0;    // the hysteresis state: is it currently blowing?
  uint16_t duty_now = 0; // what we are actually commanding, after slew limiting

  // vTaskDelayUntil counts from a reference point instead of from "now".
  // It updates next_wake itself on every call, so the period stays exactly
  // 1000 ms however long the body of the loop took.
  TickType_t next_wake = xTaskGetTickCount();

  for (;;)
  {
    float t = temperature_c; // latest temperature from SensorTask
    uint16_t want = 0;       // what the curve asks for, before slew limiting

    uint8_t temp_fresh =
        (temp_ok && (HAL_GetTick() - temp_last_ms) < TEMP_STALE_MS);

    if (!temp_fresh) {
      want = FAN_SAFE_DUTY; // nothing to go on, so stop pretending to control
      fan_on = 0;           // and make a returning sensor start the curve fresh
    } else {
      // Hysteresis. It takes FAN_T_ON to start but only FAN_T_OFF to stop, so a
      // temperature sitting exactly on one threshold cannot make the fan
      // chatter.
      if (!fan_on && t >= FAN_T_ON)
        fan_on = 1;
      if (fan_on && t <= FAN_T_OFF)
        fan_on = 0;

      if (!fan_on) {
        want = 0;
      } else if (t >= FAN_T_FULL) {
        want = FAN_DUTY_MAX;
      } else {
        // Straight line between the two temperatures, starting at the lowest
        // duty that actually turns the fan rather than at zero.
        float frac = (t - FAN_T_ON) / (FAN_T_FULL - FAN_T_ON);
        want =
            FAN_DUTY_START + (uint16_t)(frac * (FAN_DUTY_MAX - FAN_DUTY_START));
      }
    }

    uint8_t starting = (duty_now == 0) && (want > 0);
    // Slew limiting. The curve is allowed to jump, the fan is not: a sudden
    // change in airflow is the thing a person in front of it actually notices.
    if (want == 0) {
      duty_now = 0; // stopping may be immediate
    } else if (duty_now < FAN_DUTY_START) {
      duty_now = FAN_DUTY_START; // from standstill, jump to where it turns.
    } // Ramping through 10 and 20 only makes it buzz.
    else if (want > duty_now) {
      duty_now += (want - duty_now > FAN_SLEW_PER_S) ? FAN_SLEW_PER_S
                                                     : (want - duty_now);
    } else {
      duty_now -= (duty_now - want > FAN_SLEW_PER_S) ? FAN_SLEW_PER_S
                                                     : (duty_now - want);
    }

    uint32_t duty = duty_now;
    fan_duty_pct = (uint8_t)duty;                  // publish it, so the OLED can show it

    if (starting) {
      __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 3840); // 100 %
      vTaskDelay(pdMS_TO_TICKS(FAN_KICK_MS));
    }
    
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, duty * 3840 / 100);  // 3840 ticks = one full PWM period

    vTaskDelayUntil(&next_wake, pdMS_TO_TICKS(1000)); // exactly 1 s between wake-ups

    taskENTER_CRITICAL();                          // block interrupts so the ISR can't change the counter mid-read
    uint32_t pulses = tach_pulses;                 // take the count...
    tach_pulses = 0;                               // ...and reset it for the next second
    taskEXIT_CRITICAL();                           // interrupts back on

    fan_rpm = pulses * 30;                         // fan gives 2 pulses per turn: pulses/2 per second x 60 s
  }
}

// HAL calls this from the TIM2 interrupt every time the tach pin sees a rising edge.
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM2)                      // this callback is shared by all timers, so check which one
  {
    tach_pulses++;                                 // just count; keep interrupt code as short as possible
  }
}
/* USER CODE END Application */

