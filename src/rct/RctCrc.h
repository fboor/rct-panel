// The checksum of an RCT frame.
//
// CRC-16 with polynomial 0x1021, preset 0xFFFF, most significant bit first and
// no final inversion: the variant usually called CRC-16/CCITT-FALSE, whose check
// value for the nine bytes "123456789" is 0x29B1. That is what the device
// expects, so it is what this computes.
//
// One deviation from the plain standard: data of odd length is padded with one
// 0x00 byte before the checksum is taken. The extension frame the panel sends
// on every connect (0x2b 0x3c 0xe1, three bytes) is such a frame, and the
// device checks it over the padded length - without the pad byte the checksum
// does not match and the frame is dropped. A six-byte READ frame has even
// length and is therefore not touched.
//
// Header-only and free of Arduino so the check values can be verified on the
// build machine (tools/crc_test) instead of only by a device that happens to
// accept our frames. The values in that test were computed from the polynomial
// with a table-driven implementation; they also agree with the frames the
// device and the simulator have accepted (see tools/crc_test).
//
// SPDX-License-Identifier: MIT
#ifndef RCT_RCTCRC_H
#define RCT_RCTCRC_H

#include <stddef.h>
#include <stdint.h>

namespace rctcrc {

// Checksum of len bytes. A null pointer with len 0 is allowed and gives the
// preset; anything else is a caller's mistake and reads as it comes.
inline uint16_t compute(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  const size_t padded = len + (len & (size_t)1);
  for (size_t i = 0; i < padded; i++) {
    const uint8_t byte = (i < len) ? data[i] : 0x00;
    crc ^= (uint16_t)((uint16_t)byte << 8);
    for (int bit = 0; bit < 8; bit++) {
      if (crc & 0x8000u) {
        crc = (uint16_t)((uint16_t)(crc << 1) ^ 0x1021u);
      } else {
        crc = (uint16_t)(crc << 1);
      }
    }
  }
  return crc;
}

} // namespace rctcrc

#endif // RCT_RCTCRC_H