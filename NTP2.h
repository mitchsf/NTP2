/* NTP2.h
   NTP2 library for Arduino - non-blocking, Kiss-o'-Death codes
   The MIT License (MIT)
   (c) 2025 Mitch Feig (mitch@feig.com)

   update() return codes:
     NTP_BAD_PACKET (0x00)    - Failure or no valid response.
     NTP_IDLE       (0x01)    - Idle (processing or waiting).
     NTP_CONNECTED  (0x02)    - Valid NTP response (delivered only once).
     KoD codes: 0x10–0x1E     - Specific Kiss-o'-Death responses.
     NTP_UNKNOWN_KOD (0x20)   - Unknown KoD code.
*/

#ifndef NTP2_H
#define NTP2_H

#include "Arduino.h"
#include <Udp.h>

#if !defined(__time_t_defined)
typedef unsigned long time_t;
#endif

// Default settings.
#define SEVENTYYEARS       2208988800UL
#define NTP_SERVER         "us.pool.ntp.org"
#define NTP_PACKET_SIZE    48
#define NTP_PORT           123
#define NTP_RESPONSE_DELAY 250          // in ms
#define NTP_RETRY_DELAY    30000        // in ms
#define NTP_POLL_INTERVAL  1800000      // in ms

// Simplified enumeration for status codes.
enum NTPStatus : uint8_t {
  NTP_BAD_PACKET   = 0x00, // Error or invalid response.
  NTP_IDLE         = 0x01, // Waiting or processing.
  NTP_CONNECTED    = 0x02, // Valid response.
  // KoD codes:
  NTP_KOD_RATE     = 0x10,
  NTP_KOD_DENY     = 0x11,
  NTP_KOD_ACST     = 0x12,
  NTP_KOD_AUTH     = 0x13,
  NTP_KOD_AUTO     = 0x14,
  NTP_KOD_BCST     = 0x15,
  NTP_KOD_CRYP     = 0x16,
  NTP_KOD_DROP     = 0x17,
  NTP_KOD_RSTR     = 0x18,
  NTP_KOD_INIT     = 0x19,
  NTP_KOD_MCST     = 0x1A,
  NTP_KOD_NKEY     = 0x1B,
  NTP_KOD_NTSN     = 0x1C,
  NTP_KOD_RMOT     = 0x1D,
  NTP_KOD_STEP     = 0x1E,
  NTP_UNKNOWN_KOD  = 0x20
};

class NTP2 {
  public:
    NTP2(UDP& udp);
    ~NTP2();

    // Initialization: 
    // The no-argument begin() uses NTP_SERVER.
    // Alternatively, provide a server name or IP.
    void begin();
    void begin(const char* server);
    void begin(IPAddress serverIP);
    void stop();

    // Timing configuration.
    void updateInterval(unsigned long int uInterval);
    void responseDelay(uint32_t newDelay);
    void retryDelay(uint32_t newDelay);
    void pollInterval(uint32_t newInterval);

    // update() sends a request if needed, processes a response if available,
    // and returns:
    //   - NTP_CONNECTED (0x02) exactly once when a valid update has been processed,
    //   - Then NTP_IDLE (0x01) until a new request is sent,
    //   - Or NTP_BAD_PACKET (0x00) for a failure.
    // KoD responses are passed through unchanged.
    NTPStatus update();

    // forceUpdate() forces an immediate update.
    NTPStatus forceUpdate();

    // Returns the last valid Unix epoch time.
    time_t epoch();

    // Returns the millis() timestamp when the valid response was received.
    uint32_t timestamp();

    // Returns true if the last update resulted in GOOD_TIME.
    bool ntpStat();

  private:
    // Prepare the NTP request packet.
    void init();

    // Validate the received NTP packet.
    bool checkValid();

    // Return error status.
    NTPStatus badRead();

    // Low-level functions.
    NTPStatus sendNTPRequest();
    NTPStatus processNTPResponse();

    UDP *udp;
    const char* server = nullptr;
    IPAddress serverIP;

    // Buffers for packets.
    uint8_t ntpRequest[NTP_PACKET_SIZE] = { 0xE3, 0x00, 0x06, 0xEC };
    uint8_t ntpQuery[NTP_PACKET_SIZE];

    // Timing variables.
    uint32_t defaultInterval = NTP_POLL_INTERVAL;
    uint32_t activeInterval = defaultInterval;
    uint32_t responseDelayValue = NTP_RESPONSE_DELAY;
    uint32_t retryDelayValue = NTP_RETRY_DELAY;
    uint32_t utcTime = 0;
    uint32_t lastUpdate = 0;
    uint32_t ntpTime = 0;
    uint32_t requestTimestamp = 0;  // When the request was sent.
    uint32_t lastResponseMillis = 0; // When a valid response was received.

    bool force = false;  // Flag for forcing an update.
    NTPStatus ntpSt = NTP_BAD_PACKET; // Last status code.

    // Latch flag: set to true when a valid response is processed but not yet delivered.
    bool pendingGood = false;

    // KoD lookup table (moved inline here).
    struct KodEntry {
      const char *code;
      uint8_t ret;
    };
    inline static const KodEntry kodLookup[16] = {
      {"RATE", 0x10},
      {"DENY", 0x11},
      {"ACST", 0x12},
      {"AUTH", 0x13},
      {"AUTO", 0x14},
      {"BCST", 0x15},
      {"CRYP", 0x16},
      {"DROP", 0x17},
      {"RSTR", 0x18},
      {"INIT", 0x19},
      {"MCST", 0x1A},
      {"NKEY", 0x1B},
      {"NTSN", 0x1C},
      {"RMOT", 0x1D},
      {"STEP", 0x1E},
      {"UNKN", 0x20}
    };
};

#endif
