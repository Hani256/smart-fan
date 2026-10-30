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
#include "cmsis_os.h"
#include "main.h"
#include "task.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "i2c.h"           // hi2c1, the I2C1 handle CubeMX created
#include "queue.h"         // xQueueCreate, xQueueOverwrite, xQueuePeek
#include "ssd1306.h"       // OLED driver
#include "ssd1306_fonts.h" // OLED fonts
#include "tim.h"           // htim1 (fan PWM) and htim2 (tach capture)
#include "usart.h"         // huart2, the serial link to the ESP32
#include <stdio.h>         // snprintf: builds text strings
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
// One reading, together with the moment it was taken. These two belong to each
// other, so they have to travel together or neither of them is trustworthy.
typedef struct {
  float temp_c;
  float hum_pct;
  uint32_t stamp_ms;
} sensor_sample_t;

// What the fan should do. Changed by the button, and later from the phone.
typedef enum {
  MODE_AUTO = 0, // follow the temperature curve
  MODE_MANUAL,   // fixed speed
  MODE_OFF,      // stopped
  MODE_NEXT,     // only in commands: "go to the next mode" (the button)
} fan_mode_t;

// One request to change the mode. Travels through q_cmd to FanTask.
typedef struct {
  fan_mode_t mode;
  uint8_t duty_pct; // only used with MODE_MANUAL
} fan_cmd_t;

// What the fan is doing right now, for the display.
typedef struct {
  uint8_t duty_pct;
  uint32_t rpm;
  fan_mode_t mode; // which mode the fan is in
} fan_status_t;
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
#define FAN_T_ON            21.0f    // start blowing at or above this
#define FAN_T_OFF           20.0f   // and stop again only once it has dropped to here
#define FAN_T_FULL          25.0f  // full speed at or above this
#define FAN_DUTY_START      30 // the slowest duty at which the fan actually turns
#define FAN_DUTY_MAX        100
#define FAN_SLEW_PER_S      10 // the most the duty may change in one second
#define FAN_KICK_MS         400 // full power for this long when starting from standstill

// How we behave when we do not have a temperature we believe in.
#define TEMP_STALE_MS       5000                   // a reading older than this is not a reading any more
#define FAN_SAFE_DUTY       50 // what the fan runs at when it has nothing to go on
#define MANUAL_DUTY_DEFAULT 50 // manual speed until someone picks another
#define BUTTON_SETTLE_MS 30    // how long a contact may bounce

// Tach edges closer together than this are noise, not the fan. Two pulses per
// turn, so 5000 us between them would be 6000 rpm, twice the 3000 rpm this
// fan (an ARCTIC P12 Pro) is rated for. A real pulse train never comes this
// close.
#define TACH_MIN_GAP_US     5000
// The status line to the ESP32 goes out this often.
#define LINK_PERIOD_MS      1000

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
// Two one-deep mailboxes. Each holds only the newest value: the writer
// overwrites whatever was there, the reader peeks without taking it away.
//
// A shared volatile global would have been simpler and subtly wrong. Writing
// the temperature and its timestamp is two separate stores, and the task that
// reads them has the higher priority, so it can run in the gap between the two
// and see a new reading carrying an old timestamp. volatile does not help with
// that: it controls visibility and the order of volatile accesses, not whether
// a group of them is indivisible. The queue copies the whole struct under a
// critical section, so from any other task there is no half-written state.
QueueHandle_t q_sensor; // SensorTask to FanTask: the temperature
QueueHandle_t q_fan;    // FanTask to SensorTask: duty and rpm, for the screen
QueueHandle_t q_cmd;    // button and ESP32 to FanTask: mode changes

volatile uint32_t temp_errors = 0; // SensorTask only, never crosses a task
volatile uint32_t tach_pulses = 0; // written by the TIM2 ISR, read by FanTask
static TaskHandle_t link_task;     // The UART transmit-complete interrupt wakes this task, so it needs the handle.
static TaskHandle_t button_task;   // the EXTI0 interrupt wakes this task
/* USER CODE END Variables */

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
    .name = "defaultTask",
    .stack_size = 128 * 4,
    .priority = (osPriority_t)osPriorityLow,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void SensorTask(
    void *argument); // our own task: reads the AHT20 and draws on the OLED
void FanTask(
    void *argument); // our own task: drives the fan PWM and measures its RPM
void LinkTask(
    void *argument); // our own task: sends the status to the ESP32 over USART2
void ButtonTask(
    void *argument); // our own task: turns presses on PA0 into mode changes
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
__weak void configureTimerForRunTimeStats(void) {}

__weak unsigned long getRunTimeCounterValue(void) { return 0; }
/* USER CODE END 1 */

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName) {
  /* Run time stack overflow checking is performed if
  configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
  called if a stack overflow is detected. */
}
/* USER CODE END 4 */

/* USER CODE BEGIN 5 */
void vApplicationMallocFailedHook(void) {
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
  // Length 1 on purpose: a mailbox, not a backlog. Neither task wants a queue
  // of old readings, both want the newest one. This runs before the tasks are
  // created and before the scheduler starts, so the mailboxes always exist by
  // the time anyone looks in them.
  q_sensor = xQueueCreate(1, sizeof(sensor_sample_t));
  q_fan = xQueueCreate(1, sizeof(fan_status_t));
  q_cmd = xQueueCreate(4, sizeof(fan_cmd_t));
  configASSERT(q_sensor != NULL && q_fan != NULL &&
               q_cmd != NULL); // NULL here means the heap is too small
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle =
      osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  // Priorities, lowest to highest:
  //   defaultTask (LED blink)  osPriorityLow          8
  //   link to the ESP32        osPriorityLow4        12
  //   sensor, button           osPriorityBelowNormal 16
  //   fan                      osPriorityNormal      24
  // The blink task is set in CubeMX and created with the CMSIS API, which uses
  // these same numbers, so ours use the CMSIS names too. That keeps all three
  // on one scale, and the LED cannot get in front of the fan.
  //
  // Created with the native FreeRTOS API. 512 = stack size in words (2 KB).
  xTaskCreate(SensorTask, "sensor", 512, NULL,
              (UBaseType_t)osPriorityBelowNormal, NULL);
  // Fan task: small stack (256 words = 1 KB), highest of the three because it
  // is timing-related.
  xTaskCreate(FanTask, "fan", 256, NULL, (UBaseType_t)osPriorityNormal, NULL);
  // Link task: 384 words (1.5 KB), because snprintf needs room. Below the
  // sensor and the fan: if the ESP32 misses a line, nothing bad happens.
  xTaskCreate(LinkTask, "link", 384, NULL, (UBaseType_t)osPriorityLow4,
              &link_task);
  // Button task: tiny stack, it only reads a pin and sends a command.
  xTaskCreate(ButtonTask, "button", 128, NULL,
              (UBaseType_t)osPriorityBelowNormal, &button_task);
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
void StartDefaultTask(void *argument) {
  /* USER CODE BEGIN StartDefaultTask */
  for (;;) {
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13); // blink = "system is alive"
    vTaskDelay(pdMS_TO_TICKS(500)); // sleep 500 ms, CPU free for other tasks
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
// Bring I2C1 back after an error. A slave unplugged in the middle of a
// transfer can leave the bus stuck, and once HAL has recorded that, every
// later call keeps failing until the peripheral is rebuilt.
static void i2c_recover(void) {
  HAL_I2C_DeInit(&hi2c1);
  vTaskDelay(pdMS_TO_TICKS(2));
  MX_I2C1_Init();
}

// Ask the AHT20 whether it is calibrated and calibrate it if not.
// This has to be callable at any time, not only at boot: a sensor that was
// disconnected comes back uncalibrated and answers with nonsense until it
// gets this command again. Returns 1 if the sensor is talking to us.
static uint8_t aht20_begin(void) {
  uint8_t cmd[3], rx[1];

  if (HAL_I2C_Master_Receive(&hi2c1, 0x38 << 1, rx, 1, 100) != HAL_OK)
    return 0; // nobody there

  if ((rx[0] & 0x08) == 0) // bit 3 = calibrated? if not...
  {
    cmd[0] = 0xBE;
    cmd[1] = 0x08;
    cmd[2] = 0x00; // ...the "initialize" command
    if (HAL_I2C_Master_Transmit(&hi2c1, 0x38 << 1, cmd, 3, 100) != HAL_OK)
      return 0;
    vTaskDelay(pdMS_TO_TICKS(10)); // give it 10 ms
  }
  return 1;
}

void SensorTask(void *argument) {
  uint8_t cmd[3]; // bytes we send to the sensor
  uint8_t rx[7];  // bytes we get back
  char line[20];  // one line of text for the screen

  sensor_sample_t s = {0};  // the newest reading we have taken
  fan_status_t f = {0};     // the newest fan status, for the bottom line
  uint8_t have_reading = 0; // 1 once the sensor has answered at least once
  uint8_t fails = 0; // failed rounds in a row, for deciding when to recover

  vTaskDelay(pdMS_TO_TICKS(40)); // AHT20 needs 40 ms after power-up

  aht20_begin(); // may fail if the sensor is absent; the loop retries

  ssd1306_Init(); // wake the OLED up and configure it

  for (;;) {
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
            s.temp_c = t;
            s.hum_pct = h;
            s.stamp_ms = HAL_GetTick(); // this is the important line
            have_reading = 1;
            ok = 1;
            // One call, one complete sample. There is no instant at which the
            // other task can see half of it.
            xQueueOverwrite(q_sensor, &s);
          }
        }
      }
    }

    // Either way, do not touch the mailbox. Let the last sample go stale on
    // its own, and let whoever reads it notice from the timestamp.
    if (!ok) {
      temp_errors++;
      // Three failures in a row is not bad luck, it is a broken link. Rebuild
      // the bus, then hand the sensor its calibrate command again, because a
      // reconnected AHT20 has forgotten it.
      if (++fails >= 3) {
        fails = 0;
        i2c_recover();
        aht20_begin();
      }
    } else {
      fails = 0;
    }

    uint8_t temp_fresh =
        (have_reading && (HAL_GetTick() - s.stamp_ms) < TEMP_STALE_MS);
    int t10 =
        (int)(s.temp_c * 10); // 23.4 -> 234, so we can print without floats
    int h10 = (int)(s.hum_pct * 10); // same for humidity

    xQueuePeek(q_fan, &f, 0); // newest fan status, or leave f as it was

    ssd1306_Fill(Black);     // clear the drawing buffer
    ssd1306_SetCursor(0, 0); // top-left corner
    if (temp_fresh)
      snprintf(line, sizeof(line), "T %d.%d C", t10 / 10,
               t10 % 10); // e.g. "T 23.4 C"
    else
      snprintf(line, sizeof(line),
               "T --.- C"); // say so, do not show a stale number
    ssd1306_WriteString(line, Font_11x18, White); // draw it into the buffer
    ssd1306_SetCursor(0, 22);                     // second line
    snprintf(line, sizeof(line), "H %d.%d %%", h10 / 10,
             h10 % 10);                           // e.g. "H 45.2 %"
    ssd1306_WriteString(line, Font_11x18, White); // draw it
    ssd1306_SetCursor(0, 44);                     // third line
    snprintf(line, sizeof(line), "%c %lu %u%%", "AMO"[f.mode],
             (unsigned long)f.rpm, f.duty_pct);   // e.g. "A 1500 40%"
    ssd1306_WriteString(line, Font_11x18, White); // draw it
    ssd1306_UpdateScreen(); // send the whole buffer to the OLED

    vTaskDelay(pdMS_TO_TICKS(1000)); // one reading per second
  }
}

void FanTask(void *argument) {
  HAL_TIM_PWM_Start(&htim1,
                    TIM_CHANNEL_1); // start the 25 kHz PWM signal on PA8
  HAL_TIM_IC_Start_IT(
      &htim2,
      TIM_CHANNEL_1); // start tach capture on PA5, interrupt on every pulse

  uint8_t fan_on = 0;    // the hysteresis state: is it currently blowing?
  uint16_t duty_now = 0; // what we are actually commanding, after slew limiting

  fan_mode_t mode = MODE_AUTO; // start in automatic
  uint8_t manual_duty = MANUAL_DUTY_DEFAULT;

  sensor_sample_t s;     // the latest sample we peeked out of the mailbox
  fan_status_t st = {0}; // what we publish back for the screen

  // vTaskDelayUntil counts from a reference point instead of from "now".
  // It updates next_wake itself on every call, so the period stays exactly
  // 1000 ms however long the body of the loop took.
  TickType_t next_wake = xTaskGetTickCount();

  for (;;) {
    uint16_t want = 0; // what the curve asks for, before slew limiting

    // Take every command that arrived since the last round.
    fan_cmd_t c;
    while (xQueueReceive(q_cmd, &c, 0) == pdTRUE) {
      if (c.mode == MODE_NEXT) {
        mode = (mode == MODE_OFF) ? MODE_AUTO : (fan_mode_t)(mode + 1);
      } else {
        mode = c.mode;
        if (mode == MODE_MANUAL)
          manual_duty = c.duty_pct;
      }
    }

    // Peek, not receive: the sample stays in the mailbox, so a round in which
    // the sensor said nothing still sees the last thing it did say. The
    // timestamp inside the sample is what decides whether that is still worth
    // anything, and it arrived together with the value it belongs to.
    float t = 0.0f;
    uint8_t temp_fresh = 0;
    if (xQueuePeek(q_sensor, &s, 0) == pdTRUE) {
      t = s.temp_c;
      temp_fresh = (HAL_GetTick() - s.stamp_ms) < TEMP_STALE_MS;
    }

    if (mode == MODE_OFF) {
      want = 0;
      fan_on = 0; // back in auto, the curve starts fresh
    } else if (mode == MODE_MANUAL) {
      want = manual_duty;
      fan_on = 0;
    } else if (!temp_fresh) {
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
    st.mode = mode;
    st.duty_pct = (uint8_t)duty;

    xQueueOverwrite(q_fan, &st); // publish it, so the OLED can show it

    if (starting) {
      __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 3840); // 100 %
      vTaskDelay(pdMS_TO_TICKS(FAN_KICK_MS));
    }

    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1,
                          duty * 3840 /
                              100); // 3840 ticks = one full PWM period

    vTaskDelayUntil(&next_wake,
                    pdMS_TO_TICKS(1000)); // exactly 1 s between wake-ups

    taskENTER_CRITICAL(); // block interrupts so the ISR can't change the
                          // counter mid-read
    uint32_t pulses = tach_pulses; // take the count...
    tach_pulses = 0;               // ...and reset it for the next second
    taskEXIT_CRITICAL();           // interrupts back on

    st.rpm =
        pulses * 30; // fan gives 2 pulses per turn: pulses/2 per second x 60 s
    xQueueOverwrite(q_fan, &st); // publish the speed too
  }
}

// HAL calls this from the TIM2 interrupt every time the tach pin sees a rising
// edge.
//
// The tach line is filtered twice. In hardware, TIM2's input filter only
// passes a level that has been stable for about 11 us, which removes the short
// spikes the 25 kHz PWM couples onto it. Here, an edge only counts if it comes
// at least TACH_MIN_GAP_US after the last one that counted, which removes
// anything slower that got through.
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim) {
  if (htim->Instance ==
      TIM2) // this callback is shared by all timers, so check which one
  {
    static uint32_t last_us = 0; // capture time of the last edge we counted
    // TIM2 runs at 1 MHz, so the captured value is a time in microseconds.
    uint32_t now_us = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
    // Unsigned subtraction stays correct when the 32 bit counter wraps.
    if (now_us - last_us >= TACH_MIN_GAP_US) {
      last_us = now_us;
      tach_pulses++; // keep interrupt code as short as possible
    }
  }
}
// XOR of n characters. Both sides compute it, so a damaged line is caught.
static uint8_t link_checksum(const char *p, int n) {
  uint8_t x = 0;
  while (n--)
    x ^= (uint8_t)*p++;
  return x;
}
void LinkTask(void *argument) {
  static char buf[48]; // the line we send, must outlive the DMA transfer

  TickType_t next_wake = xTaskGetTickCount();

  for (;;) {
    vTaskDelayUntil(&next_wake, pdMS_TO_TICKS(LINK_PERIOD_MS));

    sensor_sample_t s;
    uint8_t fresh = 0;
    if (xQueuePeek(q_sensor, &s, 0) == pdTRUE)
      fresh = (HAL_GetTick() - s.stamp_ms) < TEMP_STALE_MS;

    fan_status_t f = {0};
    xQueuePeek(q_fan, &f, 0);

    int n;
    if (fresh)
      n = snprintf(buf, sizeof(buf), "$S,%d,%d,%u,%lu", (int)(s.temp_c * 10),
                   (int)(s.hum_pct * 10), f.duty_pct, (unsigned long)f.rpm);
    else
      n = snprintf(buf, sizeof(buf), "$S,,,%u,%lu", f.duty_pct,
                   (unsigned long)f.rpm);

    // checksum of everything after '$', then end the line
    n += snprintf(buf + n, sizeof(buf) - n, "*%02X\r\n",
                  link_checksum(buf + 1, n - 1));

    if (HAL_UART_Transmit_DMA(&huart2, (uint8_t *)buf, n) == HAL_OK) {
      if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100)) == 0)
        HAL_UART_AbortTransmit(&huart2); // took far too long, give up
    }
  }
}
// HAL calls this when the last byte has gone out.
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == USART2) {
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(link_task, &woken); // wake LinkTask
    portYIELD_FROM_ISR(woken);
  }
}

// Sleeps until the button interrupt wakes it, then decides whether that was a
// real press or just the contact bouncing.
void ButtonTask(void *argument) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // sleep until PA0 falls

    vTaskDelay(pdMS_TO_TICKS(BUTTON_SETTLE_MS)); // let the contact settle
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) {
      // Still pressed after the bounce: a real press.
      fan_cmd_t c = {.mode = MODE_NEXT};
      xQueueSend(q_cmd, &c, 0); // if the queue is full, drop this press

      // Wait for the release, so holding the button counts once.
      while (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET)
        vTaskDelay(pdMS_TO_TICKS(10));
      vTaskDelay(pdMS_TO_TICKS(BUTTON_SETTLE_MS)); // the release bounces too
    }
    ulTaskNotifyTake(pdTRUE, 0); // forget the edges the bouncing caused
  }
}

// HAL calls this from the EXTI0 interrupt when PA0 falls (button pressed).
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
  if (GPIO_Pin == GPIO_PIN_0) {
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(button_task, &woken); // wake ButtonTask
    portYIELD_FROM_ISR(woken);
  }
}
/* USER CODE END Application */