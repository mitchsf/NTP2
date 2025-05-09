/* NTP2.cpp
NTP2 library for Arduino - non-blocking, Kiss-o'-Death codes
   The MIT License (MIT)
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
  // Force an update at startup.
  lastUpdate = millis() - activeInterval;
}

void NTP2::begin(const char* server) {
  this->server = server;
  udp->begin(NTP_PORT);
  force = true;
  lastUpdate = millis() - activeInterval;
}

void NTP2::begin(IPAddress serverIP) {
  this->serverIP = serverIP;
  udp->begin(NTP_PORT);
  force = true;
  lastUpdate = millis() - activeInterval;
}

void NTP2::stop() {
  udp->stop();
}

void NTP2::updateInterval(unsigned long int uInterval) {
  activeInterval = uInterval;
  defaultInterval = uInterval;
}

void NTP2::responseDelay(uint32_t newDelay) {
  responseDelayValue = newDelay;
}

void NTP2::retryDelay(uint32_t newDelay) {
  retryDelayValue = newDelay;
}

NTPStatus NTP2::forceUpdate() {
  // If a request is pending, ignore forcing.
  if (requestTimestamp != 0) {
    ntpSt = NTP_BAD_PACKET;
    return ntpSt;
  }
  force = true;
  return update();
}

// Note: No helper function needed as uint32_t subtraction automatically handles overflow
// Example: 500 - 4294967000 = 500 + (2^32 - 4294967000) = 500 + 1296 = 1796

NTPStatus NTP2::update() {
  // If there is a valid response latched that has not been delivered, deliver GOOD_TIME exactly once.
  if (pendingGood) {
    pendingGood = false;
    ntpSt = NTP_CONNECTED;
    return ntpSt;
  }
  
  // If a request is pending:
  if (requestTimestamp != 0) {
    if (millis() - requestTimestamp >= responseDelayValue) {
      return processNTPResponse();
    } else {
      return NTP_IDLE;
    }
  }
  
  // If no request is pending and the active interval has elapsed, send a new request.
  if (millis() - lastUpdate >= activeInterval) {
    return sendNTPRequest();
  }
  
  return NTP_IDLE;
}

NTPStatus NTP2::sendNTPRequest() {
  // Clear the latch.
  pendingGood = false;
  ntpTimeSeconds = 0;
  lastUpdate = millis();
  init();
  
  if (server == nullptr) {
    udp->beginPacket(serverIP, NTP_PORT);
  } else {
    udp->beginPacket(server, NTP_PORT);
  }
    
  udp->write(ntpRequest, NTP_PACKET_SIZE);
  udp->endPacket();
  
  requestTimestamp = millis();
  force = false;
  ntpSt = NTP_IDLE;
  return ntpSt;
}

NTPStatus NTP2::processNTPResponse() {
  requestTimestamp = 0; // Clear pending request.
  uint8_t size = udp->parsePacket();
  
  if (size != NTP_PACKET_SIZE) {
    return badRead();
  }
  
  udp->read(ntpQuery, NTP_PACKET_SIZE);
  
  // Check for a KoD (Kiss‑o‑Death) packet.
  if (ntpQuery[1] == 0) {
    char kodMessage[5] = { ntpQuery[12], ntpQuery[13], ntpQuery[14], ntpQuery[15], '\0' };
    for (size_t i = 0; i < sizeof(kodLookup) / sizeof(kodLookup[0]); i++) {
      if (strcmp(kodMessage, kodLookup[i].code) == 0) {
        return static_cast<NTPStatus>(kodLookup[i].ret);
      }
    }
    activeInterval = retryDelayValue;
    ntpSt = NTP_UNKNOWN_KOD;
    return ntpSt;
  }
   
  // Process the NTP response correctly - extract the transmit timestamp
  // NTP timestamp is seconds since 1900-01-01
  ntpTimeSeconds = ((uint32_t)ntpQuery[40] << 24) | 
                   ((uint32_t)ntpQuery[41] << 16) | 
                   ((uint32_t)ntpQuery[42] << 8) | 
                    (uint32_t)ntpQuery[43];
  
  // Store the local millis() at the time of sync
  localMillisAtSync = millis();
  
  if (checkValid()) {
    if (activeInterval != defaultInterval) {
      activeInterval = defaultInterval;
    }
    ntpSt = NTP_CONNECTED;
    lastResponseMillis = millis();
    // Latch the good response so that the next update() call returns GOOD_TIME exactly once.
    pendingGood = true;
    return NTP_IDLE;  // Return IDLE now; on the next update() call, GOOD_TIME is delivered.
  } else {
    return badRead();
  }
}

NTPStatus NTP2::badRead() {
  activeInterval = retryDelayValue;
  ntpSt = NTP_BAD_PACKET;
  return ntpSt;
}

void NTP2::init() {
  memset(ntpRequest, 0, NTP_PACKET_SIZE);
  ntpRequest[0] = 0b11100011;  // LI=0, Version=4, Mode=3 (client)
  ntpRequest[1] = 0;
  ntpRequest[2] = 6;
  ntpRequest[3] = 0xEC;
  ntpRequest[12] = 49;
  ntpRequest[13] = 0x4E;
  ntpRequest[14] = 49;
  ntpRequest[15] = 52;
}

bool NTP2::checkValid() {
  // If we didn't get a transmit timestamp, packet is invalid
  if (ntpTimeSeconds == 0) {
    return false;
  }
  
  // Check leap indicator (LI) field - shouldn't be 3 (alarm condition)
  if ((ntpQuery[0] & 0xC0) == 0xC0) {
    return false;
  }
  
  // Check the version number - should be 3 or 4
  uint8_t version = (ntpQuery[0] & 0x38) >> 3;
  if (version < 3 || version > 4) {
    return false;
  }
  
  // Check the mode - should be 4 (server) or 5 (broadcast)
  uint8_t mode = ntpQuery[0] & 0x07;
  if (mode != 4 && mode != 5) {
    return false;
  }
  
  // Check stratum - should be between 1 and 15
  if (ntpQuery[1] < 1 || ntpQuery[1] > 15) {
    return false;
  }
  
  // Extract key timestamp fields for further validation
  uint32_t refTsSec = ((uint32_t)ntpQuery[16] << 24) | 
                     ((uint32_t)ntpQuery[17] << 16) | 
                     ((uint32_t)ntpQuery[18] << 8) | 
                      (uint32_t)ntpQuery[19];
                     
  uint32_t origTsSec = ((uint32_t)ntpQuery[24] << 24) | 
                      ((uint32_t)ntpQuery[25] << 16) | 
                      ((uint32_t)ntpQuery[26] << 8) | 
                       (uint32_t)ntpQuery[27];
                      
  uint32_t recvTsSec = ((uint32_t)ntpQuery[32] << 24) | 
                      ((uint32_t)ntpQuery[33] << 16) | 
                      ((uint32_t)ntpQuery[34] << 8) | 
                       (uint32_t)ntpQuery[35];
  
  // Perform basic sanity checks on timestamps
  // Reference timestamp shouldn't be 0
  if (refTsSec == 0) {
    return false;
  }
  
  // Receive timestamp should be non-zero and not later than transmit timestamp
  if (recvTsSec == 0 || recvTsSec > ntpTimeSeconds) {
    return false;
  }
  
  return true;
}

time_t NTP2::epoch() {
  if (ntpTimeSeconds == 0 || localMillisAtSync == 0) {
    return 0; // No valid synchronization has occurred
  }
  
  // Calculate how many seconds have elapsed since our last sync
  uint32_t millisSinceSync = millis() - localMillisAtSync;
  uint32_t secondsSinceSync = millisSinceSync / 1000;
  
  // Return the current time as seconds since UNIX epoch (1970-01-01)
  // NTP time starts at 1900-01-01, so subtract 70 years worth of seconds
  return (time_t)(ntpTimeSeconds - SEVENTYYEARS + secondsSinceSync);
}

uint32_t NTP2::timestamp() {
  return lastResponseMillis;
}

bool NTP2::ntpStat() {
  return (ntpSt == NTP_CONNECTED);
}
