#include "display_stability.h"

#include <Waveshare_ST7262_LVGL.h>
#include <esp_err.h>
#include <esp_lcd_panel_rgb.h>
#include <lvgl.h>

namespace {
esp_lcd_panel_handle_t rgbPanelHandle() {
  lv_disp_t* display = lv_disp_get_default();
  if (!display || !display->driver || !display->driver->user_data) {
    return nullptr;
  }

  auto* lcd = static_cast<ESP_PanelLcd*>(display->driver->user_data);
  return lcd ? lcd->getHandle() : nullptr;
}
}  // namespace

namespace DisplayStability {

bool configureSafePixelClock(uint32_t pixelClockHz) {
  esp_lcd_panel_handle_t panel = rgbPanelHandle();
  if (!panel) {
    Serial.println("LCD sync: RGB panel handle is not available");
    return false;
  }

  const esp_err_t clockResult =
      esp_lcd_rgb_panel_set_pclk(panel, pixelClockHz);
  if (clockResult != ESP_OK) {
    Serial.printf("LCD sync: set PCLK failed: %s\n",
                  esp_err_to_name(clockResult));
    return false;
  }

  const esp_err_t restartResult = esp_lcd_rgb_panel_restart(panel);
  if (restartResult != ESP_OK) {
    Serial.printf("LCD sync: initial restart failed: %s\n",
                  esp_err_to_name(restartResult));
    return false;
  }

  // PCLK changes and restart requests are applied by the RGB driver at VSYNC.
  delay(45);
  Serial.printf("LCD sync: safe PCLK set to %.1f MHz\n",
                pixelClockHz / 1000000.0f);
  return true;
}

bool restartAtVsync() {
  esp_lcd_panel_handle_t panel = rgbPanelHandle();
  if (!panel) return false;
  const esp_err_t result = esp_lcd_rgb_panel_restart(panel);
  if (result != ESP_OK) {
    Serial.printf("LCD sync: restart failed: %s\n", esp_err_to_name(result));
    return false;
  }
  return true;
}

}  // namespace DisplayStability
