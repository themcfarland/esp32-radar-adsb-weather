#include "map_renderer.h"

#include <math.h>

#include "config.h"

namespace MapRenderer {
namespace {
struct GeoPoint {
  float lon;
  float lat;
};

struct City {
  const char* name;
  float lat;
  float lon;
};

// Detailed 300-point Czech Republic border derived from geoBoundaries ADM0.
constexpr GeoPoint kCzechBorder[] = {
    {16.812237f, 50.191014f},
    {16.783348f, 50.145599f},
    {16.706029f, 50.096584f},
    {16.641270f, 50.112173f},
    {16.561117f, 50.163879f},
    {16.557353f, 50.220381f},
    {16.522873f, 50.239357f},
    {16.431301f, 50.324702f},
    {16.399263f, 50.319191f},
    {16.360662f, 50.379550f},
    {16.278635f, 50.367445f},
    {16.252925f, 50.405644f},
    {16.221848f, 50.406923f},
    {16.205009f, 50.448653f},
    {16.343805f, 50.495841f},
    {16.401786f, 50.529888f},
    {16.404650f, 50.569273f},
    {16.444912f, 50.579571f},
    {16.343087f, 50.661510f},
    {16.234819f, 50.671571f},
    {16.217361f, 50.633348f},
    {16.100774f, 50.662511f},
    {16.056083f, 50.609706f},
    {15.986714f, 50.615222f},
    {16.008550f, 50.634493f},
    {15.967169f, 50.691726f},
    {15.860964f, 50.674452f},
    {15.816195f, 50.755323f},
    {15.705706f, 50.737260f},
    {15.578751f, 50.778970f},
    {15.524309f, 50.777003f},
    {15.439524f, 50.809057f},
    {15.394134f, 50.776547f},
    {15.353740f, 50.851380f},
    {15.277063f, 50.891022f},
    {15.274077f, 50.979547f},
    {15.236557f, 50.998670f},
    {15.191839f, 50.980547f},
    {15.171849f, 51.020031f},
    {15.129002f, 50.990129f},
    {15.057722f, 51.022509f},
    {14.985420f, 51.010842f},
    {14.968245f, 50.990014f},
    {15.021714f, 50.967073f},
    {14.989454f, 50.921642f},
    {15.001928f, 50.868780f},
    {14.829589f, 50.872752f},
    {14.801634f, 50.825236f},
    {14.722343f, 50.822064f},
    {14.708602f, 50.840780f},
    {14.618915f, 50.857760f},
    {14.650200f, 50.931524f},
    {14.581937f, 50.913548f},
    {14.560622f, 50.925240f},
    {14.599073f, 50.987175f},
    {14.564551f, 51.010167f},
    {14.453830f, 51.035944f},
    {14.419835f, 51.019057f},
    {14.301713f, 51.055001f},
    {14.258684f, 50.987535f},
    {14.328182f, 50.973097f},
    {14.311441f, 50.954011f},
    {14.402130f, 50.923854f},
    {14.388003f, 50.899200f},
    {14.304750f, 50.883928f},
    {14.233366f, 50.887599f},
    {14.223501f, 50.859087f},
    {14.029924f, 50.804256f},
    {13.990740f, 50.820006f},
    {13.938987f, 50.789929f},
    {13.903907f, 50.794243f},
    {13.902774f, 50.752999f},
    {13.854940f, 50.726954f},
    {13.664672f, 50.732085f},
    {13.602734f, 50.710154f},
    {13.525240f, 50.704387f},
    {13.544372f, 50.677549f},
    {13.464856f, 50.601779f},
    {13.418589f, 50.615377f},
    {13.391814f, 50.646625f},
    {13.291093f, 50.574929f},
    {13.255925f, 50.595386f},
    {13.195291f, 50.503241f},
    {13.056123f, 50.501236f},
    {13.031694f, 50.509764f},
    {13.019866f, 50.446556f},
    {12.936178f, 50.411818f},
    {12.819032f, 50.460291f},
    {12.734469f, 50.432338f},
    {12.707114f, 50.397118f},
    {12.673272f, 50.416804f},
    {12.512028f, 50.397257f},
    {12.492628f, 50.356217f},
    {12.404663f, 50.324045f},
    {12.394147f, 50.289583f},
    {12.331290f, 50.242448f},
    {12.319540f, 50.171722f},
    {12.274651f, 50.196547f},
    {12.293903f, 50.221034f},
    {12.239473f, 50.246152f},
    {12.253840f, 50.270975f},
    {12.201321f, 50.272836f},
    {12.184602f, 50.322220f},
    {12.124940f, 50.315227f},
    {12.138285f, 50.275020f},
    {12.097611f, 50.262304f},
    {12.108940f, 50.237981f},
    {12.215968f, 50.168202f},
    {12.199565f, 50.110824f},
    {12.317279f, 50.053369f},
    {12.367491f, 50.017423f},
    {12.400430f, 50.015020f},
    {12.499557f, 49.972045f},
    {12.478334f, 49.935533f},
    {12.547673f, 49.920492f},
    {12.497977f, 49.837524f},
    {12.473055f, 49.833667f},
    {12.469327f, 49.787144f},
    {12.404760f, 49.762780f},
    {12.442450f, 49.703816f},
    {12.521986f, 49.686440f},
    {12.528103f, 49.618098f},
    {12.560741f, 49.619605f},
    {12.574348f, 49.559132f},
    {12.644195f, 49.522962f},
    {12.655552f, 49.434795f},
    {12.708703f, 49.424756f},
    {12.757668f, 49.394798f},
    {12.803089f, 49.341623f},
    {12.880011f, 49.350403f},
    {12.950035f, 49.342907f},
    {13.029114f, 49.304325f},
    {13.033984f, 49.263926f},
    {13.176902f, 49.164227f},
    {13.170297f, 49.144002f},
    {13.236062f, 49.113713f},
    {13.289185f, 49.118632f},
    {13.344185f, 49.088889f},
    {13.405832f, 49.023843f},
    {13.402727f, 48.987220f},
    {13.508278f, 48.942135f},
    {13.507074f, 48.969110f},
    {13.585856f, 48.968039f},
    {13.621579f, 48.948983f},
    {13.671437f, 48.880140f},
    {13.730556f, 48.887113f},
    {13.764423f, 48.834475f},
    {13.815234f, 48.797088f},
    {13.813179f, 48.773997f},
    {13.876116f, 48.766604f},
    {13.955807f, 48.714452f},
    {14.003281f, 48.708583f},
    {14.053526f, 48.652926f},
    {14.010561f, 48.639654f},
    {14.054241f, 48.604132f},
    {14.271549f, 48.581374f},
    {14.327116f, 48.554074f},
    {14.386245f, 48.592674f},
    {14.431415f, 48.589132f},
    {14.468573f, 48.646211f},
    {14.564022f, 48.608406f},
    {14.603138f, 48.628095f},
    {14.663460f, 48.581959f},
    {14.721138f, 48.602379f},
    {14.712342f, 48.650098f},
    {14.736567f, 48.698932f},
    {14.808133f, 48.733883f},
    {14.825527f, 48.783850f},
    {14.955791f, 48.758086f},
    {14.972938f, 48.874655f},
    {14.992919f, 48.903712f},
    {14.978419f, 48.980444f},
    {15.020543f, 49.020530f},
    {15.064843f, 48.999586f},
    {15.156272f, 48.993291f},
    {15.190289f, 48.943284f},
    {15.261618f, 48.953648f},
    {15.291028f, 48.984319f},
    {15.366975f, 48.981884f},
    {15.424755f, 48.951607f},
    {15.468491f, 48.951818f},
    {15.513392f, 48.914122f},
    {15.619602f, 48.895611f},
    {15.689694f, 48.855675f},
    {15.753664f, 48.852174f},
    {15.779535f, 48.874883f},
    {15.852176f, 48.867154f},
    {15.884598f, 48.842010f},
    {15.958383f, 48.823061f},
    {15.994366f, 48.779336f},
    {16.102688f, 48.745420f},
    {16.153971f, 48.748790f},
    {16.359770f, 48.728066f},
    {16.410299f, 48.743291f},
    {16.448699f, 48.799376f},
    {16.540751f, 48.814282f},
    {16.593585f, 48.782668f},
    {16.663744f, 48.781009f},
    {16.682585f, 48.727788f},
    {16.747282f, 48.732113f},
    {16.775787f, 48.711943f},
    {16.906190f, 48.714679f},
    {16.918755f, 48.622428f},
    {16.956707f, 48.623842f},
    {16.967874f, 48.668565f},
    {17.043284f, 48.764258f},
    {17.088656f, 48.785514f},
    {17.110853f, 48.831192f},
    {17.201995f, 48.878148f},
    {17.321663f, 48.845297f},
    {17.361318f, 48.813516f},
    {17.396852f, 48.813324f},
    {17.453189f, 48.846744f},
    {17.528480f, 48.812160f},
    {17.595247f, 48.828557f},
    {17.632977f, 48.854890f},
    {17.703332f, 48.860023f},
    {17.781313f, 48.925275f},
    {17.885305f, 48.927677f},
    {17.924309f, 49.019961f},
    {18.024466f, 49.021231f},
    {18.070771f, 49.037773f},
    {18.115540f, 49.090081f},
    {18.106742f, 49.134187f},
    {18.136706f, 49.161020f},
    {18.146964f, 49.247930f},
    {18.184018f, 49.286996f},
    {18.248105f, 49.294740f},
    {18.378911f, 49.330545f},
    {18.415559f, 49.367548f},
    {18.411938f, 49.399078f},
    {18.449949f, 49.393501f},
    {18.547944f, 49.467859f},
    {18.545742f, 49.500506f},
    {18.711946f, 49.502247f},
    {18.754475f, 49.488378f},
    {18.840204f, 49.514914f},
    {18.839224f, 49.560806f},
    {18.804583f, 49.678872f},
    {18.740969f, 49.676539f},
    {18.709552f, 49.704487f},
    {18.625213f, 49.722377f},
    {18.569428f, 49.834392f},
    {18.603876f, 49.857110f},
    {18.565669f, 49.880664f},
    {18.572907f, 49.921616f},
    {18.534370f, 49.899600f},
    {18.490922f, 49.903387f},
    {18.430752f, 49.938157f},
    {18.346414f, 49.940210f},
    {18.322377f, 49.916311f},
    {18.276309f, 49.964633f},
    {18.214288f, 49.972004f},
    {18.206573f, 49.997934f},
    {18.153839f, 49.982367f},
    {18.093285f, 50.014995f},
    {18.089363f, 50.044117f},
    {18.033607f, 50.066019f},
    {18.008825f, 50.031283f},
    {18.035299f, 50.011006f},
    {17.954045f, 50.005066f},
    {17.922849f, 49.978750f},
    {17.868499f, 49.972489f},
    {17.827692f, 50.011304f},
    {17.777351f, 50.020299f},
    {17.730882f, 50.097161f},
    {17.650345f, 50.110807f},
    {17.601157f, 50.169627f},
    {17.704661f, 50.184904f},
    {17.758463f, 50.206572f},
    {17.764978f, 50.236392f},
    {17.724955f, 50.256751f},
    {17.749650f, 50.301165f},
    {17.686927f, 50.327977f},
    {17.689124f, 50.301861f},
    {17.612099f, 50.266008f},
    {17.495593f, 50.275090f},
    {17.438235f, 50.251663f},
    {17.420674f, 50.277423f},
    {17.337738f, 50.283474f},
    {17.348665f, 50.328351f},
    {17.291565f, 50.317559f},
    {17.201146f, 50.364017f},
    {17.203269f, 50.386360f},
    {17.143334f, 50.380422f},
    {17.110752f, 50.404963f},
    {17.052537f, 50.406962f},
    {16.998296f, 50.427751f},
    {16.974016f, 50.417757f},
    {16.907905f, 50.449450f},
    {16.860781f, 50.411506f},
    {16.907097f, 50.391314f},
    {16.955854f, 50.312223f},
    {17.002641f, 50.302119f},
    {17.001555f, 50.256397f},
    {17.028316f, 50.229991f},
    {16.998544f, 50.215883f},
    {16.975923f, 50.244818f},
    {16.882740f, 50.199842f},
    {16.812237f, 50.191014f},
};

constexpr City kCities[] = {
    {"Praha", 50.0755f, 14.4378f},
    {"Plzen", 49.7384f, 13.3736f},
    {"Brno", 49.1951f, 16.6068f},
    {"Ostrava", 49.8209f, 18.2625f},
    {"Liberec", 50.7663f, 15.0543f},
    {"Olomouc", 49.5938f, 17.2509f},
    {"Hradec K.", 50.2104f, 15.8252f},
    {"C. Budejovice", 48.9745f, 14.4743f},
    {"Karlovy Vary", 50.2319f, 12.8710f},
};

uint16_t color(uint32_t rgb) { return lv_color_hex(rgb).full; }

float mercatorY(float latitudeDeg) {
  const float latitude = constrain(latitudeDeg, -85.0f, 85.0f) * DEG_TO_RAD;
  return logf(tanf(PI * 0.25f + latitude * 0.5f));
}

float inverseMercatorY(float value) {
  return atanf(sinhf(value)) * RAD_TO_DEG;
}

uint16_t radiusForMode(MapZoomMode mode) {
  switch (mode) {
    case MapZoomMode::Km50:
      return 50;
    case MapZoomMode::Km25:
      return 25;
    case MapZoomMode::Km10:
      return 10;
    case MapZoomMode::Full:
    default:
      return 0;
  }
}

int mapX(float lon, uint16_t width, const MapViewport& viewport) {
  const float span = viewport.lonRight - viewport.lonLeft;
  if (span <= 0.0f || width < 2) return 0;
  return lroundf((lon - viewport.lonLeft) / span * (width - 1));
}

int mapY(float lat, uint16_t height, const MapViewport& viewport) {
  if (height < 2) return 0;
  const float top = mercatorY(viewport.latTop);
  const float bottom = mercatorY(viewport.latBottom);
  if (top <= bottom) return 0;
  return lroundf((top - mercatorY(lat)) / (top - bottom) * (height - 1));
}

bool geoVisible(float lon, float lat, const MapViewport& viewport) {
  return lon >= viewport.lonLeft && lon <= viewport.lonRight &&
         lat >= viewport.latBottom && lat <= viewport.latTop;
}

void putPixel(uint16_t* buffer, uint16_t width, uint16_t height, int x, int y,
              uint16_t c) {
  if (x < 0 || y < 0 || x >= width || y >= height) return;
  buffer[static_cast<size_t>(y) * width + x] = c;
}

enum ClipCode : uint8_t {
  kInside = 0,
  kLeft = 1,
  kRight = 2,
  kBottom = 4,
  kTop = 8,
};

uint8_t clipCode(float x, float y, uint16_t width, uint16_t height) {
  uint8_t code = kInside;
  if (x < 0.0f)
    code |= kLeft;
  else if (x > static_cast<float>(width - 1))
    code |= kRight;
  if (y < 0.0f)
    code |= kTop;
  else if (y > static_cast<float>(height - 1))
    code |= kBottom;
  return code;
}

bool clipLineToCanvas(int& x0, int& y0, int& x1, int& y1, uint16_t width,
                      uint16_t height) {
  float ax = static_cast<float>(x0);
  float ay = static_cast<float>(y0);
  float bx = static_cast<float>(x1);
  float by = static_cast<float>(y1);

  while (true) {
    const uint8_t codeA = clipCode(ax, ay, width, height);
    const uint8_t codeB = clipCode(bx, by, width, height);
    if ((codeA | codeB) == 0) {
      x0 = lroundf(ax);
      y0 = lroundf(ay);
      x1 = lroundf(bx);
      y1 = lroundf(by);
      return true;
    }
    if ((codeA & codeB) != 0) return false;

    const uint8_t outside = codeA ? codeA : codeB;
    float x = 0.0f;
    float y = 0.0f;
    if (outside & kTop) {
      if (fabsf(by - ay) < 1e-6f) return false;
      x = ax + (bx - ax) * (0.0f - ay) / (by - ay);
      y = 0.0f;
    } else if (outside & kBottom) {
      if (fabsf(by - ay) < 1e-6f) return false;
      y = static_cast<float>(height - 1);
      x = ax + (bx - ax) * (y - ay) / (by - ay);
    } else if (outside & kRight) {
      if (fabsf(bx - ax) < 1e-6f) return false;
      x = static_cast<float>(width - 1);
      y = ay + (by - ay) * (x - ax) / (bx - ax);
    } else {
      if (fabsf(bx - ax) < 1e-6f) return false;
      x = 0.0f;
      y = ay + (by - ay) * (0.0f - ax) / (bx - ax);
    }

    if (outside == codeA) {
      ax = x;
      ay = y;
    } else {
      bx = x;
      by = y;
    }
  }
}

void line(uint16_t* buffer, uint16_t width, uint16_t height, int x0, int y0,
          int x1, int y1, uint16_t c) {
  if (!clipLineToCanvas(x0, y0, x1, y1, width, height)) return;
  int dx = abs(x1 - x0);
  const int sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0);
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    putPixel(buffer, width, height, x0, y0, c);
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

void fillRect(uint16_t* buffer, uint16_t width, uint16_t height, int x, int y,
              int rectWidth, int rectHeight, uint16_t c) {
  const int left = x > 0 ? x : 0;
  const int top = y > 0 ? y : 0;
  const int right = (x + rectWidth) < static_cast<int>(width)
                        ? (x + rectWidth)
                        : static_cast<int>(width);
  const int bottom = (y + rectHeight) < static_cast<int>(height)
                         ? (y + rectHeight)
                         : static_cast<int>(height);
  for (int py = top; py < bottom; ++py) {
    uint16_t* row = buffer + static_cast<size_t>(py) * width;
    for (int px = left; px < right; ++px) row[px] = c;
  }
}

void circle(uint16_t* buffer, uint16_t width, uint16_t height, int cx, int cy,
            int radius, uint16_t c) {
  if (cx + radius < 0 || cy + radius < 0 || cx - radius >= width ||
      cy - radius >= height) {
    return;
  }
  for (int y = -radius; y <= radius; ++y) {
    for (int x = -radius; x <= radius; ++x) {
      if (x * x + y * y <= radius * radius)
        putPixel(buffer, width, height, cx + x, cy + y, c);
    }
  }
}

void circleOutline(uint16_t* buffer, uint16_t width, uint16_t height, int cx,
                   int cy, int radius, uint16_t c) {
  int x = radius;
  int y = 0;
  int error = 1 - radius;
  while (x >= y) {
    putPixel(buffer, width, height, cx + x, cy + y, c);
    putPixel(buffer, width, height, cx + y, cy + x, c);
    putPixel(buffer, width, height, cx - y, cy + x, c);
    putPixel(buffer, width, height, cx - x, cy + y, c);
    putPixel(buffer, width, height, cx - x, cy - y, c);
    putPixel(buffer, width, height, cx - y, cy - x, c);
    putPixel(buffer, width, height, cx + y, cy - x, c);
    putPixel(buffer, width, height, cx + x, cy - y, c);
    ++y;
    if (error < 0) {
      error += 2 * y + 1;
    } else {
      --x;
      error += 2 * (y - x) + 1;
    }
  }
}

uint16_t altitudeColor(int32_t altitudeFt) {
  if (altitudeFt < 0) return color(0xD0D9DF);
  if (altitudeFt < 5000) return color(0x58E487);
  if (altitudeFt < 15000) return color(0xF8E45C);
  if (altitudeFt < 30000) return color(0xFF9F43);
  return color(0xDF6CFF);
}

void drawText(lv_obj_t* canvas, int x, int y, int maxWidth, const char* text,
              uint32_t rgb, const lv_font_t* font = &lv_font_montserrat_12,
              lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
  lv_draw_label_dsc_t label;
  lv_draw_label_dsc_init(&label);
  label.color = lv_color_hex(rgb);
  label.font = font;
  label.align = align;
  lv_canvas_draw_text(canvas, x, y, maxWidth, &label, text);
}

void drawLegend(lv_obj_t* canvas, uint16_t* buffer, uint16_t width,
                uint16_t height) {
  constexpr uint32_t kColors[] = {0x2A65B7, 0x00A8D8, 0x00B85A, 0x9BCB32,
                                  0xF2D035, 0xF38A27, 0xD9362B, 0xA22C9D};
  fillRect(buffer, width, height, 8, 8, 224, 34, color(0x071018));
  drawText(canvas, 14, 12, 32, "dBZ", 0xDCE8EF, &lv_font_montserrat_10);
  for (int i = 0; i < 8; ++i) {
    fillRect(buffer, width, height, 46 + i * 21, 12, 21, 9,
             color(kColors[i]));
  }
  drawText(canvas, 44, 24, 176, "10    20    30    40    50    60+",
           0xB8C8D2, &lv_font_montserrat_10);
}

void drawLightningTrailLegend(lv_obj_t* canvas, uint16_t* buffer,
                              uint16_t width, uint16_t height) {
  (void)height;
  fillRect(buffer, width, height, 8, 46, 224, 32, color(0x071018));
  drawText(canvas, 14, 50, 44, "BLESKY", 0xDCE8EF, &lv_font_montserrat_10);

  constexpr uint32_t kTrailColors[] = {
      0xFFFFFF, 0xFFE000, 0xFF8000, 0xFF2828};
  constexpr const char* kTrailLabels[] = {
      "0-2", "2-5", "5-10", "10-20"};
  constexpr int kXs[] = {58, 98, 138, 184};
  for (int i = 0; i < 4; ++i) {
    fillRect(buffer, width, height, kXs[i], 50, 12, 8, color(kTrailColors[i]));
    drawText(canvas, kXs[i] - 3, 62, i == 3 ? 42 : 34, kTrailLabels[i],
             0xB8C8D2, &lv_font_montserrat_10);
  }
}

void drawZoomBadge(lv_obj_t* canvas, uint16_t* buffer, uint16_t width,
                   uint16_t height, const MapViewport& viewport) {
  (void)height;
  char text[40];
  if (viewport.mode == MapZoomMode::Full) {
    snprintf(text, sizeof(text), "MAPA: CELA CR");
  } else {
    snprintf(text, sizeof(text), "OKOLI: %u km",
             static_cast<unsigned>(viewport.radiusKm));
  }
  constexpr int badgeWidth = 126;
  const int x = (static_cast<int>(width) - badgeWidth) / 2;
  fillRect(buffer, width, height, x, 8, badgeWidth, 28, color(0x071018));
  drawText(canvas, x + 6, 14, badgeWidth - 12, text, 0xFFFFFF,
           &lv_font_montserrat_12, LV_TEXT_ALIGN_CENTER);
}

void drawGeographicCircle(uint16_t* buffer, uint16_t width, uint16_t height,
                          const MapViewport& viewport, float centerLatDeg,
                          float centerLonDeg, float radiusKm, uint16_t c) {
  if (!buffer || radiusKm <= 0.0f) return;

  constexpr float kEarthRadiusKm = 6371.0088f;
  constexpr int kSegments = 96;
  const float angularDistance = radiusKm / kEarthRadiusKm;
  const float centerLat = centerLatDeg * DEG_TO_RAD;
  const float centerLon = centerLonDeg * DEG_TO_RAD;

  int previousX = 0;
  int previousY = 0;
  bool havePrevious = false;
  for (int i = 0; i <= kSegments; ++i) {
    const float bearing = 2.0f * PI * static_cast<float>(i) /
                          static_cast<float>(kSegments);
    const float lat = asinf(sinf(centerLat) * cosf(angularDistance) +
                            cosf(centerLat) * sinf(angularDistance) *
                                cosf(bearing));
    const float lon = centerLon +
                      atan2f(sinf(bearing) * sinf(angularDistance) *
                                 cosf(centerLat),
                             cosf(angularDistance) -
                                 sinf(centerLat) * sinf(lat));
    const int x = mapX(lon * RAD_TO_DEG, width, viewport);
    const int y = mapY(lat * RAD_TO_DEG, height, viewport);
    if (havePrevious) {
      line(buffer, width, height, previousX, previousY, x, y, c);
    }
    previousX = x;
    previousY = y;
    havePrevious = true;
  }
}

void drawLightningProximityAlert(uint16_t* buffer, uint16_t width,
                                 uint16_t height,
                                 const MapViewport& viewport) {
  const uint16_t red = color(0xFF2020);
  // Outer edge is the requested true 10 km geodesic radius. The second line
  // sits just inside it to remain visible even in the full-country view.
  drawGeographicCircle(buffer, width, height, viewport, Config::FALLBACK_LAT,
                       Config::FALLBACK_LON,
                       Config::LIGHTNING_ALERT_RADIUS_KM, red);
  drawGeographicCircle(buffer, width, height, viewport, Config::FALLBACK_LAT,
                       Config::FALLBACK_LON,
                       Config::LIGHTNING_ALERT_RADIUS_KM - 0.20f, red);
}

void drawStation(uint16_t* buffer, uint16_t width, uint16_t height,
                 const MapViewport& viewport) {
  if (!geoVisible(Config::FALLBACK_LON, Config::FALLBACK_LAT, viewport)) return;
  const int x = mapX(Config::FALLBACK_LON, width, viewport);
  const int y = mapY(Config::FALLBACK_LAT, height, viewport);
  const uint16_t outer = color(0xFFFFFF);
  const uint16_t inner = color(0x00D8FF);
  circle(buffer, width, height, x, y, 5, outer);
  circle(buffer, width, height, x, y, 3, inner);
  line(buffer, width, height, x - 8, y, x + 8, y, outer);
  line(buffer, width, height, x, y - 8, x, y + 8, outer);
}

float gridStepForMode(MapZoomMode mode) {
  switch (mode) {
    case MapZoomMode::Km50:
      return 0.25f;
    case MapZoomMode::Km25:
      return 0.10f;
    case MapZoomMode::Km10:
      return 0.05f;
    case MapZoomMode::Full:
    default:
      return 1.0f;
  }
}
}  // namespace

MapViewport makeViewport(MapZoomMode mode, float centerLat, float centerLon,
                         uint16_t width, uint16_t height) {
  MapViewport viewport;
  viewport.mode = mode;
  viewport.radiusKm = radiusForMode(mode);

  const float fullCenterLat = (Config::MAP_LAT_TOP + Config::MAP_LAT_BOTTOM) * 0.5f;
  const float fullCenterLon = (Config::MAP_LON_LEFT + Config::MAP_LON_RIGHT) * 0.5f;
  if (mode == MapZoomMode::Full || !isfinite(centerLat) || !isfinite(centerLon) ||
      width < 2 || height < 2) {
    viewport.mode = MapZoomMode::Full;
    viewport.radiusKm = 0;
    viewport.centerLat = fullCenterLat;
    viewport.centerLon = fullCenterLon;
    viewport.lonLeft = Config::MAP_LON_LEFT;
    viewport.lonRight = Config::MAP_LON_RIGHT;
    viewport.latTop = Config::MAP_LAT_TOP;
    viewport.latBottom = Config::MAP_LAT_BOTTOM;
    return viewport;
  }

  constexpr float kKmPerLatDegree = 111.32f;
  const float halfLat = viewport.radiusKm / kKmPerLatDegree;
  centerLat = constrain(centerLat, Config::MAP_LAT_BOTTOM + halfLat,
                        Config::MAP_LAT_TOP - halfLat);

  const float aspect = static_cast<float>(width) / static_cast<float>(height);
  const float cosLat = fmaxf(0.25f, cosf(centerLat * DEG_TO_RAD));
  float halfLon = viewport.radiusKm * aspect / (kKmPerLatDegree * cosLat);
  const float maximumHalfLon =
      (Config::MAP_LON_RIGHT - Config::MAP_LON_LEFT) * 0.5f;
  halfLon = fminf(halfLon, maximumHalfLon);
  centerLon = constrain(centerLon, Config::MAP_LON_LEFT + halfLon,
                        Config::MAP_LON_RIGHT - halfLon);

  viewport.centerLat = centerLat;
  viewport.centerLon = centerLon;
  viewport.lonLeft = centerLon - halfLon;
  viewport.lonRight = centerLon + halfLon;
  viewport.latTop = centerLat + halfLat;
  viewport.latBottom = centerLat - halfLat;
  return viewport;
}

MapZoomMode nextZoomMode(MapZoomMode mode) {
  switch (mode) {
    case MapZoomMode::Full:
      return MapZoomMode::Km50;
    case MapZoomMode::Km50:
      return MapZoomMode::Km25;
    case MapZoomMode::Km25:
      return MapZoomMode::Km10;
    case MapZoomMode::Km10:
    default:
      return MapZoomMode::Full;
  }
}

bool screenToGeo(const MapViewport& viewport, int x, int y, uint16_t width,
                 uint16_t height, float& lat, float& lon) {
  if (width < 2 || height < 2) return false;
  x = constrain(x, 0, static_cast<int>(width) - 1);
  y = constrain(y, 0, static_cast<int>(height) - 1);
  const float xFraction = static_cast<float>(x) / (width - 1);
  const float yFraction = static_cast<float>(y) / (height - 1);
  lon = viewport.lonLeft + xFraction * (viewport.lonRight - viewport.lonLeft);
  const float top = mercatorY(viewport.latTop);
  const float bottom = mercatorY(viewport.latBottom);
  lat = inverseMercatorY(top - yFraction * (top - bottom));
  return isfinite(lat) && isfinite(lon);
}

const char* zoomModeLabel(MapZoomMode mode) {
  switch (mode) {
    case MapZoomMode::Km50:
      return "50 km";
    case MapZoomMode::Km25:
      return "25 km";
    case MapZoomMode::Km10:
      return "10 km";
    case MapZoomMode::Full:
    default:
      return "cela CR";
  }
}

void drawBase(lv_obj_t* canvas, uint16_t* buffer, uint16_t width,
              uint16_t height, const MapViewport& viewport) {
  const uint16_t background = color(0x07131C);
  const uint16_t subtleBand = color(0x081721);
  const uint16_t grid = color(0x173142);

  for (uint16_t y = 0; y < height; ++y) {
    uint16_t* row = buffer + static_cast<size_t>(y) * width;
    for (uint16_t x = 0; x < width; ++x) row[x] = background;
    if ((y & 0x0F) == 0x0F) delay(1);
  }

  for (int y = 32; y < height; y += 64) {
    fillRect(buffer, width, height, 0, y, width, 32, subtleBand);
  }

  const float step = gridStepForMode(viewport.mode);
  const float firstLon = ceilf(viewport.lonLeft / step) * step;
  for (float lon = firstLon; lon <= viewport.lonRight + 0.0001f; lon += step) {
    const int x = mapX(lon, width, viewport);
    line(buffer, width, height, x, 0, x, height - 1, grid);
  }
  const float firstLat = ceilf(viewport.latBottom / step) * step;
  for (float lat = firstLat; lat <= viewport.latTop + 0.0001f; lat += step) {
    const int y = mapY(lat, height, viewport);
    line(buffer, width, height, 0, y, width - 1, y, grid);
  }

  (void)canvas;
}

void drawReference(lv_obj_t* canvas, uint16_t* buffer, uint16_t width,
                   uint16_t height, const MapViewport& viewport,
                   bool radarLayerEnabled, bool lightningLayerEnabled,
                   bool adsbLayerEnabled, bool lightningProximityAlert) {
  const uint16_t borderShadow = color(0x081018);
  const uint16_t border = color(0xDCEAF2);
  const uint16_t city = color(0xFFFFFF);
  constexpr size_t pointCount = sizeof(kCzechBorder) / sizeof(kCzechBorder[0]);

  for (size_t i = 1; i < pointCount; ++i) {
    const int x0 = mapX(kCzechBorder[i - 1].lon, width, viewport);
    const int y0 = mapY(kCzechBorder[i - 1].lat, height, viewport);
    const int x1 = mapX(kCzechBorder[i].lon, width, viewport);
    const int y1 = mapY(kCzechBorder[i].lat, height, viewport);
    line(buffer, width, height, x0 + 1, y0 + 1, x1 + 1, y1 + 1,
         borderShadow);
    line(buffer, width, height, x0, y0, x1, y1, border);
  }

  for (const City& item : kCities) {
    if (!geoVisible(item.lon, item.lat, viewport)) continue;
    const int x = mapX(item.lon, width, viewport);
    const int y = mapY(item.lat, height, viewport);
    circle(buffer, width, height, x, y, 2, city);
    if (x >= 0 && x < static_cast<int>(width) - 8 && y >= 7 &&
        y < static_cast<int>(height) - 12) {
      drawText(canvas, x + 4, y - 7, 90, item.name, 0xE7F0F5);
    }
  }

  if (lightningProximityAlert) {
    drawLightningProximityAlert(buffer, width, height, viewport);
  }
  drawStation(buffer, width, height, viewport);
  if (radarLayerEnabled) drawLegend(canvas, buffer, width, height);
  if (lightningLayerEnabled) {
    drawLightningTrailLegend(canvas, buffer, width, height);
  }
  drawZoomBadge(canvas, buffer, width, height, viewport);

  fillRect(buffer, width, height, 0, height - 22, width, 22, color(0x071018));
  char layerText[72] = "Vrstvy:";
  bool anyLayer = false;
  if (radarLayerEnabled) {
    strlcat(layerText, " RADAR", sizeof(layerText));
    anyLayer = true;
  }
  if (lightningLayerEnabled) {
    strlcat(layerText, anyLayer ? " + BLESKY" : " BLESKY", sizeof(layerText));
    anyLayer = true;
  }
  if (adsbLayerEnabled) {
    strlcat(layerText, anyLayer ? " + ADS-B" : " ADS-B", sizeof(layerText));
    anyLayer = true;
  }
  if (!anyLayer) strlcat(layerText, " vypnuty", sizeof(layerText));
  char footer[112];
  snprintf(footer, sizeof(footer), "%s | tap: 50 > 25 > 10 > CR", layerText);
  drawText(canvas, 10, height - 18, 340, footer, 0x91A9B7,
           &lv_font_montserrat_10);
  drawText(canvas, width - 150, height - 18, 140, "Stanice: Dolni Vlkys",
           0x91A9B7, &lv_font_montserrat_10, LV_TEXT_ALIGN_RIGHT);
}

void drawAircraft(lv_obj_t* canvas, uint16_t* buffer, uint16_t width,
                  uint16_t height, const AircraftSnapshot& snapshot,
                  const MapViewport& viewport,
                  const AircraftAlertConfig& alert) {
  if (!snapshot.valid) return;

  size_t labelsDrawn = 0;
  for (size_t i = 0; i < snapshot.count; ++i) {
    const Aircraft& aircraft = snapshot.items[i];
    if (!geoVisible(aircraft.lon, aircraft.lat, viewport)) continue;
    const int cx = mapX(aircraft.lon, width, viewport);
    const int cy = mapY(aircraft.lat, height, viewport);
    const float angle = aircraft.trackDeg * DEG_TO_RAD;
    const int8_t alertSlot = aircraftAlertMatchIndex(aircraft, alert);
    const bool highlighted = alertSlot >= 0;
    constexpr uint32_t kAlertColors[AIRCRAFT_ALERT_SLOT_COUNT] = {
        0xFF4FD8, 0x00E5FF, 0xFFD54F};
    const uint16_t c = highlighted
                           ? color(kAlertColors[static_cast<size_t>(alertSlot)])
                           : altitudeColor(aircraft.altitudeFt);
    const float symbolRadius = highlighted ? 13.0f : 9.0f;
    const float wingRadius = highlighted ? 10.0f : 7.0f;

    const int noseX = cx + lroundf(sinf(angle) * symbolRadius);
    const int noseY = cy - lroundf(cosf(angle) * symbolRadius);
    const int leftX = cx + lroundf(sinf(angle - 2.45f) * wingRadius);
    const int leftY = cy - lroundf(cosf(angle - 2.45f) * wingRadius);
    const int rightX = cx + lroundf(sinf(angle + 2.45f) * wingRadius);
    const int rightY = cy - lroundf(cosf(angle + 2.45f) * wingRadius);

    if (highlighted) {
      // Visual-only alert: a larger double ring and larger aircraft symbol.
      // Three saved targets use three ring colours. No sound or popup is used.
      const uint16_t outer = color(0xFFFFFF);
      circleOutline(buffer, width, height, cx, cy, 16, outer);
      circleOutline(buffer, width, height, cx, cy, 14, c);
      line(buffer, width, height, cx - 19, cy, cx - 15, cy, c);
      line(buffer, width, height, cx + 15, cy, cx + 19, cy, c);
      line(buffer, width, height, cx, cy - 19, cx, cy - 15, c);
      line(buffer, width, height, cx, cy + 15, cx, cy + 19, c);
    }

    line(buffer, width, height, noseX, noseY, leftX, leftY, c);
    line(buffer, width, height, leftX, leftY, rightX, rightY, c);
    line(buffer, width, height, rightX, rightY, noseX, noseY, c);
    line(buffer, width, height, cx, cy, noseX, noseY, c);
    circle(buffer, width, height, cx, cy, highlighted ? 2 : 1, c);

    if ((highlighted || labelsDrawn < 22) &&
        cx < static_cast<int>(width) - 12 && cy >= 8 &&
        cy < static_cast<int>(height) - 12) {
      char label[32];
      const char* id = aircraft.flight[0] ? aircraft.flight : aircraft.hex;
      if (aircraft.altitudeFt >= 0)
        snprintf(label, sizeof(label), "%s %.0fk", id,
                 aircraft.altitudeFt / 1000.0f);
      else
        snprintf(label, sizeof(label), "%s GND", id);
      drawText(canvas, cx + (highlighted ? 17 : 9), cy - 8, 92, label,
               0xFFFFFF, &lv_font_montserrat_10);
      if (!highlighted) ++labelsDrawn;
    }
  }
}

void drawRadarAge(lv_obj_t* canvas, uint16_t* buffer, uint16_t width,
                  uint16_t height, const char* frameName, uint8_t frameIndex,
                  uint8_t frameCount, uint16_t sourceWidth,
                  uint16_t sourceHeight) {
  if (!frameName || !frameName[0]) return;
  String name(frameName);
  String shown = "Radar --:-- UTC";
  const int marker = name.indexOf("z_max3d.");
  if (marker >= 0) {
    const int dateStart = marker + 8;
    if (name.length() >= dateStart + 13) {
      const String time = name.substring(dateStart + 9, dateStart + 13);
      shown = "Radar " + time.substring(0, 2) + ":" +
              time.substring(2, 4) + " UTC";
    }
  }
  char detail[96];
  snprintf(detail, sizeof(detail), "%s  %u/%u  %ux%u", shown.c_str(),
           static_cast<unsigned>(frameIndex + 1),
           static_cast<unsigned>(frameCount),
           static_cast<unsigned>(sourceWidth),
           static_cast<unsigned>(sourceHeight));
  fillRect(buffer, width, height, width - 238, 8, 230, 28,
           color(0x071018));
  drawText(canvas, width - 230, 14, 214, detail, 0xFFFFFF,
           &lv_font_montserrat_12, LV_TEXT_ALIGN_RIGHT);
}

void drawRadarMessage(lv_obj_t* canvas, uint16_t* buffer, uint16_t width,
                      uint16_t height, const char* message) {
  const int cardWidth = 350;
  const int cardHeight = 64;
  const int x = (width - cardWidth) / 2;
  const int y = (height - cardHeight) / 2;
  fillRect(buffer, width, height, x, y, cardWidth, cardHeight, color(0x101E29));
  fillRect(buffer, width, height, x, y, 5, cardHeight, color(0xFFB347));
  drawText(canvas, x + 18, y + 10, cardWidth - 30,
           "Radarova vrstva neni zobrazena", 0xFFFFFF,
           &lv_font_montserrat_14);
  drawText(canvas, x + 18, y + 34, cardWidth - 30,
           message && message[0] ? message
                                 : "Zkontrolujte sit a stisknete OBNOVIT",
           0xAFC2CE, &lv_font_montserrat_12);
}

}  // namespace MapRenderer
