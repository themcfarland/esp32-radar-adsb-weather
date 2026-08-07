#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "zambretti_forecaster.h"

static void expectCode(float pressure, float trend, uint8_t month,
                       bool monthValid, float wind, bool windValid,
                       char expected) {
  const Zambretti::Result result = Zambretti::forecast(
      pressure, trend, month, monthValid, wind, windValid, true);
  if (!result.valid || result.code != expected) {
    fprintf(stderr,
            "pressure=%.1f trend=%.2f month=%u wind=%.1f -> %c, expected %c\n",
            pressure, trend, month, wind, result.code, expected);
    assert(false);
  }
}

int main() {
  expectCode(1030.0f, 0.0f, 7, true, NAN, false, 'A');
  expectCode(1000.0f, 0.0f, 7, true, NAN, false, 'N');
  expectCode(980.0f, 0.0f, 7, true, NAN, false, 'W');
  expectCode(1020.0f, 0.2f, 7, true, NAN, false, 'B');
  expectCode(990.0f, -0.2f, 1, true, NAN, false, 'V');

  const Zambretti::Result wind = Zambretti::forecast(
      1005.0f, 0.2f, 1, true, 0.0f, true, true);
  assert(wind.valid && wind.windUsed);
  assert(wind.adjustedPressureHpa > 1009.0f);

  const Zambretti::Result season = Zambretti::forecast(
      1005.0f, -0.2f, 7, true, NAN, false, true);
  assert(season.valid && season.seasonalCorrectionApplied);
  assert(season.adjustedPressureHpa < 1005.0f);

  puts("ZAMBRETTI TEST OK");
  return 0;
}
