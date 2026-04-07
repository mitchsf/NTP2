# NTP2 - Non-Blocking NTP Client for Arduino

A lightweight, non-blocking NTP (Network Time Protocol) client library for Arduino with full RFC 5905 Kiss-o'-Death (KoD) support.

## Features

- **Non-blocking operation** — doesn't freeze your sketch while waiting for responses
- **RFC 5905 compliant** — handles all standard Kiss-o'-Death codes (untested)
- **Request/response correlation** — stamps each request with a token in the Transmit Timestamp field; rejects responses whose Originate Timestamp doesn't match, preventing acceptance of stale or unrelated packets
- **Stale packet flushing** — reads all queued UDP packets and keeps only the last valid one, handling servers that send extension fields or duplicate responses
- **Configurable intervals** — set custom poll, retry, and response timeouts
- **Millisecond precision** — stores fractional seconds from the NTP Transmit Timestamp for sub-second accuracy (untested)
- **Automatic error recovery** — backs off to retry interval on errors; restores default interval on next success
- **Strict time validity** — `badRead()` invalidates cached time so `epoch()` returns 0 until the next successful sync; retries automatically at the retry interval
- **Plausibility guard** — `epoch()` rejects obviously wrong timestamps outside the 2000–2100 range
- **Stratum validation** — verifies server quality and synchronization status
- **Overflow-safe** — handles `millis()` wraparound and NTP timestamp calculations correctly through 2106

## Installation

Copy `NTP2.h` and `NTP2.cpp` to your Arduino libraries folder or include them directly in your sketch directory.

## Basic Usage

```cpp
#include <WiFiUdp.h>
#include "NTP2.h"

WiFiUdp udp;
NTP2 ntp(udp);

void setup() {
  Serial.begin(115200);
  WiFi.begin("ssid", "password");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  
  ntp.begin();  // Uses pool.ntp.org by default
}

void loop() {
  NTPStatus status = ntp.update();
  
  if (status == NTP_CONNECTED) {
    Serial.print("Unix time: ");
    Serial.println(ntp.epoch());
  }
  
  // Your other code here - update() is non-blocking
}
```

## Advanced Configuration

```cpp
// Use specific NTP server
ntp.begin("time.nist.gov");

// Or use IP address
ntp.begin(IPAddress(132, 163, 96, 1));

// Configure polling interval (default: 1 hour)
ntp.updateInterval(3600000);  // milliseconds

// Set response timeout (default: 250ms)
ntp.responseDelay(500);

// Set retry delay for errors/KoD (default: 30 seconds)
ntp.retryDelay(60000);

// Force immediate update
ntp.forceUpdate();
```

## Return Status Codes

The `update()` method returns one of these status codes:

| Status | Description |
|--------|-------------|
| `NTP_IDLE` | Waiting for next poll or response |
| `NTP_CONNECTED` | Successfully synchronized |
| `NTP_BAD_PACKET` | Invalid or missing response |
| `NTP_KOD_RATE` | Rate limiting (too many requests) |
| `NTP_KOD_DENY` | Access denied |
| `NTP_KOD_ACST` | Association belongs to anycast server |
| `NTP_KOD_AUTH` | Authentication failed |
| `NTP_KOD_AUTO` | Autokey sequence failed |
| `NTP_KOD_BCST` | Broadcast mode |
| `NTP_KOD_CRYP` | Cryptographic authentication failed |
| `NTP_KOD_DROP` | Server dropping packets |
| `NTP_KOD_RSTR` | Access denied (rate restriction) |
| `NTP_KOD_INIT` | Association not yet synchronized |
| `NTP_KOD_MCST` | Multicast mode |
| `NTP_KOD_NKEY` | No key found |
| `NTP_KOD_NTSN` | Network Time Security negative-acknowledgment |
| `NTP_KOD_RMOT` | Somebody else's server |
| `NTP_KOD_STEP` | Step change in system time |
| `NTP_UNKNOWN_KOD` | Unrecognized Kiss-o'-Death code |

## API Reference

### Methods

- `void begin()` — Initialize with default server (pool.ntp.org)
- `void begin(const char* server)` — Initialize with hostname
- `void begin(IPAddress serverIP)` — Initialize with IP address
- `void stop()` — Stop NTP client and release UDP port
- `NTPStatus update()` — Non-blocking update, call frequently in loop()
- `NTPStatus forceUpdate()` — Force immediate sync request
- `time_t epoch()` — Get current Unix timestamp (returns 0 if no valid sync)
- `uint32_t timestamp()` — Get millis() value at last successful sync
- `bool ntpStat()` — Returns true if last sync succeeded
- `void updateInterval(unsigned long ms)` — Set polling interval
- `void responseDelay(uint32_t ms)` — Set response timeout
- `void retryDelay(uint32_t ms)` — Set error retry delay

### Defaults

| Parameter | Default | Define |
|-----------|---------|--------|
| Server | `pool.ntp.org` | `NTP_SERVER` |
| Poll interval | 3,600,000 ms (1 hr) | `NTP_POLL_INTERVAL` |
| Response timeout | 250 ms | `NTP_RESPONSE_DELAY` |
| Retry delay | 30,000 ms (30 s) | `NTP_RETRY_DELAY` |

## How It Works

NTP2 uses a non-blocking state machine:

1. When the poll interval expires (or `forceUpdate()` is called), sends an NTP request with a correlation token in the Transmit Timestamp field.
2. Returns `NTP_IDLE` while waiting for the response timeout to elapse.
3. After the timeout, reads all queued UDP packets, keeping the last one of valid size (≥ 48 bytes). This flushes stale packets and discards extension fields.
4. Validates the response: checks that the Originate Timestamp matches the request token, then verifies protocol fields and extracts the Transmit Timestamp.
5. On success, stores the NTP time with fractional-second precision, resets the poll interval, and returns `NTP_CONNECTED`.
6. On failure, invalidates cached time so `epoch()` returns 0, switches to the retry interval, and keeps retrying until a successful sync.
7. Handles Kiss-o'-Death packets per RFC 5905, mapping all 15 standard KoD codes to distinct status values.

### Validation checks

- Originate Timestamp matches request token (correlation)
- NTP version 3 or 4
- Mode 4 (server) or 5 (broadcast)
- Stratum 1–15 (rejects 0/KoD and 16/unsynchronized)
- Leap Indicator ≠ 3 (alarm/unsynchronized)
- Transmit Timestamp non-zero
- Unix epoch within 2000–2100 plausibility window

## Technical Details

- **Protocol**: NTP v3/v4 (RFC 5905)
- **UDP Port**: 123
- **Packet Size**: 48 bytes
- **Precision**: Millisecond (stores fractional seconds — untested)
- **Time Base**: Unix epoch (January 1, 1970)
- **Overflow Safe**: Until February 2106

## License

Copyright (c) 2026 Mitch Feig (mitch@feig.com)

## Credits

