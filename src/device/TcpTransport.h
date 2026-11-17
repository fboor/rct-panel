// The one transport: a TCP socket.
//
// WiFiClient is not inlined here - it is the Arduino ESP32 socket, wrapped so
// that the driver never names it and so that a host test can hand the driver a
// transport of its own without a network.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_DEVICE_TCP_TRANSPORT_H
#define RCT_DEVICE_TCP_TRANSPORT_H

#include <WiFi.h>

#include "DeviceTransport.h"

class TcpTransport : public DeviceTransport {
public:
  bool open(const char *host, const char *port, uint32_t timeoutMs) override {
    return m_client.connect(host, (uint16_t)atol(port), timeoutMs);
  }

  size_t write(const uint8_t *buf, size_t n) override {
    // WiFiClient::write() may return a short count. The driver's contract says
    // all n bytes or none, and a frame with a missing byte is worse than no
    // frame: the receiver then reads a checksum that belongs to the next frame.
    // Tinkerforge's TCP client does the same bounded loop; HA's asyncio writer
    // buffers the whole frame before drain(). Ten tries is that client's limit.
    size_t done = 0;
    for (int tries = 0; tries < 10 && done < n; ++tries) {
      const size_t now = m_client.write(buf + done, n - done);
      if (now == 0) {
        break;
      }
      done += now;
    }
    return done;
  }

  int available() override { return m_client.available(); }
  int read() override { return m_client.read(); }

  // The socket itself: closed by the peer (remote side gone, network down) and
  // still open locally. That is what tells the driver to stop and reconnect -
  // a Wi-Fi drop shows up here, a device that stopped answering does not.
  bool peerOpen() override { return m_client.connected(); }

  void close() override { m_client.stop(); }

private:
  WiFiClient m_client;
};

#endif // RCT_DEVICE_TCP_TRANSPORT_H