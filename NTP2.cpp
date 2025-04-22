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

void NTP2::pollInterval(uint32_t newInterval) {
  activeInterval = newInterval;
  defaultInterval = newInterval;
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

NTPStatus NTP2::update() {
  // If there is a valid response latched that has not been delivered, deliver GOOD_TIME exactly once.
  if (pendingGood) {
    pendingGood = false;
    ntpSt = NTP_CONNECTED;
    return ntpSt;
  }
  
  // If a request is pending:
  if (requestTimestamp != 0) {
    if (millis() - requestTimestamp >= responseDelayValue)
      return processNTPResponse();
    else
      return NTP_IDLE;
  }
  
  // If no request is pending and the active interval has elapsed, send a new request.
  if (millis() - lastUpdate >= activeInterval)
    return sendNTPRequest();
  
  return NTP_IDLE;
}

NTPStatus NTP2::sendNTPRequest() {
  // Clear the latch.
  pendingGood = false;
  utcTime = 0;
  lastUpdate = millis();
  init();
  
  if (server == nullptr)
    udp->beginPacket(serverIP, NTP_PORT);
  else
    udp->beginPacket(server, NTP_PORT);
    
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
  if (size == NTP_PACKET_SIZE) {
    udp->read(ntpQuery, NTP_PACKET_SIZE);
    // Check for a KoD (Kiss‑o‑Death) packet.
    if (ntpQuery[1] == 0) {
      char kodMessage[5] = { ntpQuery[12], ntpQuery[13], ntpQuery[14], ntpQuery[15], '\0' };
      for (size_t i = 0; i < sizeof(kodLookup) / sizeof(kodLookup[0]); i++) {
        if (strcmp(kodMessage, kodLookup[i].code) == 0)
          return static_cast<NTPStatus>(kodLookup[i].ret);
      }
      activeInterval = retryDelayValue;
      ntpSt = NTP_UNKNOWN_KOD;
      return ntpSt;
    }
    
    // Process the NTP response.
    ntpTime = (ntpQuery[40] << 24) | (ntpQuery[41] << 16) | (ntpQuery[42] << 8) | ntpQuery[43];
    uint32_t delaySeconds = (millis() - lastUpdate) / 1000;
    utcTime = ntpTime - SEVENTYYEARS + delaySeconds;
    
    if (checkValid()) {
      if (activeInterval != defaultInterval)
        activeInterval = defaultInterval;
      ntpSt = NTP_CONNECTED;
      lastResponseMillis = millis();
      // Latch the good response so that the next update() call returns GOOD_TIME exactly once.
      pendingGood = true;
      return NTP_IDLE;  // Return IDLE now; on the next update() call, GOOD_TIME is delivered.
    } else {
      return badRead();
    }
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
  ntpRequest[0] = 0b11100011;
  ntpRequest[1] = 0;
  ntpRequest[2] = 6;
  ntpRequest[3] = 0xEC;
  ntpRequest[12] = 49;
  ntpRequest[13] = 0x4E;
  ntpRequest[14] = 49;
  ntpRequest[15] = 52;
}

bool NTP2::checkValid() {
  uint32_t highWord, lowWord; // some for future use
  highWord = ((ntpQuery[16] << 8) | ntpQuery[17]) & 0x0000FFFF;
  lowWord  = ((ntpQuery[18] << 8) | ntpQuery[19]) & 0x0000FFFF;
  uint32_t reftsSec = (highWord << 16) | lowWord;
  
  highWord = ((ntpQuery[32] << 8) | ntpQuery[33]) & 0x0000FFFF;
  lowWord  = ((ntpQuery[34] << 8) | ntpQuery[35]) & 0x0000FFFF;
  uint32_t rcvtsSec = (highWord << 16) | lowWord;
  
  highWord = ((ntpQuery[40] << 8) | ntpQuery[41]) & 0x0000FFFF;
  lowWord  = ((ntpQuery[42] << 8) | ntpQuery[43]) & 0x0000FFFF;
  uint32_t secsSince1900 = (highWord << 16) | lowWord;
  
  highWord = ((ntpQuery[44] << 8) | ntpQuery[45]) & 0x0000FFFF;
  lowWord  = ((ntpQuery[46] << 8) | ntpQuery[47]) & 0x0000FFFF;
  uint32_t fraction = (highWord << 16) | lowWord;
  
  if ((ntpQuery[1] < 1) || (ntpQuery[1] > 15) ||
      (reftsSec == 0) || (rcvtsSec == 0) || (rcvtsSec > secsSince1900))
    return false;
    
  return true;
}

time_t NTP2::epoch() {
  return utcTime;
}

uint32_t NTP2::timestamp() {
  return lastResponseMillis;
}

bool NTP2::ntpStat() {
  return (ntpSt == NTP_CONNECTED);
}
