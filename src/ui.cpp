#include "ui.h"

#include <WiFi.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <time.h>

#include "config.h"

namespace UI {
namespace {
constexpr uint8_t kMapBufferCount = 2;
constexpr uint8_t kForecastCardCount = 3;
constexpr int kPressureCanvasW = 184;
constexpr int kPressureCanvasH = 86;
constexpr uint32_t kScreenBg = 0x061019;
constexpr uint32_t kHeaderBg = 0x0A1823;
constexpr uint32_t kSidebarBg = 0x09151F;
constexpr uint32_t kCardBg = 0x102433;
constexpr uint32_t kCardBorder = 0x254254;

lv_obj_t* gMapCanvas[kMapBufferCount] = {nullptr, nullptr};
uint16_t* gMapBuffer[kMapBufferCount] = {nullptr, nullptr};
uint8_t gFrontMap = 0;
uint8_t gBackMap = 1;
bool gDoubleBufferedMap = false;

lv_obj_t* gClockLabel = nullptr;
lv_obj_t* gHeaderLabel = nullptr;
lv_obj_t* gBacklightWakeOverlay = nullptr;
lv_obj_t* gPauseLabel = nullptr;
lv_obj_t* gCurrentTemp = nullptr;
lv_obj_t* gCurrentDetail1 = nullptr;
lv_obj_t* gCurrentDetail2 = nullptr;
lv_obj_t* gCurrentDetail3 = nullptr;
lv_obj_t* gSunLine = nullptr;
lv_obj_t* gSunAltitude = nullptr;
lv_obj_t* gMoonLine = nullptr;
lv_obj_t* gMoonAltitude = nullptr;
lv_obj_t* gMoonPhaseLabel = nullptr;
lv_obj_t* gMoonIcon = nullptr;
lv_obj_t* gForecastTime[kForecastCardCount] = {nullptr};
lv_obj_t* gForecastTemp[kForecastCardCount] = {nullptr};
lv_obj_t* gForecastRain[kForecastCardCount] = {nullptr};
lv_obj_t* gForecastIcon[kForecastCardCount] = {nullptr};
uint16_t* gIconBuffers[kForecastCardCount] = {nullptr};
lv_obj_t* gPressureCanvas = nullptr;
lv_obj_t* gPressureTitle = nullptr;
lv_obj_t* gPressureValue = nullptr;
lv_obj_t* gPressureDelta = nullptr;
lv_obj_t* gPressureForecast = nullptr;
uint16_t* gPressureBuffer = nullptr;
uint32_t gPressureHistoryRevision = UINT32_MAX;
uint16_t* gMoonIconBuffer = nullptr;
volatile bool gRadarPaused = false;
volatile bool gManualRefresh = false;
volatile bool gMapTapPending = false;
volatile int16_t gMapTapX = 0;
volatile int16_t gMapTapY = 0;
volatile bool gBacklightWakeRequested = false;

lv_obj_t* gStartupScreen = nullptr;
lv_obj_t* gMainScreen = nullptr;
lv_obj_t* gStartupStatus = nullptr;
lv_obj_t* gStartupProgressFill = nullptr;
lv_obj_t* gStartupProgressText = nullptr;
lv_obj_t* gStartupCenterLabel = nullptr;
lv_obj_t* gStartupDots[8] = {nullptr};
lv_timer_t* gStartupAnimationTimer = nullptr;
uint8_t gStartupAnimationPhase = 0;
bool gStartupActive = false;

lv_obj_t* gOtaScreen = nullptr;
lv_obj_t* gOtaStatus = nullptr;
lv_obj_t* gOtaBytes = nullptr;
lv_obj_t* gOtaResult = nullptr;

void startupAnimationTimer(lv_timer_t*) {
  if (!gStartupActive) return;
  gStartupAnimationPhase = static_cast<uint8_t>((gStartupAnimationPhase + 1) % 8);
  for (uint8_t i = 0; i < 8; ++i) {
    if (!gStartupDots[i]) continue;
    const uint8_t distance = static_cast<uint8_t>((i + 8 - gStartupAnimationPhase) % 8);
    lv_opa_t opacity = LV_OPA_30;
    if (distance == 0) opacity = LV_OPA_COVER;
    else if (distance == 1) opacity = LV_OPA_80;
    else if (distance == 2) opacity = LV_OPA_60;
    else if (distance == 3) opacity = LV_OPA_40;
    lv_obj_set_style_bg_opa(gStartupDots[i], opacity, 0);
  }
}

void applyPanelStyle(lv_obj_t* object, uint32_t background, int radius = 0,
                     uint32_t borderColor = 0, int borderWidth = 0) {
  lv_obj_set_style_bg_color(object, lv_color_hex(background), 0);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(object, lv_color_hex(borderColor), 0);
  lv_obj_set_style_border_width(object, borderWidth, 0);
  lv_obj_set_style_radius(object, radius, 0);
  lv_obj_set_style_pad_all(object, 0, 0);
  lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* makeLabel(lv_obj_t* parent, int x, int y, int width,
                    const char* text, const lv_font_t* font,
                    uint32_t color, lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_pos(label, x, y);
  lv_obj_set_width(label, width);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(label, align, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  return label;
}

void pixel36(uint16_t* buffer, int x, int y, uint16_t c) {
  if (x < 0 || y < 0 || x >= 36 || y >= 36) return;
  buffer[y * 36 + x] = c;
}

void iconLine(uint16_t* buffer, int x0, int y0, int x1, int y1, uint16_t c) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    pixel36(buffer, x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void iconCircle(uint16_t* buffer, int cx, int cy, int radius, uint16_t c) {
  for (int y = -radius; y <= radius; ++y)
    for (int x = -radius; x <= radius; ++x)
      if (x * x + y * y <= radius * radius)
        pixel36(buffer, cx + x, cy + y, c);
}

void iconCloud(uint16_t* buffer) {
  const uint16_t cloud = lv_color_hex(0xE1EAF0).full;
  iconCircle(buffer, 15, 18, 7, cloud);
  iconCircle(buffer, 23, 17, 8, cloud);
  iconCircle(buffer, 28, 21, 6, cloud);
  for (int y = 18; y <= 25; ++y)
    for (int x = 8; x <= 32; ++x) pixel36(buffer, x, y, cloud);
}

void drawIcon(uint8_t slot, int code) {
  if (slot >= kForecastCardCount || !gIconBuffers[slot]) return;
  uint16_t* buffer = gIconBuffers[slot];
  const uint16_t bg = lv_color_hex(kCardBg).full;
  const uint16_t sun = lv_color_hex(0xFFD34E).full;
  const uint16_t rain = lv_color_hex(0x45A9FF).full;
  const uint16_t snow = lv_color_hex(0xE8F4FF).full;
  const uint16_t thunder = lv_color_hex(0xFFE55F).full;
  for (int i = 0; i < 36 * 36; ++i) buffer[i] = bg;

  const bool thunderstorm = code == 3 || code == 4 || code == 37 ||
                            code == 38 || code == 39 || code == 45 ||
                            code == 47;
  const bool snowy = code == 13 || code == 14 || code == 15 || code == 16 ||
                     code == 41 || code == 42 || code == 43 || code == 46;
  const bool rainy = code == 9 || code == 10 || code == 11 || code == 12 ||
                     code == 40;
  const bool cloudy = code == 26 || code == 27 || code == 28 || code == 29 ||
                      code == 30 || code == 44;
  const bool clear = code == 31 || code == 32 || code == 33 || code == 34;

  if (clear) {
    iconCircle(buffer, 18, 18, 7, sun);
    for (int angle = 0; angle < 360; angle += 45) {
      const float r = angle * DEG_TO_RAD;
      iconLine(buffer, 18 + lroundf(cosf(r) * 10),
               18 + lroundf(sinf(r) * 10),
               18 + lroundf(cosf(r) * 15),
               18 + lroundf(sinf(r) * 15), sun);
    }
  } else {
    if (cloudy && (code == 29 || code == 30))
      iconCircle(buffer, 10, 10, 6, sun);
    iconCloud(buffer);
    if (rainy || thunderstorm) {
      for (int x = 12; x <= 28; x += 8)
        iconLine(buffer, x, 27, x - 2, 33, rain);
    }
    if (snowy) {
      for (int x = 12; x <= 28; x += 8) {
        pixel36(buffer, x, 30, snow);
        pixel36(buffer, x - 1, 30, snow);
        pixel36(buffer, x + 1, 30, snow);
        pixel36(buffer, x, 29, snow);
        pixel36(buffer, x, 31, snow);
      }
    }
    if (thunderstorm) {
      iconLine(buffer, 21, 24, 17, 31, thunder);
      iconLine(buffer, 17, 31, 22, 29, thunder);
      iconLine(buffer, 22, 29, 19, 35, thunder);
    }
  }
  if (gForecastIcon[slot]) lv_obj_invalidate(gForecastIcon[slot]);
}

void drawMoonPhase(float phase) {
  if (!gMoonIconBuffer) return;
  const uint16_t bg = lv_color_hex(kCardBg).full;
  const uint16_t dark = lv_color_hex(0x263746).full;
  const uint16_t light = lv_color_hex(0xF3E8C8).full;
  const uint16_t rim = lv_color_hex(0x8EA4B2).full;
  for (int i = 0; i < 44 * 44; ++i) gMoonIconBuffer[i] = bg;

  phase -= floorf(phase);
  if (phase < 0.0f) phase += 1.0f;
  const float angle = phase * 2.0f * PI;
  const float sinPhase = sinf(angle);
  const float minusCosPhase = -cosf(angle);
  constexpr int cx = 22;
  constexpr int cy = 22;
  constexpr float radius = 17.0f;

  for (int py = 4; py < 40; ++py) {
    for (int px = 4; px < 40; ++px) {
      const float nx = (px - cx) / radius;
      const float ny = (py - cy) / radius;
      const float r2 = nx * nx + ny * ny;
      if (r2 > 1.0f) continue;
      const float nz = sqrtf(fmaxf(0.0f, 1.0f - r2));
      const bool illuminated = nx * sinPhase + nz * minusCosPhase > 0.0f;
      const bool edge = r2 > 0.88f;
      gMoonIconBuffer[py * 44 + px] = edge ? rim : (illuminated ? light : dark);
    }
  }
  if (gMoonIcon) lv_obj_invalidate(gMoonIcon);
}

void pauseEvent(lv_event_t*) {
  gRadarPaused = !gRadarPaused;
  lv_label_set_text(gPauseLabel, gRadarPaused ? "PLAY" : "PAUZA");
}

void refreshEvent(lv_event_t*) { gManualRefresh = true; }

void backlightWakeEvent(lv_event_t* event) {
  if (!event || lv_event_get_code(event) != LV_EVENT_RELEASED) return;
  gBacklightWakeRequested = true;
}

void mapTapEvent(lv_event_t* event) {
  if (!event || lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  lv_obj_t* target = lv_event_get_target(event);
  lv_indev_t* input = lv_indev_get_act();
  if (!target || !input) return;

  lv_point_t point {};
  lv_area_t area {};
  lv_indev_get_point(input, &point);
  lv_obj_get_coords(target, &area);
  const int x = point.x - area.x1;
  const int y = point.y - area.y1;
  if (x < 0 || y < 0 || x >= Config::MAP_W || y >= Config::MAP_H) return;

  gMapTapX = static_cast<int16_t>(x);
  gMapTapY = static_cast<int16_t>(y);
  gMapTapPending = true;
}

lv_obj_t* makeButton(lv_obj_t* parent, int x, int width, uint32_t background,
                     const char* text, lv_event_cb_t callback,
                     lv_obj_t** labelOut = nullptr) {
  lv_obj_t* button = lv_btn_create(parent);
  lv_obj_set_pos(button, x, 3);
  lv_obj_set_size(button, width, 30);
  lv_obj_set_style_bg_color(button, lv_color_hex(background), 0);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(button, 0, 0);
  lv_obj_set_style_radius(button, 6, 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(label);
  if (labelOut) *labelOut = label;
  return button;
}


void pressurePixel(int x, int y, uint16_t color) {
  if (!gPressureBuffer || x < 0 || y < 0 ||
      x >= kPressureCanvasW || y >= kPressureCanvasH) return;
  gPressureBuffer[y * kPressureCanvasW + x] = color;
}

void pressureLine(int x0, int y0, int x1, int y1, uint16_t color,
                  bool dashed = false) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  int step = 0;
  while (true) {
    if (!dashed || ((step / 3) & 1) == 0) pressurePixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
    ++step;
  }
}

void pressureMarker(int x, int y, uint16_t color) {
  for (int py = -2; py <= 2; ++py)
    for (int px = -2; px <= 2; ++px)
      if (px * px + py * py <= 4) pressurePixel(x + px, y + py, color);
}

void drawPressureGraph(const BarometerSnapshot& barometer) {
  if (!gPressureBuffer || !gPressureCanvas) return;
  const uint16_t bg = lv_color_hex(0x081821).full;
  const uint16_t grid = lv_color_hex(0x27404E).full;
  const uint16_t historyColor = lv_color_hex(0x59C7F5).full;
  const uint16_t forecastColor = lv_color_hex(0xFFD166).full;
  const uint16_t nowColor = lv_color_hex(0xFFFFFF).full;
  for (int i = 0; i < kPressureCanvasW * kPressureCanvasH; ++i)
    gPressureBuffer[i] = bg;

  constexpr int xLeft = 3;
  constexpr int xNow = 130;
  constexpr int xRight = 180;
  constexpr int yTop = 4;
  constexpr int yBottom = 80;
  for (int row = 0; row <= 4; ++row) {
    const int y = yTop + row * (yBottom - yTop) / 4;
    pressureLine(xLeft, y, xRight, y, grid);
  }
  pressureLine(xLeft, yTop, xLeft, yBottom, grid);
  pressureLine((xLeft + xNow) / 2, yTop, (xLeft + xNow) / 2, yBottom, grid);
  pressureLine(xNow, yTop, xNow, yBottom, nowColor, true);
  pressureLine(xRight, yTop, xRight, yBottom, grid);

  if (!barometer.valid || barometer.historyCount == 0) {
    lv_obj_invalidate(gPressureCanvas);
    return;
  }

  float minimum = barometer.pressureHpa;
  float maximum = barometer.pressureHpa;
  const uint32_t nowMs = millis();
  for (size_t i = 0; i < barometer.historyCount; ++i) {
    const uint32_t age = static_cast<uint32_t>(nowMs - barometer.history[i].timestampMs);
    if (age > 24UL * 60UL * 60UL * 1000UL) continue;
    const float value = barometer.history[i].pressureHpa;
    if (!isfinite(value)) continue;
    minimum = fminf(minimum, value);
    maximum = fmaxf(maximum, value);
  }
  for (float value : barometer.projectedPressureHpa) {
    if (!isfinite(value)) continue;
    minimum = fminf(minimum, value);
    maximum = fmaxf(maximum, value);
  }
  if (maximum - minimum < 2.0f) {
    const float center = (maximum + minimum) * 0.5f;
    minimum = center - 1.0f;
    maximum = center + 1.0f;
  } else {
    const float pad = (maximum - minimum) * 0.10f;
    minimum -= pad;
    maximum += pad;
  }

  const auto mapY = [&](float pressure) {
    const float normalized = (pressure - minimum) / (maximum - minimum);
    return yBottom - static_cast<int>(lroundf(normalized * (yBottom - yTop)));
  };

  bool havePrevious = false;
  int previousX = 0;
  int previousY = 0;
  for (size_t i = 0; i < barometer.historyCount; ++i) {
    const PressurePoint& point = barometer.history[i];
    const uint32_t age = static_cast<uint32_t>(nowMs - point.timestampMs);
    if (age > 24UL * 60UL * 60UL * 1000UL || !isfinite(point.pressureHpa))
      continue;
    const float fraction = 1.0f - age / (24.0f * 60.0f * 60.0f * 1000.0f);
    const int x = xLeft + static_cast<int>(lroundf(fraction * (xNow - xLeft)));
    const int y = mapY(point.pressureHpa);
    if (havePrevious) pressureLine(previousX, previousY, x, y, historyColor);
    previousX = x;
    previousY = y;
    havePrevious = true;
  }
  const int currentY = mapY(barometer.pressureHpa);
  pressureMarker(xNow, currentY, nowColor);

  int forecastX0 = xNow;
  int forecastY0 = currentY;
  constexpr int hours[3] = {3, 6, 9};
  for (size_t i = 0; i < 3; ++i) {
    if (!isfinite(barometer.projectedPressureHpa[i])) continue;
    const int x = xNow + hours[i] * (xRight - xNow) / 9;
    const int y = mapY(barometer.projectedPressureHpa[i]);
    pressureLine(forecastX0, forecastY0, x, y, forecastColor, true);
    pressureMarker(x, y, forecastColor);
    forecastX0 = x;
    forecastY0 = y;
  }
  lv_obj_invalidate(gPressureCanvas);
}
}  // namespace

bool beginStartup(const char* versionText) {
  gStartupScreen = lv_scr_act();
  if (!gStartupScreen) return false;

  lv_obj_clean(gStartupScreen);
  applyPanelStyle(gStartupScreen, kScreenBg);

  makeLabel(gStartupScreen, 0, 48, Config::SCREEN_W,
            "RADAR / ADS-B / POCASI", &lv_font_montserrat_24,
            0xF2F7FA, LV_TEXT_ALIGN_CENTER);
  makeLabel(gStartupScreen, 0, 90, Config::SCREEN_W,
            "Waveshare ESP32-S3 Touch LCD 7 inch",
            &lv_font_montserrat_16, 0x83CFF4, LV_TEXT_ALIGN_CENTER);

  lv_obj_t* ring = lv_obj_create(gStartupScreen);
  lv_obj_set_pos(ring, 330, 132);
  lv_obj_set_size(ring, 140, 140);
  applyPanelStyle(ring, 0x081821, LV_RADIUS_CIRCLE, 0x254C61, 2);

  constexpr int centerX = 400;
  constexpr int centerY = 202;
  constexpr int radius = 58;
  for (uint8_t i = 0; i < 8; ++i) {
    const float angle = (static_cast<float>(i) * 45.0f - 90.0f) * DEG_TO_RAD;
    const int x = centerX + static_cast<int>(lroundf(cosf(angle) * radius)) - 7;
    const int y = centerY + static_cast<int>(lroundf(sinf(angle) * radius)) - 7;
    gStartupDots[i] = lv_obj_create(gStartupScreen);
    lv_obj_set_pos(gStartupDots[i], x, y);
    lv_obj_set_size(gStartupDots[i], 14, 14);
    applyPanelStyle(gStartupDots[i], 0x42B7F5, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(gStartupDots[i], LV_OPA_30, 0);
  }

  gStartupCenterLabel = makeLabel(gStartupScreen, 350, 190, 100, "START",
                                   &lv_font_montserrat_16, 0xFFD166,
                                   LV_TEXT_ALIGN_CENTER);

  gStartupStatus = makeLabel(gStartupScreen, 80, 306, 640,
                             "Spoustim system...",
                             &lv_font_montserrat_16, 0xDCE7ED,
                             LV_TEXT_ALIGN_CENTER);

  lv_obj_t* progressTrack = lv_obj_create(gStartupScreen);
  lv_obj_set_pos(progressTrack, 120, 350);
  lv_obj_set_size(progressTrack, 560, 12);
  applyPanelStyle(progressTrack, 0x173040, 6);

  gStartupProgressFill = lv_obj_create(progressTrack);
  lv_obj_set_pos(gStartupProgressFill, 0, 0);
  lv_obj_set_size(gStartupProgressFill, 4, 12);
  applyPanelStyle(gStartupProgressFill, 0x42B7F5, 6);

  gStartupProgressText = makeLabel(gStartupScreen, 350, 372, 100, "0 %",
                                    &lv_font_montserrat_12, 0x83CFF4,
                                    LV_TEXT_ALIGN_CENTER);

  char versionLine[72];
  snprintf(versionLine, sizeof(versionLine), "Firmware %s",
           versionText && versionText[0] ? versionText : "--");
  makeLabel(gStartupScreen, 0, 414, Config::SCREEN_W, versionLine,
            &lv_font_montserrat_12, 0x8298A5, LV_TEXT_ALIGN_CENTER);
  makeLabel(gStartupScreen, 0, 448, Config::SCREEN_W,
            "Vytvoril OK5TVR", &lv_font_montserrat_16, 0xFFD166,
            LV_TEXT_ALIGN_CENTER);

  gStartupAnimationPhase = 0;
  gStartupActive = true;
  if (gStartupAnimationTimer) lv_timer_del(gStartupAnimationTimer);
  gStartupAnimationTimer = lv_timer_create(startupAnimationTimer, 110, nullptr);
  startupAnimationTimer(nullptr);
  lv_obj_invalidate(gStartupScreen);
  return true;
}

void updateStartupStatus(const char* status, uint8_t progressPercent) {
  if (!gStartupActive) return;
  if (progressPercent > 100) progressPercent = 100;
  if (gStartupStatus)
    lv_label_set_text(gStartupStatus, status && status[0] ? status : "Pracuji...");
  if (gStartupProgressFill) {
    const int width = 4 + static_cast<int>(progressPercent) * 556 / 100;
    lv_obj_set_width(gStartupProgressFill, width);
  }
  if (gStartupProgressText) {
    char text[12];
    snprintf(text, sizeof(text), "%u %%", static_cast<unsigned>(progressPercent));
    lv_label_set_text(gStartupProgressText, text);
  }
}

void completeStartup(const char* finalStatus, bool success) {
  if (!gStartupActive) return;
  updateStartupStatus(finalStatus && finalStatus[0] ? finalStatus :
                      (success ? "System je pripraven" : "Chyba startu"), 100);
  if (gStartupAnimationTimer) {
    lv_timer_del(gStartupAnimationTimer);
    gStartupAnimationTimer = nullptr;
  }
  const uint32_t color = success ? 0x6EE7A5 : 0xFF8F70;
  for (lv_obj_t* dot : gStartupDots) {
    if (!dot) continue;
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  }
  if (gStartupCenterLabel) {
    lv_label_set_text(gStartupCenterLabel, success ? "OK" : "CHYBA");
    lv_obj_set_style_text_color(gStartupCenterLabel, lv_color_hex(color), 0);
  }
  if (gStartupProgressFill)
    lv_obj_set_style_bg_color(gStartupProgressFill, lv_color_hex(color), 0);
}

bool startupActive() { return gStartupActive; }

void showMainScreen() {
  if (!gMainScreen) return;
  lv_obj_t* previous = lv_scr_act();
  lv_scr_load(gMainScreen);
  gStartupActive = false;
  gStartupScreen = nullptr;
  gStartupStatus = nullptr;
  gStartupProgressFill = nullptr;
  gStartupProgressText = nullptr;
  gStartupCenterLabel = nullptr;
  for (lv_obj_t*& dot : gStartupDots) dot = nullptr;
  if (previous && previous != gMainScreen) lv_obj_del(previous);
}

void showOtaScreen(const char* filename, const char* versionText) {
  if (gOtaScreen) {
    lv_obj_del(gOtaScreen);
    gOtaScreen = nullptr;
  }

  gOtaScreen = lv_obj_create(nullptr);
  if (!gOtaScreen) return;
  applyPanelStyle(gOtaScreen, 0x061019);

  makeLabel(gOtaScreen, 0, 74, Config::SCREEN_W,
            "AKTUALIZACE FIRMWARE", &lv_font_montserrat_24, 0x42B7F5,
            LV_TEXT_ALIGN_CENTER);

  lv_obj_t* panel = lv_obj_create(gOtaScreen);
  lv_obj_set_pos(panel, 120, 142);
  lv_obj_set_size(panel, 560, 218);
  applyPanelStyle(panel, 0x0D202C, 14, 0x254C61, 2);

  gOtaStatus = makeLabel(panel, 30, 34, 500,
                         "Nahravam novy firmware...",
                         &lv_font_montserrat_16, 0xDCE7ED,
                         LV_TEXT_ALIGN_CENTER);

  gOtaBytes = makeLabel(panel, 30, 82, 500, "Nahrano: 0 kB",
                        &lv_font_montserrat_24, 0xFFD166,
                        LV_TEXT_ALIGN_CENTER);

  gOtaResult = makeLabel(panel, 30, 136, 500,
                         "NEVYPINEJTE ZARIZENI",
                         &lv_font_montserrat_16, 0xFF8F70,
                         LV_TEXT_ALIGN_CENTER);

  char fileLine[96];
  snprintf(fileLine, sizeof(fileLine), "Soubor: %s",
           filename && filename[0] ? filename : "firmware.bin");
  makeLabel(gOtaScreen, 80, 386, 640, fileLine,
            &lv_font_montserrat_12, 0x8298A5, LV_TEXT_ALIGN_CENTER);

  char versionLine[96];
  snprintf(versionLine, sizeof(versionLine), "Aktualni firmware: %s",
           versionText && versionText[0] ? versionText : "--");
  makeLabel(gOtaScreen, 80, 416, 640, versionLine,
            &lv_font_montserrat_12, 0x8298A5, LV_TEXT_ALIGN_CENTER);

  lv_scr_load(gOtaScreen);
  lv_obj_invalidate(gOtaScreen);
}

void updateOtaScreen(uint32_t bytesWritten) {
  if (!gOtaScreen) return;
  if (gOtaStatus) lv_label_set_text(gOtaStatus, "Nahravam novy firmware...");
  if (gOtaBytes) {
    char text[48];
    if (bytesWritten >= 1024U * 1024U) {
      const float mib = static_cast<float>(bytesWritten) / (1024.0f * 1024.0f);
      snprintf(text, sizeof(text), "Nahrano: %.2f MB", mib);
    } else {
      snprintf(text, sizeof(text), "Nahrano: %u kB",
               static_cast<unsigned>(bytesWritten / 1024U));
    }
    lv_label_set_text(gOtaBytes, text);
  }
  lv_obj_invalidate(gOtaScreen);
}

void finishOtaScreen(bool success, uint32_t bytesWritten, int errorCode) {
  if (!gOtaScreen) return;
  updateOtaScreen(bytesWritten);
  if (gOtaStatus) {
    lv_label_set_text(gOtaStatus,
                      success ? "Firmware byl uspesne nahran."
                              : "Aktualizace firmware selhala.");
    lv_obj_set_style_text_color(
        gOtaStatus, lv_color_hex(success ? 0x6EE7A5 : 0xFF8F70), 0);
  }
  if (gOtaResult) {
    if (success) {
      lv_label_set_text(gOtaResult, "RESTARTUJI ZARIZENI...");
      lv_obj_set_style_text_color(gOtaResult, lv_color_hex(0x6EE7A5), 0);
    } else {
      char text[64];
      snprintf(text, sizeof(text), "Chyba OTA: %d", errorCode);
      lv_label_set_text(gOtaResult, text);
      lv_obj_set_style_text_color(gOtaResult, lv_color_hex(0xFF8F70), 0);
    }
  }
  lv_obj_invalidate(gOtaScreen);
}

void hideOtaScreen() {
  if (!gMainScreen) return;
  lv_obj_t* ota = gOtaScreen;
  lv_scr_load(gMainScreen);
  gOtaScreen = nullptr;
  gOtaStatus = nullptr;
  gOtaBytes = nullptr;
  gOtaResult = nullptr;
  if (ota && ota != gMainScreen) lv_obj_del(ota);
}

bool begin() {
  gMainScreen = lv_obj_create(nullptr);
  if (!gMainScreen) return false;
  lv_obj_t* screen = gMainScreen;
  applyPanelStyle(screen, kScreenBg);

  const size_t mapBytes =
      static_cast<size_t>(Config::MAP_W) * Config::MAP_H * sizeof(uint16_t);

  for (uint8_t i = 0; i < kMapBufferCount; ++i) {
    gMapBuffer[i] = static_cast<uint16_t*>(heap_caps_malloc(
        mapBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!gMapBuffer[i]) {
      for (uint8_t j = 0; j < i; ++j) {
        heap_caps_free(gMapBuffer[j]);
        gMapBuffer[j] = nullptr;
      }
      return false;
    }
    const uint16_t initialColor = lv_color_hex(kScreenBg).full;
    for (uint16_t y = 0; y < Config::MAP_H; ++y) {
      uint16_t* row = gMapBuffer[i] + static_cast<size_t>(y) * Config::MAP_W;
      for (uint16_t x = 0; x < Config::MAP_W; ++x) row[x] = initialColor;
      if ((y & 0x0F) == 0x0F) delay(1);
    }
  }
  gDoubleBufferedMap = true;

  for (uint8_t i = 0; i < kForecastCardCount; ++i) {
    gIconBuffers[i] = static_cast<uint16_t*>(heap_caps_calloc(
        36U * 36U, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!gIconBuffers[i]) return false;
  }
  gMoonIconBuffer = static_cast<uint16_t*>(heap_caps_calloc(
      44U * 44U, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!gMoonIconBuffer) return false;
  gPressureBuffer = static_cast<uint16_t*>(heap_caps_calloc(
      static_cast<size_t>(kPressureCanvasW) * kPressureCanvasH,
      sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!gPressureBuffer) return false;

  // One full-width header keeps controls away from the weather cards and gives
  // the map the same vertical origin as the sidebar.
  lv_obj_t* header = lv_obj_create(screen);
  lv_obj_set_pos(header, 0, 0);
  lv_obj_set_size(header, Config::SCREEN_W, Config::HEADER_H);
  applyPanelStyle(header, kHeaderBg, 0, 0x1E3849, 1);
  gClockLabel = makeLabel(header, 10, 7, 205, "--:--:--  --.--.----",
                          &lv_font_montserrat_16, 0xF2F7FA);
  gHeaderLabel = makeLabel(header, 220, 10, 420, "Inicializace...",
                           &lv_font_montserrat_12, 0xAFC4D1);
  makeButton(header, 646, 70, 0x176B9A, "PAUZA", pauseEvent, &gPauseLabel);
  makeButton(header, 722, 72, 0x247A4B, "OBNOVIT", refreshEvent);

  for (uint8_t i = 0; i < kMapBufferCount; ++i) {
    gMapCanvas[i] = lv_canvas_create(screen);
    lv_obj_set_pos(gMapCanvas[i], 0, Config::HEADER_H);
    lv_canvas_set_buffer(gMapCanvas[i], gMapBuffer[i], Config::MAP_W,
                         Config::MAP_H, LV_IMG_CF_TRUE_COLOR);
    // Buffer was initialized in short row-sized writes above. Avoid another
    // full PSRAM fill here while the RGB DMA is already active.
    lv_obj_clear_flag(gMapCanvas[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(gMapCanvas[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(gMapCanvas[i], mapTapEvent, LV_EVENT_CLICKED, nullptr);
  }
  lv_obj_add_flag(gMapCanvas[gBackMap], LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* sidebar = lv_obj_create(screen);
  lv_obj_set_pos(sidebar, Config::MAP_W, Config::HEADER_H);
  lv_obj_set_size(sidebar, Config::SIDEBAR_W, Config::MAP_H);
  applyPanelStyle(sidebar, kSidebarBg, 0, 0x1D3444, 1);

  lv_obj_t* current = lv_obj_create(sidebar);
  lv_obj_set_pos(current, 4, 4);
  lv_obj_set_size(current, 192, 82);
  applyPanelStyle(current, kCardBg, 7, kCardBorder, 1);
  makeLabel(current, 9, 5, 174, "Dolni Vlkys  |  IPLZE179",
            &lv_font_montserrat_12, 0x83CFF4);
  gCurrentTemp = makeLabel(current, 9, 24, 78, "--.- C",
                           &lv_font_montserrat_24, 0xFFFFFF);
  gCurrentDetail1 = makeLabel(current, 88, 25, 94, "Vlhkost -- %",
                              &lv_font_montserrat_12, 0xDCE7ED);
  gCurrentDetail2 = makeLabel(current, 88, 43, 94, "Vitr -- km/h",
                              &lv_font_montserrat_12, 0xDCE7ED);
  gCurrentDetail3 = makeLabel(current, 9, 65, 174,
                              "Tlak ---- hPa  |  dest --.- mm/h",
                              &lv_font_montserrat_10, 0x9DB2BF);

  lv_obj_t* astronomy = lv_obj_create(sidebar);
  lv_obj_set_pos(astronomy, 4, 90);
  lv_obj_set_size(astronomy, 192, 94);
  applyPanelStyle(astronomy, kCardBg, 7, kCardBorder, 1);
  makeLabel(astronomy, 9, 5, 174, "SLUNCE A MESIC",
            &lv_font_montserrat_10, 0x83CFF4);
  gMoonIcon = lv_canvas_create(astronomy);
  lv_obj_set_pos(gMoonIcon, 7, 24);
  lv_canvas_set_buffer(gMoonIcon, gMoonIconBuffer, 44, 44,
                       LV_IMG_CF_TRUE_COLOR);
  gMoonPhaseLabel = makeLabel(astronomy, 2, 70, 54, "--",
                              &lv_font_montserrat_10, 0xDCE7ED,
                              LV_TEXT_ALIGN_CENTER);
  gSunLine = makeLabel(astronomy, 57, 24, 126, "S  V --:--  Z --:--",
                       &lv_font_montserrat_12, 0xFFD76A);
  gSunAltitude = makeLabel(astronomy, 57, 42, 126, "Vyska Slunce --.- st.",
                           &lv_font_montserrat_10, 0xDCE7ED);
  gMoonLine = makeLabel(astronomy, 57, 59, 126, "M  V --:--  Z --:--",
                        &lv_font_montserrat_12, 0xD6E0E7);
  gMoonAltitude = makeLabel(astronomy, 57, 77, 126, "Vyska --.- st. | --%",
                            &lv_font_montserrat_10, 0x9DB2BF);
  drawMoonPhase(0.0f);

  makeLabel(sidebar, 8, 188, 184, "PREDPOVED  +3 / +6 / +9 H",
            &lv_font_montserrat_10, 0x83CFF4);

  constexpr int cardW = 62;
  constexpr int cardH = 62;
  for (uint8_t i = 0; i < kForecastCardCount; ++i) {
    lv_obj_t* card = lv_obj_create(sidebar);
    lv_obj_set_pos(card, 4 + i * 64, 204);
    lv_obj_set_size(card, cardW, cardH);
    applyPanelStyle(card, kCardBg, 6, kCardBorder, 1);

    gForecastTime[i] = makeLabel(card, 3, 2, 56, "--",
                                 &lv_font_montserrat_10, 0xFFFFFF,
                                 LV_TEXT_ALIGN_CENTER);
    gForecastIcon[i] = lv_canvas_create(card);
    lv_obj_set_pos(gForecastIcon[i], 1, 20);
    lv_canvas_set_buffer(gForecastIcon[i], gIconBuffers[i], 36, 36,
                         LV_IMG_CF_TRUE_COLOR);
    gForecastTemp[i] = makeLabel(card, 36, 22, 23, "--",
                                 &lv_font_montserrat_10, 0xFFD76A,
                                 LV_TEXT_ALIGN_RIGHT);
    gForecastRain[i] = makeLabel(card, 36, 42, 23, "--%",
                                 &lv_font_montserrat_10, 0x62B8FF,
                                 LV_TEXT_ALIGN_RIGHT);
    drawIcon(i, 44);
  }

  lv_obj_t* pressureCard = lv_obj_create(sidebar);
  lv_obj_set_pos(pressureCard, 4, 270);
  lv_obj_set_size(pressureCard, 192, 124);
  applyPanelStyle(pressureCard, kCardBg, 7, kCardBorder, 1);
  gPressureTitle = makeLabel(pressureCard, 6, 3, 180,
                             "TLAK 24 H | ZAMBRETTI",
                             &lv_font_montserrat_10, 0x83CFF4,
                             LV_TEXT_ALIGN_CENTER);
  gPressureCanvas = lv_canvas_create(pressureCard);
  lv_obj_set_pos(gPressureCanvas, 4, 18);
  lv_canvas_set_buffer(gPressureCanvas, gPressureBuffer,
                       kPressureCanvasW, kPressureCanvasH,
                       LV_IMG_CF_TRUE_COLOR);
  makeLabel(pressureCard, 5, 108, 44, "-24 h",
            &lv_font_montserrat_10, 0x8298A5);
  makeLabel(pressureCard, 116, 108, 38, "ted",
            &lv_font_montserrat_10, 0xFFD76A,
            LV_TEXT_ALIGN_CENTER);
  makeLabel(pressureCard, 154, 108, 32, "+9 h",
            &lv_font_montserrat_10, 0x8298A5,
            LV_TEXT_ALIGN_RIGHT);

  lv_obj_t* pressureSummary = lv_obj_create(sidebar);
  lv_obj_set_pos(pressureSummary, 4, 398);
  lv_obj_set_size(pressureSummary, 192, 42);
  applyPanelStyle(pressureSummary, kCardBg, 7, kCardBorder, 1);
  gPressureValue = makeLabel(pressureSummary, 6, 4, 88, "----.- hPa",
                             &lv_font_montserrat_12, 0xFFFFFF);
  gPressureDelta = makeLabel(pressureSummary, 96, 5, 88, "D3h --.-",
                             &lv_font_montserrat_10, 0xFFD76A,
                             LV_TEXT_ALIGN_RIGHT);
  gPressureForecast = makeLabel(pressureSummary, 6, 21, 180,
                                "BARO: senzor nenalezen",
                                &lv_font_montserrat_10, 0x9DB2BF);
  lv_obj_set_height(gPressureForecast, 20);
  lv_label_set_long_mode(gPressureForecast, LV_LABEL_LONG_WRAP);

  // Invisible full-screen overlay is enabled only while the scheduled
  // backlight is off. It consumes the first touch so waking the display does
  // not also zoom the map or press a hidden button.
  gBacklightWakeOverlay = lv_obj_create(screen);
  lv_obj_set_pos(gBacklightWakeOverlay, 0, 0);
  lv_obj_set_size(gBacklightWakeOverlay, Config::SCREEN_W, Config::SCREEN_H);
  lv_obj_set_style_bg_opa(gBacklightWakeOverlay, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(gBacklightWakeOverlay, 0, 0);
  lv_obj_set_style_pad_all(gBacklightWakeOverlay, 0, 0);
  lv_obj_clear_flag(gBacklightWakeOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(gBacklightWakeOverlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(gBacklightWakeOverlay, backlightWakeEvent,
                      LV_EVENT_RELEASED, nullptr);
  lv_obj_add_flag(gBacklightWakeOverlay, LV_OBJ_FLAG_HIDDEN);

  return true;
}

lv_obj_t* mapCanvas() {
  return gMapCanvas[gDoubleBufferedMap ? gBackMap : gFrontMap];
}

uint16_t* mapBuffer() {
  return gMapBuffer[gDoubleBufferedMap ? gBackMap : gFrontMap];
}

void presentMap() {
  if (!gMapCanvas[gFrontMap]) return;

  if (!gDoubleBufferedMap || !gMapCanvas[gBackMap]) {
    lv_obj_invalidate(gMapCanvas[gFrontMap]);
    return;
  }

  lv_obj_clear_flag(gMapCanvas[gBackMap], LV_OBJ_FLAG_HIDDEN);
  lv_obj_invalidate(gMapCanvas[gBackMap]);
  lv_obj_add_flag(gMapCanvas[gFrontMap], LV_OBJ_FLAG_HIDDEN);

  const uint8_t previousFront = gFrontMap;
  gFrontMap = gBackMap;
  gBackMap = previousFront;
}

void invalidateMap() { presentMap(); }

void updateWeather(const WeatherSnapshot& weather) {
  char text[96];
  if (weather.current.valid) {
    snprintf(text, sizeof(text), "%.1f C", weather.current.temperatureC);
    lv_label_set_text(gCurrentTemp, text);
    snprintf(text, sizeof(text), "Vlhkost %.0f %%", weather.current.humidityPct);
    lv_label_set_text(gCurrentDetail1, text);
    snprintf(text, sizeof(text), "Vitr %.0f km/h", weather.current.windKph);
    lv_label_set_text(gCurrentDetail2, text);
    snprintf(text, sizeof(text), "Tlak %.0f hPa  |  dest %.1f mm/h",
             weather.current.pressureHpa, weather.current.rainRateMmH);
    lv_label_set_text(gCurrentDetail3, text);
  } else {
    lv_label_set_text(gCurrentTemp, "--.- C");
    lv_label_set_text(gCurrentDetail1, "Stanice bez dat");
    lv_label_set_text(gCurrentDetail2, "--");
    lv_label_set_text(gCurrentDetail3, weather.status);
  }

  for (uint8_t i = 0; i < kForecastCardCount; ++i) {
    const ForecastSlot& slot = weather.slots[i];
    if (!slot.valid) {
      lv_label_set_text(gForecastTime[i], "--");
      lv_label_set_text(gForecastTemp[i], "--");
      lv_label_set_text(gForecastRain[i], "--%");
      drawIcon(i, 44);
      continue;
    }
    lv_label_set_text(gForecastTime[i], slot.timeText);
    snprintf(text, sizeof(text), "%dC", slot.temperatureC);
    lv_label_set_text(gForecastTemp[i], text);
    snprintf(text, sizeof(text), "%d%%", slot.precipChancePct);
    lv_label_set_text(gForecastRain[i], text);
    drawIcon(i, slot.iconCode);
  }
}

void updatePressure(const BarometerSnapshot& barometer) {
  char text[112];
  if (!barometer.enabled) {
    lv_label_set_text(gPressureValue, "BARO vypnut");
    lv_label_set_text(gPressureDelta, "");
    lv_label_set_text(gPressureForecast, "Zapnete v nastaveni webu");
    lv_obj_set_style_text_color(gPressureForecast, lv_color_hex(0x9DB2BF), 0);
  } else if (!barometer.detected) {
    lv_label_set_text(gPressureValue, "Senzor chybi");
    lv_label_set_text(gPressureDelta, "I2C0 0x77");
    lv_label_set_text(gPressureForecast, "Zkontrolujte BMP180 a kabelaz");
    lv_obj_set_style_text_color(gPressureForecast, lv_color_hex(0xFF8F70), 0);
  } else if (!barometer.valid) {
    lv_label_set_text(gPressureValue, barometer.sensorName);
    lv_label_set_text(gPressureDelta, "chyba dat");
    lv_label_set_text(gPressureForecast, barometer.status);
    lv_obj_set_style_text_color(gPressureForecast, lv_color_hex(0xFF8F70), 0);
  } else {
    snprintf(text, sizeof(text), "%.1f hPa", barometer.pressureHpa);
    lv_label_set_text(gPressureValue, text);
    if (barometer.zambrettiReady && isfinite(barometer.delta3hHpa)) {
      snprintf(text, sizeof(text), "Z:%s  %+.1f",
               barometer.zambrettiCode, barometer.delta3hHpa);
      lv_label_set_text(gPressureDelta, text);
      snprintf(text, sizeof(text), "%s | %s",
               barometer.zambrettiTrend, barometer.forecastText);
      lv_label_set_text(gPressureForecast, text);
    } else {
      lv_label_set_text(gPressureDelta, "Z:- sbiram");
      snprintf(text, sizeof(text), "%s | %s", barometer.trendText,
               barometer.forecastText);
      lv_label_set_text(gPressureForecast, text);
    }

    uint32_t color = 0xFFD166;
    if (barometer.zambrettiReady) {
      const char code = barometer.zambrettiCode[0];
      if (code >= 'A' && code <= 'G') color = 0x6EE7A5;
      if (code >= 'R' && code <= 'Z') color = 0xFF8F70;
    }
    lv_obj_set_style_text_color(gPressureForecast, lv_color_hex(color), 0);
  }

  if (barometer.historyRevision != gPressureHistoryRevision) {
    gPressureHistoryRevision = barometer.historyRevision;
    drawPressureGraph(barometer);
  }
}

void updateAstronomy(const AstronomySnapshot& astronomy) {
  char text[80];
  if (!astronomy.valid) {
    lv_label_set_text(gSunLine, "S  V --:--  Z --:--");
    lv_label_set_text(gSunAltitude, astronomy.status);
    lv_label_set_text(gMoonLine, "M  V --:--  Z --:--");
    lv_label_set_text(gMoonAltitude, "Vyska --.- st. | --%");
    lv_label_set_text(gMoonPhaseLabel, "--");
    drawMoonPhase(0.0f);
    return;
  }

  snprintf(text, sizeof(text), "S  V %s  Z %s", astronomy.sunrise,
           astronomy.sunset);
  lv_label_set_text(gSunLine, text);
  snprintf(text, sizeof(text), "Vyska Slunce %+.1f st.",
           astronomy.sunAltitudeDeg);
  lv_label_set_text(gSunAltitude, text);

  snprintf(text, sizeof(text), "M  V %s  Z %s", astronomy.moonrise,
           astronomy.moonset);
  lv_label_set_text(gMoonLine, text);
  snprintf(text, sizeof(text), "Vyska %+.1f st. | %.0f%%",
           astronomy.moonAltitudeDeg, astronomy.moonIlluminationPct);
  lv_label_set_text(gMoonAltitude, text);
  lv_label_set_text(gMoonPhaseLabel, astronomy.moonPhaseName);
  drawMoonPhase(astronomy.moonPhase);
}

void updateHeader(const char* networkStatus, const char* radarStatus,
                  const AircraftSnapshot& aircraft) {
  char clockText[32] = "--:--:--  --.--.----";
  const time_t epoch = time(nullptr);
  if (epoch > 1700000000) {
    struct tm localTime {};
    localtime_r(&epoch, &localTime);
    strftime(clockText, sizeof(clockText), "%H:%M:%S  %d.%m.%Y",
             &localTime);
  }
  if (gClockLabel) lv_label_set_text(gClockLabel, clockText);

  char text[224];
  snprintf(text, sizeof(text), "%s  |  %s  |  %s",
           networkStatus && networkStatus[0] ? networkStatus : "Sit offline",
           radarStatus && radarStatus[0] ? radarStatus : "Radar --",
           aircraft.status);
  lv_label_set_text(gHeaderLabel, text);
}

bool radarPaused() { return gRadarPaused; }

bool consumeManualRefresh() {
  const bool value = gManualRefresh;
  gManualRefresh = false;
  return value;
}

bool consumeMapTap(int16_t& x, int16_t& y) {
  if (!gMapTapPending) return false;
  x = gMapTapX;
  y = gMapTapY;
  gMapTapPending = false;
  return true;
}

void setBacklightWakeOverlay(bool enabled) {
  if (!gBacklightWakeOverlay) return;
  if (enabled) {
    lv_obj_clear_flag(gBacklightWakeOverlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(gBacklightWakeOverlay);
  } else {
    lv_obj_add_flag(gBacklightWakeOverlay, LV_OBJ_FLAG_HIDDEN);
  }
}

bool consumeBacklightWakeRequest() {
  const bool requested = gBacklightWakeRequested;
  gBacklightWakeRequested = false;
  return requested;
}

}  // namespace UI
