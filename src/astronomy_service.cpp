#include "astronomy_service.h"

#include <math.h>
#include <time.h>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;

enum class Body { Sun, Moon };

struct EquatorialPosition {
  double rightAscensionDeg = 0.0;
  double declinationDeg = 0.0;
  double eclipticLongitudeDeg = 0.0;
};

double normalize360(double value) {
  value = fmod(value, 360.0);
  if (value < 0.0) value += 360.0;
  return value;
}

double normalize180(double value) {
  value = normalize360(value);
  return value > 180.0 ? value - 360.0 : value;
}

double julianDay(time_t epoch) {
  return static_cast<double>(epoch) / 86400.0 + 2440587.5;
}

double daysSince2000Jan0(time_t epoch) {
  return julianDay(epoch) - 2451543.5;
}

EquatorialPosition sunPosition(time_t epoch) {
  const double d = daysSince2000Jan0(epoch);
  const double perihelionDeg = 282.9404 + 4.70935e-5 * d;
  const double eccentricity = 0.016709 - 1.151e-9 * d;
  const double meanAnomalyDeg = normalize360(356.0470 + 0.9856002585 * d);
  const double meanAnomalyRad = meanAnomalyDeg * kDegToRad;

  const double eccentricAnomalyDeg =
      meanAnomalyDeg + kRadToDeg * eccentricity * sin(meanAnomalyRad) *
                           (1.0 + eccentricity * cos(meanAnomalyRad));
  const double eccentricAnomalyRad = eccentricAnomalyDeg * kDegToRad;
  const double xv = cos(eccentricAnomalyRad) - eccentricity;
  const double yv = sqrt(1.0 - eccentricity * eccentricity) *
                    sin(eccentricAnomalyRad);
  const double trueAnomalyDeg = atan2(yv, xv) * kRadToDeg;
  const double distanceAu = sqrt(xv * xv + yv * yv);
  const double longitudeDeg = normalize360(trueAnomalyDeg + perihelionDeg);
  const double longitudeRad = longitudeDeg * kDegToRad;
  const double obliquityRad = (23.4393 - 3.563e-7 * d) * kDegToRad;

  const double x = distanceAu * cos(longitudeRad);
  const double y = distanceAu * sin(longitudeRad);
  const double xEq = x;
  const double yEq = y * cos(obliquityRad);
  const double zEq = y * sin(obliquityRad);

  EquatorialPosition result;
  result.rightAscensionDeg = normalize360(atan2(yEq, xEq) * kRadToDeg);
  result.declinationDeg =
      atan2(zEq, sqrt(xEq * xEq + yEq * yEq)) * kRadToDeg;
  result.eclipticLongitudeDeg = longitudeDeg;
  return result;
}

EquatorialPosition moonPosition(time_t epoch) {
  const double d = daysSince2000Jan0(epoch);
  const double nodeDeg = normalize360(125.1228 - 0.0529538083 * d);
  const double inclinationRad = 5.1454 * kDegToRad;
  const double periapsisDeg = normalize360(318.0634 + 0.1643573223 * d);
  const double semiMajorAxis = 60.2666;
  const double eccentricity = 0.054900;
  const double meanAnomalyDeg = normalize360(115.3654 + 13.0649929509 * d);
  const double meanAnomalyRad = meanAnomalyDeg * kDegToRad;

  double eccentricAnomalyDeg =
      meanAnomalyDeg + kRadToDeg * eccentricity * sin(meanAnomalyRad) *
                           (1.0 + eccentricity * cos(meanAnomalyRad));
  for (uint8_t i = 0; i < 3; ++i) {
    const double eRad = eccentricAnomalyDeg * kDegToRad;
    eccentricAnomalyDeg -=
        (eccentricAnomalyDeg - kRadToDeg * eccentricity * sin(eRad) -
         meanAnomalyDeg) /
        (1.0 - eccentricity * cos(eRad));
  }

  const double eccentricAnomalyRad = eccentricAnomalyDeg * kDegToRad;
  const double xv = semiMajorAxis * (cos(eccentricAnomalyRad) - eccentricity);
  const double yv = semiMajorAxis * sqrt(1.0 - eccentricity * eccentricity) *
                    sin(eccentricAnomalyRad);
  const double trueAnomalyRad = atan2(yv, xv);
  double distanceEarthRadii = sqrt(xv * xv + yv * yv);

  const double nodeRad = nodeDeg * kDegToRad;
  const double argumentRad = trueAnomalyRad + periapsisDeg * kDegToRad;
  const double xh =
      distanceEarthRadii *
      (cos(nodeRad) * cos(argumentRad) -
       sin(nodeRad) * sin(argumentRad) * cos(inclinationRad));
  const double yh =
      distanceEarthRadii *
      (sin(nodeRad) * cos(argumentRad) +
       cos(nodeRad) * sin(argumentRad) * cos(inclinationRad));
  const double zh = distanceEarthRadii * sin(argumentRad) * sin(inclinationRad);

  double longitudeDeg = normalize360(atan2(yh, xh) * kRadToDeg);
  double latitudeDeg =
      atan2(zh, sqrt(xh * xh + yh * yh)) * kRadToDeg;

  const double sunMeanAnomalyDeg = normalize360(356.0470 + 0.9856002585 * d);
  const double sunPerihelionDeg = 282.9404 + 4.70935e-5 * d;
  const double sunMeanLongitudeDeg =
      normalize360(sunMeanAnomalyDeg + sunPerihelionDeg);
  const double moonMeanLongitudeDeg =
      normalize360(nodeDeg + periapsisDeg + meanAnomalyDeg);
  const double elongationDeg =
      normalize360(moonMeanLongitudeDeg - sunMeanLongitudeDeg);
  const double argumentLatitudeDeg =
      normalize360(moonMeanLongitudeDeg - nodeDeg);

  auto sind = [](double degrees) { return sin(degrees * kDegToRad); };
  auto cosd = [](double degrees) { return cos(degrees * kDegToRad); };

  longitudeDeg +=
      -1.274 * sind(meanAnomalyDeg - 2.0 * elongationDeg) +
      0.658 * sind(2.0 * elongationDeg) -
      0.186 * sind(sunMeanAnomalyDeg) -
      0.059 * sind(2.0 * meanAnomalyDeg - 2.0 * elongationDeg) -
      0.057 * sind(meanAnomalyDeg - 2.0 * elongationDeg +
                   sunMeanAnomalyDeg) +
      0.053 * sind(meanAnomalyDeg + 2.0 * elongationDeg) +
      0.046 * sind(2.0 * elongationDeg - sunMeanAnomalyDeg) +
      0.041 * sind(meanAnomalyDeg - sunMeanAnomalyDeg) -
      0.035 * sind(elongationDeg) -
      0.031 * sind(meanAnomalyDeg + sunMeanAnomalyDeg) -
      0.015 * sind(2.0 * argumentLatitudeDeg - 2.0 * elongationDeg) +
      0.011 * sind(meanAnomalyDeg - 4.0 * elongationDeg);

  latitudeDeg +=
      -0.173 * sind(argumentLatitudeDeg - 2.0 * elongationDeg) -
      0.055 * sind(meanAnomalyDeg - argumentLatitudeDeg -
                   2.0 * elongationDeg) -
      0.046 * sind(meanAnomalyDeg + argumentLatitudeDeg -
                   2.0 * elongationDeg) +
      0.033 * sind(argumentLatitudeDeg + 2.0 * elongationDeg) +
      0.017 * sind(2.0 * meanAnomalyDeg + argumentLatitudeDeg);

  distanceEarthRadii +=
      -0.58 * cosd(meanAnomalyDeg - 2.0 * elongationDeg) -
      0.46 * cosd(2.0 * elongationDeg);

  const double longitudeRad = longitudeDeg * kDegToRad;
  const double latitudeRad = latitudeDeg * kDegToRad;
  const double obliquityRad = (23.4393 - 3.563e-7 * d) * kDegToRad;
  const double x = distanceEarthRadii * cos(longitudeRad) * cos(latitudeRad);
  const double y = distanceEarthRadii * sin(longitudeRad) * cos(latitudeRad);
  const double z = distanceEarthRadii * sin(latitudeRad);
  const double xEq = x;
  const double yEq = y * cos(obliquityRad) - z * sin(obliquityRad);
  const double zEq = y * sin(obliquityRad) + z * cos(obliquityRad);

  EquatorialPosition result;
  result.rightAscensionDeg = normalize360(atan2(yEq, xEq) * kRadToDeg);
  result.declinationDeg =
      atan2(zEq, sqrt(xEq * xEq + yEq * yEq)) * kRadToDeg;
  result.eclipticLongitudeDeg = normalize360(longitudeDeg);
  return result;
}

double altitudeDeg(Body body, time_t epoch, double latitudeDeg,
                   double longitudeDeg) {
  const EquatorialPosition position =
      body == Body::Sun ? sunPosition(epoch) : moonPosition(epoch);
  const double jd = julianDay(epoch);
  const double t = (jd - 2451545.0) / 36525.0;
  const double gmstDeg =
      normalize360(280.46061837 + 360.98564736629 * (jd - 2451545.0) +
                   0.000387933 * t * t - t * t * t / 38710000.0);
  const double hourAngleRad =
      normalize180(gmstDeg + longitudeDeg - position.rightAscensionDeg) *
      kDegToRad;
  const double latitudeRad = latitudeDeg * kDegToRad;
  const double declinationRad = position.declinationDeg * kDegToRad;
  const double altitude =
      asin(sin(latitudeRad) * sin(declinationRad) +
           cos(latitudeRad) * cos(declinationRad) * cos(hourAngleRad));
  return altitude * kRadToDeg;
}

void formatLocalTime(time_t epoch, char output[6]) {
  struct tm local{};
  localtime_r(&epoch, &local);
  snprintf(output, 6, "%02d:%02d", local.tm_hour, local.tm_min);
}

void findRiseSet(Body body, time_t dayStart, time_t dayEnd,
                 double latitudeDeg, double longitudeDeg, char rise[6],
                 char set[6]) {
  strlcpy(rise, "--:--", 6);
  strlcpy(set, "--:--", 6);

  const double thresholdDeg = body == Body::Sun ? -0.833 : 0.125;
  constexpr time_t stepSeconds = 5 * 60;
  time_t previousTime = dayStart;
  double previous = altitudeDeg(body, previousTime, latitudeDeg, longitudeDeg) -
                    thresholdDeg;

  for (time_t currentTime = dayStart + stepSeconds; currentTime <= dayEnd;
       currentTime += stepSeconds) {
    const double current =
        altitudeDeg(body, currentTime, latitudeDeg, longitudeDeg) - thresholdDeg;
    const bool rising = previous <= 0.0 && current > 0.0;
    const bool setting = previous >= 0.0 && current < 0.0;

    if (rising || setting) {
      const double denominator = previous - current;
      const double fraction =
          fabs(denominator) > 1e-9 ? previous / denominator : 0.5;
      const time_t eventTime = previousTime +
                               static_cast<time_t>(fraction * stepSeconds);
      if (rising && strcmp(rise, "--:--") == 0) formatLocalTime(eventTime, rise);
      if (setting && strcmp(set, "--:--") == 0) formatLocalTime(eventTime, set);
    }

    previous = current;
    previousTime = currentTime;
  }
}

const char* phaseName(double phase) {
  if (phase < 0.03 || phase >= 0.97) return "Nov";
  if (phase < 0.22) return "Dorusta";
  if (phase < 0.28) return "1. ctvrt";
  if (phase < 0.47) return "Dorusta";
  if (phase < 0.53) return "Uplnek";
  if (phase < 0.72) return "Ubyva";
  if (phase < 0.78) return "3. ctvrt";
  return "Ubyva";
}
}  // namespace

bool AstronomyService::update(float latitudeDeg, float longitudeDeg) {
  const time_t now = time(nullptr);
  if (now < 1577836800) {
    snapshot_.valid = false;
    snprintf(snapshot_.status, sizeof(snapshot_.status), "Cekam na NTP cas");
    return false;
  }

  struct tm localNow{};
  localtime_r(&now, &localNow);
  const int localDateKey = (localNow.tm_year + 1900) * 1000 + localNow.tm_yday;
  const bool coordinatesChanged =
      !isfinite(lastLatitude_) || !isfinite(lastLongitude_) ||
      fabsf(latitudeDeg - lastLatitude_) > 0.0001f ||
      fabsf(longitudeDeg - lastLongitude_) > 0.0001f;

  if (localDateKey != lastLocalDateKey_ || coordinatesChanged ||
      !snapshot_.valid) {
    struct tm localStart = localNow;
    localStart.tm_hour = 0;
    localStart.tm_min = 0;
    localStart.tm_sec = 0;
    localStart.tm_isdst = -1;
    const time_t dayStart = mktime(&localStart);

    struct tm localEnd = localStart;
    localEnd.tm_mday += 1;
    localEnd.tm_isdst = -1;
    const time_t dayEnd = mktime(&localEnd);

    findRiseSet(Body::Sun, dayStart, dayEnd, latitudeDeg, longitudeDeg,
                snapshot_.sunrise, snapshot_.sunset);
    findRiseSet(Body::Moon, dayStart, dayEnd, latitudeDeg, longitudeDeg,
                snapshot_.moonrise, snapshot_.moonset);
    lastLocalDateKey_ = localDateKey;
    lastLatitude_ = latitudeDeg;
    lastLongitude_ = longitudeDeg;
  }

  snapshot_.sunAltitudeDeg =
      static_cast<float>(altitudeDeg(Body::Sun, now, latitudeDeg, longitudeDeg));
  snapshot_.moonAltitudeDeg = static_cast<float>(
      altitudeDeg(Body::Moon, now, latitudeDeg, longitudeDeg));

  const EquatorialPosition sun = sunPosition(now);
  const EquatorialPosition moon = moonPosition(now);
  const double phase =
      normalize360(moon.eclipticLongitudeDeg - sun.eclipticLongitudeDeg) /
      360.0;
  snapshot_.moonPhase = static_cast<float>(phase);
  snapshot_.moonIlluminationPct =
      static_cast<float>(50.0 * (1.0 - cos(phase * 2.0 * kPi)));
  strlcpy(snapshot_.moonPhaseName, phaseName(phase),
          sizeof(snapshot_.moonPhaseName));
  snapshot_.latitude = latitudeDeg;
  snapshot_.longitude = longitudeDeg;
  snapshot_.epoch = static_cast<uint32_t>(now);
  snapshot_.valid = true;
  snprintf(snapshot_.status, sizeof(snapshot_.status), "Astronomie OK");
  return true;
}
