#include "device/device_wifi.h"

#include <Arduino.h>
#include <WiFi.h>

namespace diarium {
namespace device {

bool DeviceWifi::connect(const WifiConfig& config, uint32_t timeout_ms) {
  if (!config.configured()) return false;

  WiFi.mode(WIFI_STA);
  // Nothing here logs the credentials. This is the line that would leak them.
  WiFi.begin(config.ssid.c_str(),
             config.password.empty() ? nullptr : config.password.c_str());

  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < timeout_ms) {
    delay(200);
  }
  return WiFi.status() == WL_CONNECTED;
}

void DeviceWifi::disconnect() {
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
}

bool DeviceWifi::connected() const { return WiFi.status() == WL_CONNECTED; }

}  // namespace device
}  // namespace diarium
