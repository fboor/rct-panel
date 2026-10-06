// A stand-in for the ESP32 WiFi, connected to nothing.
//
// The GUI shows the panel's own IP address on the info page and asks the network
// layer whether it is still connecting. Both answers here are fixed ones, chosen so
// that the pages show their normal state: the link is up, and the address is the
// one a real installation of this panel gets. Nothing dials out.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_PANEL_SIM_WIFI_H
#define RCT_PANEL_SIM_WIFI_H

#include <errno.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "Arduino.h"

enum WiFiMode_t { WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3 };
enum wl_status_t { WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL, WL_CONNECTED,
                   WL_CONNECT_FAILED, WL_DISCONNECTED };

struct WiFiClass {
  void begin(const char * = nullptr) {}
  void mode(WiFiMode_t) {}
  WiFiMode_t getMode() { return WIFI_STA; }
  bool setAutoReconnect(bool) { return true; }
  bool setSleep(bool) { return true; }
  bool reconnect() { return true; }
  IPAddress localIP() { return IPAddress(); }
  IPAddress softAPIP() { return IPAddress(); }
  IPAddress gatewayIP() { return IPAddress(); }
  IPAddress dnsIP() { return IPAddress(); }
  // The network name the settings page shows. Named for what it is.
  const char *SSID() const { return "simulator"; }
  int32_t RSSI() { return -52; }
  // The RCT driver asks for the link's state. Here the link is always up, because a
  // driver that is talking to something has a link - and the simulator's own SIM
  // driver never asks.
  wl_status_t status() { return WL_CONNECTED; }
};

extern WiFiClass WiFi;

// A TCP client over a POSIX socket.
//
// This one is not a stub in the usual sense. The whole point of running the real
// device layer here is that --device RCT reaches a REAL inverter over the network,
// through the shipped TcpTransport and the shipped RctDriver, so the code that
// talks to an inverter is the code the panel runs and not a re-implementation of it.
class WiFiClient {
 public:
  WiFiClient() : m_fd(-1) {}
  ~WiFiClient() { stop(); }

  bool connect(const char *host, uint16_t port, uint32_t timeoutMs) {
    stop();
    // Resolve first: getaddrinfo carries the whole IPv6 story, and the panel only
    // ever sees an IPv4 address anyway.
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = nullptr;
    char dienst[8];
    snprintf(dienst, sizeof(dienst), "%u", (unsigned)port);
    if (getaddrinfo(host, dienst, &hints, &res) != 0 || res == nullptr) {
      return false;
    }
    m_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (m_fd < 0) {
      freeaddrinfo(res);
      return false;
    }
    const bool ok = connectWithTimeout(res->ai_addr, timeoutMs) == 0;
    freeaddrinfo(res);
    if (!ok) {
      stop();
    }
    return ok;
  }

  size_t write(const uint8_t *buf, size_t n) {
    if (m_fd < 0) {
      return 0;
    }
    const ssize_t k = ::send(m_fd, buf, n, MSG_NOSIGNAL);
    return (k > 0) ? (size_t)k : 0;
  }

  // HOW MANY BYTES ARE WAITING - and it must not consume one.
  //
  // The first version did recv() into a local buffer, counted the bytes and threw
  // them away, which reads like a drain and is not one: the driver asks available()
  // and then calls read() once per byte, so every frame the inverter sent was
  // counted and then dropped. The symptom was silence rather than an error - no CRC
  // mismatches, no exceptions, and 0/60 fresh forever, with the TCP port open the
  // whole time.
  //
  // FIONREAD asks the kernel how much is in the receive queue and leaves it there,
  // which is what WiFiClient::available() does.
  int available() {
    if (m_fd < 0) {
      return 0;
    }
    int anzahl = 0;
    if (ioctl(m_fd, FIONREAD, &anzahl) != 0) {
      return 0;
    }
    return anzahl;
  }

  int read() {
    if (m_fd < 0) {
      return -1;
    }
    uint8_t b;
    const ssize_t k = recv(m_fd, &b, 1, MSG_DONTWAIT);
    return (k == 1) ? (int)b : -1;
  }

  // Hand the socket of the request currently being served to a client object, so
  // that a handler writing through its own WiFiClient reference writes the response.
  // attach() does not own the socket: the server closes it when it is done.
  void attach(int fd) { m_fd = fd; m_gehoert = false; }

  bool connected() const { return m_fd >= 0; }
  void stop() {
    if (m_fd >= 0 && m_gehoert) {
      ::close(m_fd);
    }
    m_fd = -1;
    m_gehoert = true;
  }

 private:
  int connectWithTimeout(const struct sockaddr *addr, uint32_t timeoutMs) {
    int fl = fcntl(m_fd, F_GETFL, 0);
    fcntl(m_fd, F_SETFL, fl | O_NONBLOCK);
    int rc = ::connect(m_fd, addr, sizeof(struct sockaddr));
    if (rc == 0) {
      return 0;
    }
    if (errno != EINPROGRESS) {
      return -1;
    }
    fd_set w;
    FD_ZERO(&w);
    FD_SET(m_fd, &w);
    struct timeval tv = {(time_t)(timeoutMs / 1000),
                         (suseconds_t)((timeoutMs % 1000) * 1000)};
    if (select(m_fd + 1, nullptr, &w, nullptr, &tv) <= 0) {
      return -1;
    }
    int fehler = 0;
    socklen_t len = sizeof(fehler);
    getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &fehler, &len);
    return fehler == 0 ? 0 : -1;
  }

  int m_fd;
  bool m_gehoert = true;   // false while a server hands the socket over
};

#endif // RCT_PANEL_SIM_WIFI_H