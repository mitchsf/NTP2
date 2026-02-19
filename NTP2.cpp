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
  udp->begin(NTP_PORT);
  force = true;
  lastUpdate = millis() - activeInterval;
}

void NTP2::begin(IPAddress serverIP) {
  this->serverIP = serverIP;
  this->server = nullptr;
  udp->begin(NTP_PORT);
  force = true;
  lastUpdate = millis() - activeInterval;
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
    if ((int32_t)(millis() - requestTimestamp) >= (int32_t)responseDelayValue) {
      NTPStatus result = processNTPResponse();
      if (result == NTP_CONNECTED) {
        return NTP_CONNECTED;
      }
      return result;
    } else {
      return NTP_IDLE;
    }
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
  requestTimestamp = 0;
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
    return badRead();
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
    return badRead();
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

  ntpTimeSeconds = txSec;
  lastSyncMillis = millis();
  // Fix overflow: cast txSec to uint64_t before multiplication
  ntpMillisAtSync = ((uint64_t)txSec * 1000ULL) + fracMillis;
  if (activeInterval != defaultInterval) activeInterval = defaultInterval;

  lastResponseMillis = millis();
  ntpSt = NTP_CONNECTED;
  return NTP_CONNECTED;
}

NTPStatus NTP2::badRead() {
  activeInterval = retryDelayValue;
  // Do NOT zero ntpTimeSeconds — preserve last known good time
  // so epoch() continues returning valid time between syncs
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
  // NTP epoch is Jan 1, 1900, so subtract 70 years in seconds
  time_t unixNow = (time_t)((currentNtpMillis / 1000ULL) - SEVENTYYEARS);

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
  return (ntpSt == NTP_CONNECTED);
}