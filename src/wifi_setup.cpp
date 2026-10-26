#include "wifi_setup.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "captive_probe.h"
#include "device_name.h"
#include "display_registrar.h"
#include "portal_api.h"
#include "setup_partition.h"
#include "token.h"

// web/wifi.html, put in the firmware by board_build.embed_txtfiles in platformio.ini (NUL-terminated).
extern const char kPortalPage[] asm("_binary_web_wifi_html_start");

namespace {

using countdown::ConnectAttempt;

constexpr char kSsidKey[] = "WIFI_SSID";
constexpr char kPasswordKey[] = "WIFI_PASSWORD";
constexpr unsigned long kConnectTimeoutMs = 30 * 1000;
// How long a saved network gets before the portal opens. After a power cut the router can take a few
// minutes to come back, and until then the panel should keep its last dashboard, not switch to the
// set-up screen. A network that's gone for good (a new router) still gets the portal after this.
constexpr unsigned long kSavedNetworkPatienceMs = 5 * 60 * 1000;
constexpr int kPortalTimeoutS = 5 * 60;
// The page gets the last scan's list; one older than this is scanned again first.
constexpr unsigned long kScanMaxAgeMs = 60 * 1000;
// Once the board has joined, the access point stays up until the page has been told and the answer
// has had this long to reach the phone, or for this long at most if the page never asks.
constexpr unsigned long kHandoffGraceMs = 1500;
constexpr unsigned long kHandoffMaxMs = 15 * 1000;
constexpr uint16_t kDnsPort = 53;

const char* statusName(wl_status_t status) {
  switch (status) {
    case WL_NO_SSID_AVAIL:
      return "network not found";
    case WL_CONNECT_FAILED:
      return "connection failed (wrong password?)";
    case WL_CONNECTION_LOST:
      return "connection lost";
    case WL_DISCONNECTED:
      return "disconnected";
    case WL_IDLE_STATUS:
      return "idle";
    default:
      return "not connected";
  }
}

// Waits out the whole timeout instead of using WiFi.waitForConnectResult(), which returns on the
// first failed attempt. Straight after boot that's often "network not found", before the first scan
// has seen the router, while auto-reconnect would have joined it a few seconds later.
bool join(const std::string& ssid, const std::string& password, countdown::Log& log) {
  WiFi.begin(ssid.c_str(), password.c_str());
  const unsigned long start = millis();
  while (millis() - start < kConnectTimeoutMs) {
    if (WiFi.status() == WL_CONNECTED) {
      return true;
    }
    delay(100);
  }
  log.warning("Couldn't connect to " + ssid + ": " + statusName(WiFi.status()));
  return false;
}

// ePaper-Dashboard- and the last two bytes of the MAC, so two boards' portals can be told apart.
// provision/provision.py builds the same name for the set-up screen.
String portalName() {
  String mac = WiFi.macAddress();  // AA:BB:CC:DD:EE:FF
  mac.replace(":", "");
  return "ePaper-Dashboard-" + mac.substring(8);
}

std::string sha256Hex(const std::string& input) {
  unsigned char digest[32];
  const auto* data = reinterpret_cast<const unsigned char*>(input.data());
#if MBEDTLS_VERSION_MAJOR >= 3
  mbedtls_sha256(data, input.size(), digest, 0);  // Arduino-ESP32 3.x: the _ret variants are gone.
#else
  mbedtls_sha256_ret(data, input.size(), digest, 0);
#endif
  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  result.reserve(sizeof(digest) * 2);
  for (const unsigned char byte : digest) {
    result += hex[byte >> 4];
    result += hex[byte & 0x0f];
  }
  return result;
}

// Sends requests for foreign hosts (and the access point's IP) to the page's mDNS name.
void redirectToPortal(WebServer& server, countdown::Log& log) {
  const String url = String("http://") + kDeviceName + ".local/wifi";
  log.info(std::string("Redirecting portal request to ") + url.c_str());
  server.sendHeader("Location", url, true);
  server.send(302, "text/plain", "");
}

bool isMdnsHost(String host) {
  if (host.endsWith(":80")) {
    host.remove(host.length() - 3);
  }
  return host.equalsIgnoreCase(String(kDeviceName) + ".local");
}

// Adapts RequestHandler's signatures, which differ between the cores, to matches() and respond().
// Arduino-ESP32 2.x: canHandle(HTTPMethod, String) and handle(WebServer&, HTTPMethod, String).
// 3.x: WebServer asks canHandle(WebServer&, HTTPMethod, const String&) (the old two-argument
// overload is no longer called, so overriding only it would silently never match) and then calls
// handle(WebServer&, HTTPMethod, const String&).
class PortalHandler : public RequestHandler {
 public:
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  using RequestHandler::canHandle;
  bool canHandle(WebServer&, HTTPMethod, const String& uri) override { return matches(uri); }
  bool handle(WebServer& server, HTTPMethod, const String&) override { return respond(server); }
#else
  bool canHandle(HTTPMethod, String uri) override { return matches(uri); }
  bool handle(WebServer& server, HTTPMethod, String) override { return respond(server); }
#endif

 protected:
  virtual bool matches(const String& uri) = 0;
  virtual bool respond(WebServer& server) = 0;
};

class RedirectToPortal : public PortalHandler {
 public:
  RedirectToPortal(WebServer& server, countdown::Log& log) : server_(server), log_(log) {}

 protected:
  bool matches(const String&) override { return !isMdnsHost(server_.hostHeader()); }
  bool respond(WebServer& server) override {
    redirectToPortal(server, log_);
    return true;
  }

 private:
  WebServer& server_;
  countdown::Log& log_;
};

// Answers iOS's "is there internet?" checks with the page it expects, so it doesn't open its
// pop-up sheet (which closes when the access point goes away) and the user does set-up in Safari.
class AppleProbeSuccess : public PortalHandler {
 public:
  explicit AppleProbeSuccess(WebServer& server) : server_(server) {}

 protected:
  bool matches(const String&) override { return countdown::isAppleProbeHost(server_.hostHeader().c_str()); }
  bool respond(WebServer& server) override {
    server.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    return true;
  }

 private:
  WebServer& server_;
};

// The set-up portal once the access point is up: the DNS server that sends every name to it, the
// page (web/wifi.html, which documents the calls below) and the calls it makes. loop() runs it;
// the portal is done when done() says so, with the network in ssid() and password().
class Portal {
 public:
  Portal(countdown::Log& log, std::string pairingUrl)
      : attempt_(kConnectTimeoutMs), log_(log), pairingUrl_(std::move(pairingUrl)) {}

  void begin() {
    // Asked in this order, before the routes.
    server_.addHandler(new AppleProbeSuccess(server_));
    server_.addHandler(new RedirectToPortal(server_, log_));
    server_.on("/wifi", HTTP_GET, [this] { sendPage(); });
    server_.on("/scan", HTTP_GET, [this] { sendNetworks(); });
    server_.on("/scan", HTTP_POST, [this] { rescan(); });
    server_.on("/connect", HTTP_POST, [this] { connect(); });
    server_.on("/status", HTTP_GET, [this] { sendStatus(); });
    // Anything else, including the old WiFiManager pages, goes to the set-up page.
    server_.onNotFound([this] { redirectToPortal(server_, log_); });
    server_.begin();
    dns_.setErrorReplyCode(DNSReplyCode::NoError);
    dns_.start(kDnsPort, "*", WiFi.softAPIP());
    startScan();  // so the list is ready by the time the page asks
  }

  void loop() {
    dns_.processNextRequest();
    server_.handleClient();
    finishScan();
    const ConnectAttempt::State before = attempt_.state();
    const ConnectAttempt::State now = attempt_.update(millis(), WiFi.status() == WL_CONNECTED);
    if (now == before) {
      return;
    }
    if (now == ConnectAttempt::State::Connected) {
      connectedAt_ = millis();
      log_.info(std::string("Connected, IP: ") + WiFi.localIP().toString().c_str());
    } else if (now == ConnectAttempt::State::Failed) {
      failureReason_ = statusName(WiFi.status());
      log_.warning("Couldn't connect to " + ssid_ + ": " + failureReason_);
      WiFi.disconnect();  // or the driver keeps trying, and the next scan and attempt wait on it
    }
  }

  bool connected() const { return attempt_.state() == ConnectAttempt::State::Connected; }

  bool done() const {
    if (!connected()) {
      return false;
    }
    const unsigned long now = millis();
    return (reported_ && now - reportedAt_ >= kHandoffGraceMs) || now - connectedAt_ >= kHandoffMaxMs;
  }

  void end() {
    server_.stop();
    dns_.stop();
  }

  const std::string& ssid() const { return ssid_; }
  const std::string& password() const { return password_; }

 private:
  void sendJson(int code, const std::string& json) {
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(code, "application/json", json.c_str());
  }

  void sendError(int code, const std::string& message) {
    sendJson(code, "{\"error\":\"" + countdown::jsonEscape(message) + "\"}");
  }

  void sendPage() {
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(200, "text/html; charset=utf-8", kPortalPage);
  }

  void startScan() {
    WiFi.scanDelete();
    if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED) {
      log_.warning("Couldn't start a Wi-Fi scan");
      return;
    }
    scanning_ = true;
  }

  void finishScan() {
    if (!scanning_) {
      return;
    }
    const int16_t count = WiFi.scanComplete();
    if (count == WIFI_SCAN_RUNNING) {
      return;
    }
    networks_.clear();
    for (int16_t i = 0; i < count; ++i) {
      networks_.push_back({WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
    }
    WiFi.scanDelete();
    scanning_ = false;
    scannedAt_ = millis();
    scanned_ = true;
    if (count < 0) {
      log_.warning("The Wi-Fi scan failed");
    } else {
      log_.info("The Wi-Fi scan found " + std::to_string(count) + " networks");
    }
  }

  void sendNetworks() {
    const bool stale = !scanned_ || millis() - scannedAt_ >= kScanMaxAgeMs;
    if (stale && !scanning_ && attempt_.state() == ConnectAttempt::State::Idle) {
      startScan();
    }
    sendJson(200, std::string("{\"scanning\":") + (scanning_ ? "true" : "false") +
                      ",\"networks\":" + countdown::networksJson(networks_) + "}");
  }

  void rescan() {
    if (attempt_.state() != ConnectAttempt::State::Idle && attempt_.state() != ConnectAttempt::State::Failed) {
      sendError(409, "Already connecting");
      return;
    }
    if (!scanning_) {
      startScan();
    }
    sendJson(200, "{\"scanning\":true}");
  }

  void connect() {
    const std::string ssid = server_.arg("ssid").c_str();
    const std::string password = server_.arg("password").c_str();
    const std::string problem = countdown::credentialsProblem(ssid, password);
    if (!problem.empty()) {
      sendError(400, problem);
      return;
    }
    if (scanning_) {
      sendError(409, "Still looking for networks");  // the driver can't join during a scan
      return;
    }
    if (attempt_.state() == ConnectAttempt::State::Connecting) {
      sendError(409, "Already connecting");
      return;
    }
    if (attempt_.state() == ConnectAttempt::State::Connected) {
      sendError(409, "Already connected");
      return;
    }
    attempt_.reset();
    ssid_ = ssid;
    password_ = password;
    log_.info("Joining " + ssid + " from the set-up page");
    WiFi.begin(ssid.c_str(), password.c_str());
    attempt_.start(millis());
    sendJson(200, "{\"state\":\"connecting\"}");
  }

  void sendStatus() {
    const ConnectAttempt::State state = attempt_.state();
    std::string json = std::string("{\"state\":\"") + countdown::stateName(state) + "\",\"ssid\":\"" +
                       countdown::jsonEscape(ssid_) + "\"";
    if (state == ConnectAttempt::State::Failed) {
      json += ",\"reason\":\"" + countdown::jsonEscape(failureReason_) + "\"";
    } else if (state == ConnectAttempt::State::Connected) {
      json += ",\"next\":\"" + countdown::jsonEscape(pairingUrl_) + "\"";
      if (!reported_) {
        reported_ = true;
        reportedAt_ = millis();
      }
    }
    sendJson(200, json + "}");
  }

  WebServer server_{80};
  DNSServer dns_;
  ConnectAttempt attempt_;
  countdown::Log& log_;
  std::string pairingUrl_;
  std::vector<countdown::ScannedNetwork> networks_;
  bool scanning_ = false;
  bool scanned_ = false;
  unsigned long scannedAt_ = 0;
  std::string ssid_;
  std::string password_;
  std::string failureReason_;
  unsigned long connectedAt_ = 0;
  bool reported_ = false;
  unsigned long reportedAt_ = 0;
};

void runPortal(countdown::Store& store, countdown::Random& random, countdown::Log& log,
               const std::string& afterSetupUrl, countdown::Display& display) {
  const String name = portalName();
  // The set-up screen shows the network's password, so the network only gets one once the screen is
  // up: a board that was never provisioned, or whose panel didn't draw it, opens it as before.
  std::string password;
  if (std::optional<SetupScreen> screen = readSetupScreen(log)) {
    if (display.show(screen->frame)) {
      password = std::move(screen->password);
    } else {
      log.warning("Couldn't show the set-up screen, so the set-up network is open");
    }
  }  // The frame is freed here, before the portal runs.
  log.info(std::string("Opening the Wi-Fi set-up portal: join ") + name.c_str() +
           (password.empty() ? " (open)" : " (password on the set-up screen)") + " and browse to http://" +
           kDeviceName + ".local/wifi (or 192.168.4.1)");
  // The station side stays on: it scans for the page, and joins the network it picks.
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(name.c_str(), password.empty() ? nullptr : password.c_str())) {
    log.error("Couldn't open the set-up network; restarting");
    ESP.restart();
  }
  if (!MDNS.begin(kDeviceName)) {
    log.warning("Couldn't start mDNS; the set-up redirect to " + std::string(kDeviceName) + ".local may not resolve");
  }
  std::string secret = store.get(countdown::kDeviceSecretKey).value_or("");
  if (secret.empty()) {
    secret = countdown::tokenUrlsafe(random);
    store.put(countdown::kDeviceSecretKey, secret);
  }
  std::string pairingUrl = afterSetupUrl;
  while (!pairingUrl.empty() && pairingUrl.back() == '/') {
    pairingUrl.pop_back();
  }
  // In the query, not the fragment: a fragment never reaches the broker, so it is lost when the login
  // proxy sends a signed-out user to the sign-in or sign-up and back (the broker accepts either form).
  pairingUrl += "/pair?t=" + countdown::pairingToken(secret, sha256Hex);
  Portal portal(log, pairingUrl);
  portal.begin();
  const unsigned long opened = millis();
  while (!portal.done()) {
    if (!portal.connected() && millis() - opened >= kPortalTimeoutS * 1000UL) {
      log.error("Nobody set up Wi-Fi in time; restarting");
      ESP.restart();
    }
    portal.loop();
    delay(10);
  }
  portal.end();
  MDNS.end();
  WiFi.mode(WIFI_STA);  // the portal's access point off for good
  store.put(kSsidKey, portal.ssid());
  store.put(kPasswordKey, portal.password());
  log.info("Saved Wi-Fi credentials for " + portal.ssid());
}

}  // namespace

void connectWifi(countdown::Store& store, countdown::Random& random, countdown::Log& log,
                 const std::string& afterSetupUrl, countdown::Display& display) {
  WiFi.setHostname(kDeviceName);  // before mode(), or the 2.x core ignores it
  WiFi.mode(WIFI_STA);
  // The credentials live in our NVS keys, not the Wi-Fi driver's own copy.
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);

  const auto ssid = store.get(kSsidKey);
  const auto password = store.get(kPasswordKey);
  if (ssid && password) {
    log.info("Connecting to " + *ssid);
    const unsigned long start = millis();
    do {
      if (join(*ssid, *password, log)) {
        log.info(std::string("Connected, IP: ") + WiFi.localIP().toString().c_str());
        return;
      }
    } while (millis() - start < kSavedNetworkPatienceMs);
  } else {
    log.info("No Wi-Fi credentials saved");
  }
  runPortal(store, random, log, afterSetupUrl, display);
}
