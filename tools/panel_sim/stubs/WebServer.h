// A stand-in for the ESP32 WebServer library, over POSIX sockets.
//
// The panel's own web interface, unchanged, in a window on the build machine: four
// pages, two JSON endpoints and the action endpoint, served by the shipped
// src/web/WebServer.cpp. What is here is the thirteen methods that file calls and
// nothing else - a second HTTP server in this tree would be the same interface with
// two truths in it.
//
// It is deliberately small. It parses a request line, a header block and a body,
// matches the path against the registered routes (including the "{}" pattern the
// two file handlers use), and writes one response. It does no keep-alive, no chunked
// transfer and no TLS, because the interface it serves does not ask for any: every
// download there goes out with a Content-Length (see the note at the top of
// WebServer.cpp, rule 3) and closes.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_PANEL_SIM_WEBSERVER_H
#define RCT_PANEL_SIM_WEBSERVER_H

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <map>
#include <string>
#include <vector>

#include "Arduino.h"
#include "WiFi.h"

typedef enum { HTTP_ANY, HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE } HTTPMethod;

enum HTTPUploadStatus { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END,
                        UPLOAD_FILE_ABORTED };

// The upload object, only so that the firmware's upload handler compiles. The
// simulator serves the update page and accepts the POST; writing a firmware image
// through it would be theatre, so the body is accepted and dropped.
struct HTTPUpload {
  HTTPUploadStatus status = UPLOAD_FILE_START;
  String filename;
  String name;
  String type;
  size_t size = 0;         // bytes of this chunk
  size_t totalSize = 0;    // bytes announced for the whole file
  size_t currentSize = 0;  // bytes written so far; the firmware reads this one
  uint8_t buf[512];
  void write(const uint8_t *d, size_t n) { (void)d; (void)n; }
};

// A route. The path is stored with "{}" left in it, and the matcher below decides.
class Uri {
 public:
  Uri() {}
  explicit Uri(const char *p) : m_path(p ? p : "") {}
  const String &path() const { return m_path; }
  String m_path;
};

class UriBraces : public Uri {
 public:
  explicit UriBraces(const char *p) : Uri(p) {}
};

class WebServer {
 public:
  explicit WebServer(int port) : m_port((uint16_t)port) {}
  ~WebServer() { close(); }

  // --- routes ---
  void on(const Uri &uri, HTTPMethod meth, void (*fn)()) {
    Route r;
    r.path = uri.path().m_str;
    r.method = meth;
    r.fn = fn;
    m_routen.push_back(r);
  }
  void on(const UriBraces &uri, HTTPMethod meth, void (*fn)()) {
    on(Uri(uri.path().c_str()), meth, fn);
  }
  // The upload route: the firmware registers a GET and a POST handler for the same
  // path, the POST being the upload callback that runs while the body arrives. Here
  // both run, in the order the library uses - the POST one first, on the last
  // chunk, which is what the firmware's own handler expects.
  void on(const Uri &uri, HTTPMethod meth, void (*fn)(), void (*uploadFn)()) {
    Route r;
    r.path = uri.path().m_str;
    r.method = meth;
    r.fn = fn;
    r.uploadFn = uploadFn;
    m_routen.push_back(r);
  }
  void onNotFound(void (*fn)()) { m_notFound = fn; }

  // --- lifecycle ---
  //
  // TWO SOCKETS, because "localhost" is not one address. It resolves to ::1 first on
  // this machine and to 127.0.0.1 second, and a browser tries them in that order:
  // a server on the IPv4 loopback alone answers curl - which falls back - and not the
  // fetch inside the page, which does not. The page loaded and the numbers did not,
  // and that is the whole of it.
  //
  // Both are bound to the LOOPBACK and not to INADDR_ANY: a tool that answers on
  // every interface of a machine is a tool other machines can reach, and this one is
  // for the person sitting in front of the build machine.
  void begin() {
    if (m_fd >= 0 || m_fd6 >= 0) {
      return;
    }
    m_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_fd >= 0) {
      int ja = 1;
      setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &ja, sizeof(ja));
      struct sockaddr_in a = {};
      a.sin_family = AF_INET;
      a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      a.sin_port = htons(m_port);
      if (bind(m_fd, (struct sockaddr *)&a, sizeof(a)) != 0 ||
          listen(m_fd, 4) != 0) {
        ::close(m_fd);
        m_fd = -1;
      }
    }
    m_fd6 = socket(AF_INET6, SOCK_STREAM, 0);
    if (m_fd6 >= 0) {
      int nur6 = 1;   // keep the two apart: one socket per family
      setsockopt(m_fd6, IPPROTO_IPV6, IPV6_V6ONLY, &nur6, sizeof(nur6));
      struct sockaddr_in6 a6 = {};
      a6.sin6_family = AF_INET6;
      a6.sin6_addr = in6addr_loopback;
      a6.sin6_port = htons(m_port);
      if (bind(m_fd6, (struct sockaddr *)&a6, sizeof(a6)) != 0 ||
          listen(m_fd6, 4) != 0) {
        ::close(m_fd6);
        m_fd6 = -1;
      }
    }
    if (m_fd < 0 && m_fd6 < 0) {
      printf("WebServer: bind/listen auf Port %u schlug fehl\n", m_port);
      return;
    }
    printf("WebServer: lauscht auf 127.0.0.1:%u", m_port);
    if (m_fd >= 0) {
      printf(" (ipv4)");
    }
    if (m_fd6 >= 0) {
      printf(" und [::1]:%u (ipv6)", m_port);
    }
    printf("\n");
  }

  bool started() const { return m_fd >= 0 || m_fd6 >= 0; }
  void stop() { close(); }
  void close() {
    if (m_fd >= 0) {
      ::close(m_fd);
      m_fd = -1;
    }
    if (m_fd6 >= 0) {
      ::close(m_fd6);
      m_fd6 = -1;
    }
  }

  WiFiClient client() { return m_client; }

  // --- one request, non-blocking ---
  void handleClient() {
    fd_set r;
    FD_ZERO(&r);
    int hoch = -1;
    if (m_fd >= 0) {
      FD_SET(m_fd, &r);
      hoch = m_fd;
    }
    if (m_fd6 >= 0) {
      FD_SET(m_fd6, &r);
      hoch = (m_fd6 > hoch) ? m_fd6 : hoch;
    }
    if (hoch < 0) {
      return;
    }
    struct timeval tv = {0, 0};
    if (select(hoch + 1, &r, nullptr, nullptr, &tv) <= 0) {
      return;
    }
    int fd = -1;
    if (m_fd >= 0 && FD_ISSET(m_fd, &r)) {
      fd = accept(m_fd, nullptr, nullptr);
    } else if (m_fd6 >= 0 && FD_ISSET(m_fd6, &r)) {
      fd = accept(m_fd6, nullptr, nullptr);
    }
    if (fd < 0) {
      return;
    }
    bearbeite(fd);
    ::close(fd);
    m_client.stop();
  }

  void webUpdate() { handleClient(); }

  // --- the request in front of the handler ---
  bool hasArg(const char *name) const { return m_arg.find(name) != m_arg.end(); }
  String arg(const char *name) const {
    auto it = m_arg.find(name);
    return (it == m_arg.end()) ? String() : String(it->second);
  }
  String pathArg(unsigned i) const {
    return (i < m_pfadArgs.size()) ? String(m_pfadArgs[i].c_str()) : String();
  }
  HTTPUpload &upload() { return m_upload; }

  // --- the response ---
  void setContentLength(size_t n) { m_contentLength = (int64_t)n; }
  void sendHeader(const String &name, const String &value) {
    m_header.push_back(name.m_str + ": " + value.m_str);
  }
  void send(int code, const char *type, const String &body) {
    m_code = code;
    m_type = (type != nullptr) ? type : "text/html; charset=utf-8";
    m_body = body.m_str;   // String keeps its text in m_str, like the Arduino one
    flush();
  }
  void send(int code, const String &body) { send(code, "text/html; charset=utf-8", body); }
  void send(int code) { send(code, "text/plain; charset=utf-8", String()); }

  uint16_t port() const { return m_port; }

 private:
  struct Route {
    std::string path;
    HTTPMethod method;
    void (*fn)();
    void (*uploadFn)() = nullptr;
  };

  // "/daten/{}" matches "/daten/RCT-202610.csv" and puts "RCT-202610.csv" into
  // m_pfadArgs. Anything else is an exact match.
  bool passt(const std::string &muster, const std::string &pfad,
             std::vector<std::string> *args) const {
    const size_t k = muster.find("{}");
    if (k == std::string::npos) {
      return muster == pfad;
    }
    if (pfad.size() < muster.size() - 2 ||
        pfad.compare(0, k, muster, 0, k) != 0) {
      return false;
    }
    const size_t ende = muster.rfind("{}");
    const std::string rest = muster.substr(ende + 2);
    if (pfad.size() < k + rest.size()) {
      return false;
    }
    if (rest.size() > 0 &&
        pfad.compare(pfad.size() - rest.size(), rest.size(), rest) != 0) {
      return false;
    }
    if (args != nullptr) {
      args->push_back(pfad.substr(k, pfad.size() - k - rest.size()));
    }
    return true;
  }

  void bearbeite(int fd) {
    m_fdFd = fd;
    std::string request;
    char puffer[2048];
    // The request head ends at a blank line; the body, if any, after it.
    size_t kopfEnde = std::string::npos;
    while ((int)request.size() < 16384) {
      fd_set r;
      FD_ZERO(&r);
      FD_SET(fd, &r);
      struct timeval tv = {0, 200000};
      if (select(fd + 1, &r, nullptr, nullptr, &tv) <= 0) {
        break;
      }
      const ssize_t n = recv(fd, puffer, sizeof(puffer), 0);
      if (n <= 0) {
        break;
      }
      request.append(puffer, (size_t)n);
      kopfEnde = request.find("\r\n\r\n");
      if (kopfEnde != std::string::npos) {
        const size_t erwartet = contentLengthAus(request);
        if (request.size() - (kopfEnde + 4) >= erwartet) {
          break;
        }
      }
    }
    if (kopfEnde == std::string::npos) {
      return;
    }
    m_arg.clear();
    m_pfadArgs.clear();
    m_client.attach(fd);

    const std::string kopf = request.substr(0, kopfEnde);
    const size_t zeilenEnde = kopf.find("\r\n");
    const std::string zeile = kopf.substr(0, zeilenEnde);
    const size_t p1 = zeile.find(' ');
    const size_t p2 = (p1 == std::string::npos) ? std::string::npos : zeile.find(' ', p1 + 1);
    const std::string methode = (p1 == std::string::npos) ? "" : zeile.substr(0, p1);
    std::string pfad = (p2 == std::string::npos) ? "" : zeile.substr(p1 + 1, p2 - p1 - 1);

    // Query and form arguments, both into m_arg. The panel reads them by name and
    // does not care where they came from.
    const size_t q = pfad.find('?');
    if (q != std::string::npos) {
      lesePaare(pfad.substr(q + 1), '&');
      pfad = pfad.substr(0, q);
    }
    const std::string body = request.substr(kopfEnde + 4);
    if (!body.empty()) {
      lesePaare(body, '&');
    }
    // A form field may be urlencoded; decode the ones that are.
    for (auto &kv : m_arg) {
      kv.second = dekodiere(kv.second);
    }

    HTTPMethod hm = HTTP_GET;
    if (methode == "POST") hm = HTTP_POST;
    else if (methode == "PUT") hm = HTTP_PUT;
    else if (methode == "DELETE") hm = HTTP_DELETE;

    m_code = 200;
    m_type = "text/html; charset=utf-8";
    m_body.clear();
    m_header.clear();
    m_contentLength = -1;
    m_upload = HTTPUpload();

    bool bedient = false;
    for (const Route &r : m_routen) {
      std::vector<std::string> args;
      if (!passt(r.path, pfad, &args)) {
        continue;
      }
      if (r.method != HTTP_ANY && r.method != hm) {
        continue;
      }
      m_pfadArgs = args;
      if (r.uploadFn != nullptr) {
        // The upload callback first, on the finished body, then the handler - the
        // order the ESP32 library uses, so the firmware's own code path is the same.
        r.uploadFn();
      }
      r.fn();
      bedient = true;
      break;
    }
    if (!bedient && m_notFound != nullptr) {
      m_notFound();
    }
    if (m_code == 0) {
      // A handler that returns without answering has answered 200 with nothing,
      // which is what the library does too.
      m_code = 200;
    }
    flush();
  }

  // "a=1&b=2" into m_arg. A pair without "=" lands under its own name with an
  // empty value, which is how a checkbox-less form field arrives.
  void lesePaare(const std::string &s, char trenner) {
    size_t i = 0;
    while (i < s.size()) {
      size_t j = s.find(trenner, i);
      if (j == std::string::npos) {
        j = s.size();
      }
      const std::string paar = s.substr(i, j - i);
      const size_t eq = paar.find('=');
      if (eq == std::string::npos) {
        if (!paar.empty()) {
          m_arg[paar] = "";
        }
      } else {
        std::string wert = paar.substr(eq + 1);
        for (auto &c : wert) {
          if (c == '+') {
            c = ' ';
          }
        }
        m_arg[paar.substr(0, eq)] = wert;
      }
      i = j + 1;
    }
  }

  static std::string dekodiere(const std::string &s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
      if (s[i] == '+') {
        r.push_back(' ');
      } else if (s[i] == '%' && i + 2 < s.size()) {
        const std::string hex = s.substr(i + 1, 2);
        r.push_back((char)strtol(hex.c_str(), nullptr, 16));
        i += 2;
      } else {
        r.push_back(s[i]);
      }
    }
    return r;
  }

  static size_t contentLengthAus(const std::string &kopf) {
    const size_t p = kopf.find("Content-Length:");
    if (p == std::string::npos) {
      return 0;
    }
    return (size_t)atol(kopf.c_str() + p + 15);
  }

  void flush() {
    if (m_fdFd < 0) {
      return;
    }
    std::string kopf = "HTTP/1.1 " + std::to_string(m_code) + " " + textFor(m_code) + "\r\n";
    for (const std::string &h : m_header) {
      kopf += h + "\r\n";
    }
    kopf += "Content-Type: " + m_type + "\r\n";
    kopf += "Content-Length: " + std::to_string(m_body.size()) + "\r\n";
    kopf += "Connection: close\r\n\r\n";
    schreibe(m_fdFd, kopf.data(), kopf.size());
    schreibe(m_fdFd, m_body.data(), m_body.size());
  }

  void schreibe(int fd, const char *d, size_t n) {
    size_t done = 0;
    while (done < n) {
      const ssize_t k = ::send(fd, d + done, n - done, MSG_NOSIGNAL);
      if (k <= 0) {
        return;
      }
      done += (size_t)k;
    }
  }

  static const char *textFor(int code) {
    switch (code) {
      case 200: return "OK";
      case 204: return "No Content";
      case 303: return "See Other";
      case 304: return "Not Modified";
      case 400: return "Bad Request";
      case 403: return "Forbidden";
      case 404: return "Not Found";
      case 409: return "Conflict";
      case 413: return "Payload Too Large";
      case 500: return "Internal Server Error";
      default: return "OK";
    }
  }

  uint16_t m_port;
  int m_fd = -1;
  int m_fd6 = -1;
  int m_fdFd = -1;
  std::vector<Route> m_routen;
  void (*m_notFound)() = nullptr;
  std::map<std::string, std::string> m_arg;
  std::vector<std::string> m_pfadArgs;
  HTTPUpload m_upload;
  int m_code = 200;
  std::string m_type;
  std::string m_body;
  std::vector<std::string> m_header;
  int64_t m_contentLength = -1;
  WiFiClient m_client;
};

#endif // RCT_PANEL_SIM_WEBSERVER_H