#pragma once

#include <Arduino.h>
#include <stdarg.h>

namespace DebugLog {

inline void begin(uint32_t baud = 115200) {
  Serial.begin(baud);
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  // Serial is USB CDC on ESP32-S3. Serial0 is the hardware UART connected
  // to the USB/UART bridge on many Waveshare boards. Logging to both makes
  // diagnostics visible regardless of which COM port PlatformIO opens.
  Serial0.begin(baud);
#endif
  delay(1200);
}

inline void print(const char* text) {
  if (!text) return;
  Serial.print(text);
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  Serial0.print(text);
#endif
}

inline void println(const char* text = "") {
  Serial.println(text ? text : "");
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  Serial0.println(text ? text : "");
#endif
}

inline void printf(const char* format, ...) {
  if (!format) return;
  char buffer[320];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  print(buffer);
}

inline void flush() {
  Serial.flush();
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  Serial0.flush();
#endif
}

}  // namespace DebugLog
