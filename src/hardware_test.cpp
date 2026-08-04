#include <Arduino.h>
#include <Waveshare_ST7262_LVGL.h>
#include <esp_system.h>
#include <lvgl.h>

#include "version.h"

namespace {
lv_obj_t* gStatus = nullptr;
uint32_t gTouches = 0;

void touchButtonEvent(lv_event_t*) {
  ++gTouches;
  lv_label_set_text_fmt(gStatus, "Touch OK - count: %lu",
                        static_cast<unsigned long>(gTouches));
}

void createTestUi() {
  lv_obj_t* screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x07121D), 0);

  lv_obj_t* title = lv_label_create(screen);
  lv_label_set_text(title, "Waveshare ESP32-S3-Touch-LCD-7");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 35);

  lv_obj_t* info = lv_label_create(screen);
  lv_label_set_text_fmt(info,
                        "%s\nLCD: %d x %d\nFlash: %u MB\nPSRAM: %u MB\nFree PSRAM: %u kB",
                        FW_VERSION, lv_disp_get_hor_res(lv_disp_get_default()),
                        lv_disp_get_ver_res(lv_disp_get_default()),
                        static_cast<unsigned>(ESP.getFlashChipSize() / 1024 / 1024),
                        static_cast<unsigned>(ESP.getPsramSize() / 1024 / 1024),
                        static_cast<unsigned>(ESP.getFreePsram() / 1024));
  lv_obj_set_style_text_color(info, lv_color_hex(0xB8D8E8), 0);
  lv_obj_set_style_text_font(info, &lv_font_montserrat_18, 0);
  lv_obj_align(info, LV_ALIGN_CENTER, 0, -30);

  lv_obj_t* button = lv_btn_create(screen);
  lv_obj_set_size(button, 260, 72);
  lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -45);
  lv_obj_add_event_cb(button, touchButtonEvent, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* buttonLabel = lv_label_create(button);
  lv_label_set_text(buttonLabel, "DOTKNOUT SE ZDE");
  lv_obj_set_style_text_font(buttonLabel, &lv_font_montserrat_20, 0);
  lv_obj_center(buttonLabel);

  gStatus = lv_label_create(screen);
  lv_label_set_text(gStatus, "Waiting for touch...");
  lv_obj_set_style_text_color(gStatus, lv_color_hex(0x7EE2A8), 0);
  lv_obj_set_style_text_font(gStatus, &lv_font_montserrat_16, 0);
  lv_obj_align_to(gStatus, button, LV_ALIGN_OUT_TOP_MID, 0, -14);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.printf("\n%s %s - hardware test\n", FW_NAME, FW_VERSION);
  Serial.printf("Flash %u MB, PSRAM %u MB\n",
                static_cast<unsigned>(ESP.getFlashChipSize() / 1024 / 1024),
                static_cast<unsigned>(ESP.getPsramSize() / 1024 / 1024));

  if (!psramFound()) {
    Serial.println("Fatal: OPI PSRAM was not detected.");
    while (true) delay(1000);
  }

  lcd_init();
  lv_disp_t* display = lv_disp_get_default();
  if (!display || lv_disp_get_hor_res(display) != 800 ||
      lv_disp_get_ver_res(display) != 480) {
    Serial.println("Fatal: LCD is not registered as 800 x 480.");
    while (true) delay(1000);
  }

  lvgl_port_lock(-1);
  createTestUi();
  lvgl_port_unlock();
  Serial.println("LCD initialized. Press the on-screen button to test GT911.");
}

void loop() { delay(100); }
