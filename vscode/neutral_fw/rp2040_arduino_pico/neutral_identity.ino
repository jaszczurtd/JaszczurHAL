#include <Arduino.h>

#ifndef LED_BUILTIN
#define LED_BUILTIN 25
#endif

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
}

void loop() {
  static bool led_on = false;
  digitalWrite(LED_BUILTIN, led_on ? HIGH : LOW);
  led_on = !led_on;
  delay(1000);
}

