# NTP2

A modern, non-blocking NTP client library for Arduino and ESP32-based boards, with support for Kiss-o'-Death (KoD) reporting.

## 🔧 Features

- Fully non-blocking time updates
- Handles NTP KoD (Kiss‑o'-Death) codes
- Supports both domain and IP-based server configuration
- Configurable poll intervals, response timeouts, and retry delays
- Minimal resource usage
- I won't provoke an NTP server to test KoD reporting, but ChatGPT checked and confirmed that it will (should) work

## 📦 Installation

1. Download the [latest release](https://github.com/mitch@feig.com/NTP2).
2. Unzip into your Arduino `libraries/` folder.
3. Restart the Arduino IDE.
4. update() MUST be in loop().

Or install directly via **Arduino Library Manager** (when published).

## 🧪 Example

```cpp
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTP2.h>

WiFiUDP udp;
NTP2 ntp(udp);

void setup() {
  Serial.begin(115200);
  WiFi.begin("SSID", "password");
  while (WiFi.status() != WL_CONNECTED) delay(1000);

  ntp.begin();  // Uses default pool.ntp.org server
}

void loop() {
  NTPStatus status = ntp.update();

  if (status == NTP_CONNECTED) {
    Serial.println(ntp.epoch());
  }

  delay(1000);
}
```

Check the `examples/NTP2_Basic` sketch for full usage.

## 🔄 Status Codes

- `NTP_CONNECTED` – Valid response received
- `NTP_IDLE` – Waiting or processing
- `NTP_BAD_PACKET` – Invalid or no response
- `NTP_KOD_*` – Kiss-o'-Death response (rate-limiting, denial, etc.)

## ⚙️ Configuration Methods

```cpp
ntp.updateInterval(ms);      // Set poll interval
ntp.responseDelay(ms);       // Time to wait before reading response
ntp.retryDelay(ms);          // Retry delay after failure or KoD
ntp.pollInterval(ms);        // Alias for updateInterval
ntp.forceUpdate();           // Force an immediate query
```

## 🧑‍💻 Author

**Mitch Feig**  
📧 mitch@feig.com

## 🪪 License

MIT License. See `LICENSE` file for details.
