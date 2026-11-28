#pragma once

// Registering with countdown-server (https://github.com/nvorkinn/countdown-server) as an e-paper screen: the port
// of countdown_credentials.registration's Registrar and DisplayRegistrar. The device's identity is a
// secret kept in the Store, which is also sent as the bearer token on every request.

#include <string>

#include "ports.h"

namespace countdown {

// The Store key the device secret is kept under.
constexpr char kDeviceSecretKey[] = "device_secret";

class DisplayRegistrar {
 public:
  // `width` and `height` are the panel's size in pixels, sent with every registration.
  // Loads the device secret from `store`, or makes and saves a new one. A new secret is saved
  // before it's ever sent, so retrying a registration never makes a second identity.
  DisplayRegistrar(const std::string& brokerUrl, Store& store, Http& http, Random& random,
                   Sleeper& sleeper, Log& log, int width, int height);

  // Registers, retrying until the broker takes it (a 200 or a 201). A display registers on every boot,
  // which the broker allows. Nothing the broker answers makes it a new secret: the secret is the board's
  // identity, and a new one would be a new, unpaired device.
  void registerDevice();

  const std::string& brokerUrl() const { return brokerUrl_; }
  const std::string& secret() const { return secret_; }

 private:
  void newSecret();

  std::string brokerUrl_;
  Store& store_;
  Http& http_;
  Random& random_;
  Sleeper& sleeper_;
  Log& log_;
  int width_;
  int height_;
  std::string secret_;
};

}  // namespace countdown
