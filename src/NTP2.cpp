/* NTP2.cpp
   NTP2 library for Arduino - non-blocking, KoD-aware
   Updated to fix timing, race, and KoD handling issues
   Fixed epoch() calculation for accurate timekeeping
   (c) 2025 Mitch Feig (mitch@feig.com)
*/

#include "NTP2.h"

NTP2::NTP2(UDP& udp) {
  this->udp = &udp;
}

NTP2::~NTP2() {
  stop();
}

void NTP2::begin() {
  begin(NTP_SERVER);
  lastUpdate = millis() - activeInterval;
}

void NTP2::begin(const char* server) {
  this->server = server ? server : NTP_SERVER;
  udp->begin(localPort);
  force = true;
  lastUpdate = millis() - activeInterval;
}

void NTP2::begin(IPAddress serverIP) {
  this->serverIP = serverIP;
  this->server = nullptr;
  udp->begin(localPort);
  force = true;
  lastUpdate = millis() - activeInterval;
}

void NTP2::setLocalPort(uint16_t port) {
  localPort = port;
}

void NTP2::stop() {
  udp->stop();
}

void NTP2::updateInterval(unsigned long uInterval) {
  activeInterval = defaultInterval = uInterval;
}

void NTP2::responseDelay(uint32_t newDelay) {
  responseDelayValue = newDelay;
}

void NTP2::retryDelay(uint32_t newDelay) {
  retryDelayValue = newDelay;
}

NTPStatus NTP2::forceUpdate() {
  if (requestTimestamp != 0) return NTP_BAD_PACKET;
  force = true;
  return update();
}

NTPStatus NTP2::update() {
  if (requestTimestamp != 0) {
    // Poll for the reply on every call and read it the instant it arrives, so
    // the sync is timestamped at true arrival instead of requestTime+responseDelay.
    // The old code waited the full responseDelay window THEN read the buffered
    // packet, stamping lastSyncMillis ~responseDelay late while using the server's
    // earlier transmit timestamp — which left every synced clock ~responseDelay slow.
    // responseDelay is now a TIMEOUT: give up only if nothing valid arrives in time.
    NTPStatus result = processNTPResponse();   // returns NTP_IDLE until our reply lands
    if (result != NTP_IDLE) {                  // resolved: good packet, KoD, or bad
      requestTimestamp = 0;
      return result;
    }
    if ((int32_t)(millis() - requestTimestamp) >= (int32_t)responseDelayValue) {
      requestTimestamp = 0;                    // timed out waiting for the reply
      return badRead();
    }
    return NTP_IDLE;                           // still waiting — poll again next call
  }

  if (force || (int32_t)(millis() - lastUpdate) >= (int32_t)activeInterval) {
    return sendNTPRequest();
  }

  return NTP_IDLE;
}

NTPStatus NTP2::sendNTPRequest() {
  lastUpdate = millis();
  init();

  bool success = server ? udp->beginPacket(server, NTP_PORT)
                        : udp->beginPacket(serverIP, NTP_PORT);

  if (!success || udp->write(ntpRequest, NTP_PACKET_SIZE) != NTP_PACKET_SIZE || !udp->endPacket()) {
    ntpSt = NTP_BAD_PACKET;
    return ntpSt;
  }

  requestTimestamp = millis();
  force = false;
  ntpSt = NTP_IDLE;
  return ntpSt;
}

NTPStatus NTP2::processNTPResponse() {
  // requestTimestamp is owned by update() now: this function may be called
  // repeatedly (polling) and returns NTP_IDLE until our reply actually arrives.
  memset(ntpQuery, 0, NTP_PACKET_SIZE);
  
  // Read all available packets, keeping the last complete one
  // This flushes stale packets and handles servers sending extensions
  bool gotPacket = false;
  int packetSize;
  while ((packetSize = udp->parsePacket()) > 0) {
    if (packetSize >= NTP_PACKET_SIZE) {
      udp->read(ntpQuery, NTP_PACKET_SIZE);
      // Discard any trailing bytes (extension fields, padding)
      while (udp->available()) udp->read();
      gotPacket = true;
    } else {
      // Undersized packet — discard entirely
      while (udp->available()) udp->read();
    }
  }
  
  if (!gotPacket) {
    return NTP_IDLE;   // nothing yet — update() keeps polling until the timeout
  }

  uint8_t mode = ntpQuery[0] & 0x07;
  uint8_t stratum = ntpQuery[1];

  // Check for Kiss-o'-Death
  if (stratum == 0 && (mode == 4 || mode == 5)) {
    char kod[5] = {ntpQuery[12], ntpQuery[13], ntpQuery[14], ntpQuery[15], '\0'};
    for (auto& k : kodLookup) {
      if (strcmp(kod, k.code) == 0) {
        activeInterval = retryDelayValue;
        ntpSt = static_cast<NTPStatus>(k.ret);
        return ntpSt;
      }
    }
    activeInterval = retryDelayValue;
    ntpSt = NTP_UNKNOWN_KOD;
    return ntpSt;
  }

  // Correlate response to our most recent request by checking Originate Timestamp.
  // This prevents accepting stale/unrelated packets.
  uint32_t orgSec  = ((uint32_t)ntpQuery[24] << 24) |
                     ((uint32_t)ntpQuery[25] << 16) |
                     ((uint32_t)ntpQuery[26] << 8)  |
                     (uint32_t)ntpQuery[27];
  uint32_t orgFrac = ((uint32_t)ntpQuery[28] << 24) |
                     ((uint32_t)ntpQuery[29] << 16) |
                     ((uint32_t)ntpQuery[30] << 8)  |
                     (uint32_t)ntpQuery[31];

  if (orgSec != reqTxSec || orgFrac != reqTxFrac) {
    return NTP_IDLE;   // stray/stale packet, not our reply — keep polling
  }

  // Extract transmit timestamp
  uint32_t txSec = ((uint32_t)ntpQuery[40] << 24) |
                   ((uint32_t)ntpQuery[41] << 16) |
                   ((uint32_t)ntpQuery[42] << 8)  |
                   (uint32_t)ntpQuery[43];
  uint32_t txFrac = ((uint32_t)ntpQuery[44] << 24) |
                    ((uint32_t)ntpQuery[45] << 16) |
                    ((uint32_t)ntpQuery[46] << 8)  |
                    (uint32_t)ntpQuery[47];
  
  // Validate transmit timestamp is non-zero
  if (txSec == 0) return badRead();
  
  uint32_t fracMillis = (uint32_t)((uint64_t)txFrac * 1000 / 0x100000000ULL);

  if (!checkValid(txSec)) return badRead();

  // Round-trip delay compensation: the server's transmit timestamp is its time
  // when it SENT the reply; ~RTT/2 elapses before the reply reaches us. Add half
  // the measured round trip (requestTimestamp = send time, still set here — update()
  // clears it after we return) so the synced clock reflects true time at RECEPTION
  // rather than at the server's send instant. This removes the residual ~one-way
  // delay that remained after the poll-on-arrival fix.
  uint32_t readMs = millis();
  uint32_t rtt    = (uint32_t)(int32_t)(readMs - requestTimestamp);
  if (rtt > 60000UL) rtt = 0;          // ignore implausible round trips (wrap/garbage)
  lastRttMs = (uint16_t)rtt;           // exposed for diagnostics

  ntpTimeSeconds = txSec;
  lastSyncMillis = readMs;
  // Fix overflow: cast txSec to uint64_t before multiplication
  ntpMillisAtSync = ((uint64_t)txSec * 1000ULL) + fracMillis + (rtt / 2);
  if (activeInterval != defaultInterval) activeInterval = defaultInterval;

  lastResponseMillis = readMs;
  ntpSt = NTP_CONNECTED;
  return NTP_CONNECTED;
}

NTPStatus NTP2::badRead() {
  // A transient bad response must not destroy a previously-good sync. Clocks
  // built on this library extrapolate the current time from ntpTimeSeconds /
  // lastSyncMillis via epoch(); wiping those on a single bad packet causes
  // the clock to freeze until the next successful sync (which can take a
  // long time if DNS or the UDP path is temporarily wedged).
  //
  // Pace the next retry and record the failure status, but keep the prior
  // sync's data intact. ntpStat() now reflects "is there a usable sync"
  // (ntpTimeSeconds > 0), so callers get true across brief outages.
  //
  // Reset lastUpdate to "now" so the retryDelay gate runs from this moment
  // forward — without this, lastUpdate stayed pinned at the previous send
  // time and the next send fired as soon as (responseDelay) elapsed,
  // making retryDelay dead code whenever retryDelay < responseDelay.
  lastUpdate = millis();
  activeInterval = retryDelayValue;
  ntpSt = NTP_BAD_PACKET;
  return ntpSt;
}

void NTP2::init() {
  memset(ntpRequest, 0, NTP_PACKET_SIZE);
  // LI=0, VN=4, Mode=3 (client)
  ntpRequest[0] = 0b00100011;

  // Request token for request/response correlation.
  // We write a deterministic 64-bit value into the Transmit Timestamp field.
  // The server must copy it into the Originate Timestamp field in its response.
  uint32_t nowMs = millis();
  reqTxSec  = nowMs;   // token (not an actual NTP seconds value)
  reqTxFrac = 0;

  ntpRequest[40] = (reqTxSec >> 24) & 0xFF;
  ntpRequest[41] = (reqTxSec >> 16) & 0xFF;
  ntpRequest[42] = (reqTxSec >> 8)  & 0xFF;
  ntpRequest[43] = (reqTxSec)       & 0xFF;

  ntpRequest[44] = (reqTxFrac >> 24) & 0xFF;
  ntpRequest[45] = (reqTxFrac >> 16) & 0xFF;
  ntpRequest[46] = (reqTxFrac >> 8)  & 0xFF;
  ntpRequest[47] = (reqTxFrac)       & 0xFF;
}

bool NTP2::checkValid(uint32_t tempTimeSeconds) {
  if (tempTimeSeconds == 0) return false;
  
  // Check Leap Indicator (bits 7-6): reject if 3 (alarm/unsynchronized)
  uint8_t li = (ntpQuery[0] & 0xC0) >> 6;
  if (li == 3) return false;
  
  uint8_t version = (ntpQuery[0] & 0x38) >> 3;
  if (version < 3 || version > 4) return false;
  
  uint8_t mode = ntpQuery[0] & 0x07;
  if (mode != 4 && mode != 5) return false;
  
  uint8_t stratum = ntpQuery[1];
  // Reject stratum 0 (already handled as KoD) and stratum 16 (unsynchronized)
  return (stratum >= 1 && stratum <= 15);
}

time_t NTP2::epoch() {
  if (ntpTimeSeconds == 0 || lastSyncMillis == 0) return 0;
  
  // Calculate elapsed time using proper signed arithmetic for millis() wraparound
  uint32_t now = millis();
  int32_t elapsedMs = (int32_t)(now - lastSyncMillis);
  if (elapsedMs < 0) elapsedMs = 0;
  
  // Use the stored high-precision timestamp and add elapsed time
  // This preserves fractional seconds from the NTP response
  uint64_t currentNtpMillis = ntpMillisAtSync + (uint32_t)elapsedMs;
  
  // Convert to Unix epoch (seconds since Jan 1, 1970)
  // NTP epoch is Jan 1, 1900, so subtract 70 years in seconds.
  // Round to the NEAREST second (+500 ms before the divide) rather than
  // flooring — a plain integer divide left the clock set to the whole second
  // below the true time, biasing every NTP2-synced clock up to ~1 s slow.
  // Rounding centers the error at ~0 (±0.5 s) so the display tracks true time.
  time_t unixNow = (time_t)(((currentNtpMillis + 500ULL) / 1000ULL) - SEVENTYYEARS);

  // Plausibility guard: reject obviously-wrong epochs.
  // Adjust these bounds if you need to support earlier dates.
  const time_t MIN_OK = 946684800UL;   // 2000-01-01
  const time_t MAX_OK = 4102444800UL;  // 2100-01-01
  if (unixNow < MIN_OK || unixNow > MAX_OK) return 0;

  return unixNow;
}

uint32_t NTP2::timestamp() {
  return lastResponseMillis;
}

bool NTP2::ntpStat() {
  // "Do we have a usable synced time?" — true as soon as we've had any
  // successful sync, and stays true across transient bad packets. Callers
  // pairing this with epoch() get a continuously-advancing clock; callers
  // wanting current-packet status should check the return of update() /
  // forceUpdate() instead.
  return (ntpTimeSeconds > 0 && lastSyncMillis > 0);
}