/**
 * @file Lab5Part2.ino
 * @brief ESP32 application using FreeRTOS with multiple tasks:
 * - Light detection
 * - LCD display updates
 * - Alarm triggering
 * - Prime number calculation
 * 
 * Uses I2C LCD, semaphores for synchronization, and a task queue for LCD messages.
 */

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <freertos/semphr.h>

/// @brief Analog input pin for light sensor.
#define LIGHT_SENSOR_PIN 4

/// @brief Digital output pin for LED alarm.
#define LED_PIN 5

/// @brief I2C SDA pin for LCD.
#define SDA_PIN 8

/// @brief I2C SCL pin for LCD.
#define SCL_PIN 9

/// @brief I2C address for the LCD display.
#define LCD_ADDRESS 0x27

/// @brief 16x2 I2C LCD display instance.
LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);

/// @brief Semaphore used to trigger LCD display updates.
SemaphoreHandle_t displaySemaphore;

/// @brief Semaphore used to trigger alarm checks.
SemaphoreHandle_t alarmSemaphore;

/// @brief Queue for passing messages to the LCD task.
QueueHandle_t lcdQueue;

/// @brief Current light sensor reading.
volatile int lightLevel = 0;

/// @brief Current simple moving average of light readings.
volatile int smaValue = 0;

/// @brief History of last 5 light sensor readings.
int lightHistory[5] = {0};

/// @brief Index for circular light history buffer.
int historyIndex = 0;

/**
 * @brief Structure representing a two-line LCD message.
 */
typedef struct {
  char line1[16]; ///< First line of LCD message.
  char line2[16]; ///< Second line of LCD message.
} lcd_message_t;

/**
 * @brief Task that listens for LCD messages and updates the display.
 * @param arg Unused parameter.
 */
void lcdTask(void *arg) {
  lcd_message_t msg;
  while (1) {
    if (xQueueReceive(lcdQueue, &msg, portMAX_DELAY) == pdTRUE) {
      Serial.println("[LCD Task] Updating LCD");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(msg.line1);
      lcd.setCursor(0, 1);
      lcd.print(msg.line2);
    }
  }
}

/**
 * @brief Task that reads light sensor and computes simple moving average.
 * Signals other tasks using semaphores.
 * @param arg Unused parameter.
 */
void lightDetectorTask(void *arg) {
  while (1) {
    int val = analogRead(LIGHT_SENSOR_PIN);
    lightLevel = val;
    lightHistory[historyIndex] = val;
    historyIndex = (historyIndex + 1) % 5;

    int sum = 0;
    for (int i = 0; i < 5; i++) {
      sum += lightHistory[i];
    }
    smaValue = sum / 5;

    Serial.printf("[LightDetector Task] Light: %d, SMA: %d\n", lightLevel, smaValue);

    xSemaphoreGive(displaySemaphore);
    xSemaphoreGive(alarmSemaphore);

    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

/**
 * @brief Task that displays the latest light and SMA values if changed.
 * @param arg Unused parameter.
 */
void lightDisplayTask(void *arg) {
  int lastLight = -1;
  int lastSMA = -1;
  lcd_message_t msg;
  while (1) {
    if (xSemaphoreTake(displaySemaphore, portMAX_DELAY) == pdTRUE) {
      if (lightLevel != lastLight || smaValue != lastSMA) {
        snprintf(msg.line1, sizeof(msg.line1), "Light: %d", lightLevel);
        snprintf(msg.line2, sizeof(msg.line2), "SMA: %d", smaValue);
        Serial.println("[LightDisplay Task] Light/SMA changed. Sending to LCD...");
        xQueueSend(lcdQueue, &msg, portMAX_DELAY);
        lastLight = lightLevel;
        lastSMA = smaValue;
      }
    }
  }
}

/**
 * @brief Task that monitors the SMA value and triggers an LED alarm if it is out of expected range.
 * @param arg Unused parameter.
 */
void alarmTask(void *arg) {
  pinMode(LED_PIN, OUTPUT);
  while (1) {
    if (xSemaphoreTake(alarmSemaphore, portMAX_DELAY) == pdTRUE) {
      Serial.printf("[Alarm Task] Checking SMA: %d\n", smaValue);
      if (smaValue > 3800 || smaValue < 300) {
        Serial.println("[Alarm Task] Anomaly Detected! Flashing LED...");
        for (int i = 0; i < 3; i++) {
          digitalWrite(LED_PIN, HIGH);
          vTaskDelay(pdMS_TO_TICKS(250));
          digitalWrite(LED_PIN, LOW);
          vTaskDelay(pdMS_TO_TICKS(250));
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
      }
    }
  }
}

/**
 * @brief Task that computes prime numbers up to 5000.
 * @param arg Unused parameter.
 */
void primeTask(void *arg) {
  Serial.println("[Prime Task] Starting prime computation...");
  for (int num = 2; num <= 5000; num++) {
    bool isPrime = true;
    for (int i = 2; i * i <= num; i++) {
      if (num % i == 0) {
        isPrime = false;
        break;
      }
    }
    if (isPrime) {
      Serial.printf("Prime: %d\n", num);
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  Serial.println("[Prime Task] Prime computation completed.");
  vTaskDelete(NULL);
}

/**
 * @brief Initializes peripherals, semaphores, queue, and creates all tasks.
 */
void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();
  analogReadResolution(12);
  pinMode(LED_PIN, OUTPUT);

  displaySemaphore = xSemaphoreCreateBinary();
  alarmSemaphore = xSemaphoreCreateBinary();
  lcdQueue = xQueueCreate(5, sizeof(lcd_message_t));
  if (!lcdQueue || !displaySemaphore || !alarmSemaphore) {
    Serial.println("Failed to create queue or semaphores");
    while (1);
  }

  xTaskCreatePinnedToCore(lcdTask, "LCD", 2048, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(lightDetectorTask, "LightDetector", 2048, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(lightDisplayTask, "LightDisplay", 2048, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(alarmTask, "Alarm", 2048, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(primeTask, "Prime", 4096, NULL, 1, NULL, 1);
}

/**
 * @brief Main loop function (unused in FreeRTOS application).
 */
void loop() {
  // unused
}
