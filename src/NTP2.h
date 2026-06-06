/* NTP2.h
   NTP2 library for Arduino - non-blocking, KoD-aware
   (c) 2025 Mitch Feig (mitch@feig.com)
*/

#ifndef NTP2_H
#define NTP2_H

#include "Arduino.h"
#include <Udp.h>

#if !defined(__time_t_defined)
typedef unsigned long time_t;
#endif

#define SEVENTYYEARS       2208988800UL
#define NTP_SERVER         "time.google.com"
#define NTP_PACKET_SIZE    48
#define NTP_PORT           123
#define NTP_RESPONSE_DELAY 250
#define NTP_RETRY_DELAY    30000
#define NTP_POLL_INTERVAL  3600000

enum NTPStatus : uint8_t {
  NTP_BAD_PACKET   = 0x00,
  NTP_IDLE         = 0x01,
  NTP_CONNECTED    = 0x02,
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

    void begin();
    void begin(const char* server);
    void begin(IPAddress serverIP);
    void stop();

    void updateInterval(unsigned long uInterval);
    void responseDelay(uint32_t newDelay);
    void retryDelay(uint32_t newDelay);

    // Set the local UDP port used for the client socket. Call before begin()
    // (or call stop() then begin() again to re-bind). Default is 123 — keeps
    // existing single-component callers working byte-for-byte. Pass 0 to let
    // the OS pick an ephemeral port; use this when another component on the
    // same device already binds 123 (e.g. an NTP server class on the same
    // ESP32). Side note: NTP doesn't require the client to send from 123 —
    // the standard says ephemeral source is fine for unicast clients.
    void setLocalPort(uint16_t port);

    NTPStatus update();
    NTPStatus forceUpdate();

    time_t epoch();
    uint32_t timestamp();
    bool ntpStat();

    // Round-trip time (ms) of the last successful sync — diagnostics; half of
    // this is added to the synced time as the one-way delay correction.
    uint16_t lastRttMs = 0;

  private:
    void init();
    bool checkValid(uint32_t tempTimeSeconds);
    NTPStatus badRead();
    NTPStatus sendNTPRequest();
    NTPStatus processNTPResponse();

    UDP *udp;
    const char* server = nullptr;
    IPAddress serverIP;

    uint8_t ntpRequest[NTP_PACKET_SIZE];
    uint8_t ntpQuery[NTP_PACKET_SIZE];

    uint16_t localPort = NTP_PORT;       // overridable via setLocalPort(); 0 = ephemeral
    uint32_t defaultInterval = NTP_POLL_INTERVAL;
    uint32_t activeInterval = defaultInterval;
    uint32_t responseDelayValue = NTP_RESPONSE_DELAY;
    uint32_t retryDelayValue = NTP_RETRY_DELAY;
    uint32_t lastUpdate = 0;
    uint32_t requestTimestamp = 0;
    // Request token (copied back by server into Originate Timestamp)
    uint32_t reqTxSec = 0;
    uint32_t reqTxFrac = 0;
    uint32_t lastResponseMillis = 0;
    uint32_t lastSyncMillis = 0;
    uint32_t ntpTimeSeconds = 0;
    uint64_t ntpMillisAtSync = 0;

    bool force = false;
    NTPStatus ntpSt = NTP_BAD_PACKET;

    struct KodEntry {
      const char *code;
      uint8_t ret;
    };

    inline static const KodEntry kodLookup[15] = {
      {"RATE", 0x10}, {"DENY", 0x11}, {"ACST", 0x12}, {"AUTH", 0x13},
      {"AUTO", 0x14}, {"BCST", 0x15}, {"CRYP", 0x16}, {"DROP", 0x17},
      {"RSTR", 0x18}, {"INIT", 0x19}, {"MCST", 0x1A}, {"NKEY", 0x1B},
      {"NTSN", 0x1C}, {"RMOT", 0x1D}, {"STEP", 0x1E}
    };
};

#endif
