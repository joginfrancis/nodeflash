// bus_packet.h - RS-485 packet framing used by nodeflash (bootloader, gateway,
// client library). Byte-for-byte the same format as the wall's protocol v2
// (common/wall_protocol.h in the wall project), kept separate so the bundle
// builds on its own. If the wall's framing ever changes, change both.
//
// Packet: [0xAA][DEST][CMD][LEN lo][LEN hi][PAYLOAD x LEN][CRC lo][CRC hi]
// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over DEST, CMD, LEN and PAYLOAD.
#pragma once
#include <stdint.h>
#include <string.h>

#ifndef BUS_BAUD_HZ
#define BUS_BAUD_HZ 1000000  // must match the application's bus speed
#endif
constexpr uint32_t BUS_BAUD = BUS_BAUD_HZ;

constexpr uint8_t  PROTO_SOF        = 0xAA;
constexpr uint8_t  ADDR_MASTER      = 0x00;  // replies from nodes go here
constexpr uint8_t  ADDR_BROADCAST   = 0xFF;  // nodes are 0x01..0x14
constexpr uint16_t PROTO_MAX_PAYLOAD = 512;
constexpr uint8_t  PROTO_OVERHEAD   = 7;     // SOF, DEST, CMD, LEN x2, CRC x2
constexpr uint8_t  PROTO_BYTE_GAP_MS = 3;    // longer silence mid-frame = drop it

inline uint16_t crc16Update(uint16_t crc, uint8_t b) {
  crc ^= (uint16_t)b << 8;
  for (uint8_t i = 0; i < 8; i++) crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
  return crc;
}

// Builds a complete frame into out (needs len + PROTO_OVERHEAD bytes). Returns its length.
inline uint16_t buildFrame(uint8_t* out, uint8_t dest, uint8_t cmd, const uint8_t* payload, uint16_t len) {
  uint16_t n = 0, crc = 0xFFFF;
  out[n++] = PROTO_SOF;
  const uint8_t hdr[4] = {dest, cmd, (uint8_t)(len & 0xFF), (uint8_t)(len >> 8)};
  for (uint8_t b : hdr) { out[n++] = b; crc = crc16Update(crc, b); }
  for (uint16_t i = 0; i < len; i++) { out[n++] = payload[i]; crc = crc16Update(crc, payload[i]); }
  out[n++] = crc & 0xFF;
  out[n++] = crc >> 8;
  return n;
}

// Byte-by-byte receiver. Feed every received byte; when feed() returns true,
// dest, cmd, len and payload hold one valid frame until the next feed().
class FrameParser {
public:
  uint8_t  dest = 0, cmd = 0;
  uint16_t len = 0;
  uint8_t  payload[PROTO_MAX_PAYLOAD];
  uint32_t crcErrors = 0;

  void reset() { state_ = WAIT_SOF; }

  bool feed(uint8_t b, uint32_t nowMs) {
    if (state_ != WAIT_SOF && (uint32_t)(nowMs - lastMs_) > PROTO_BYTE_GAP_MS) state_ = WAIT_SOF;
    lastMs_ = nowMs;
    switch (state_) {
      case WAIT_SOF: if (b == PROTO_SOF) { crc_ = 0xFFFF; state_ = DEST; } return false;
      case DEST:  dest = b; crc_ = crc16Update(crc_, b); state_ = CMD;  return false;
      case CMD:   cmd = b;  crc_ = crc16Update(crc_, b); state_ = LEN_L; return false;
      case LEN_L: len = b;  crc_ = crc16Update(crc_, b); state_ = LEN_H; return false;
      case LEN_H:
        len |= (uint16_t)b << 8; crc_ = crc16Update(crc_, b); idx_ = 0;
        if (len > PROTO_MAX_PAYLOAD) { state_ = WAIT_SOF; return false; }
        state_ = len ? PAYLOAD : CRC_L; return false;
      case PAYLOAD:
        payload[idx_++] = b; crc_ = crc16Update(crc_, b);
        if (idx_ >= len) state_ = CRC_L;
        return false;
      case CRC_L: rxCrc_ = b; state_ = CRC_H; return false;
      case CRC_H:
        rxCrc_ |= (uint16_t)b << 8; state_ = WAIT_SOF;
        if (rxCrc_ == crc_) return true;
        crcErrors++; return false;
    }
    return false;
  }

private:
  enum State : uint8_t { WAIT_SOF, DEST, CMD, LEN_L, LEN_H, PAYLOAD, CRC_L, CRC_H };
  State    state_ = WAIT_SOF;
  uint16_t idx_ = 0, crc_ = 0xFFFF, rxCrc_ = 0;
  uint32_t lastMs_ = 0;
};
