#include "display_registrar.h"

#include "countdown_api/api.h"
#include "retry.h"
#include "token.h"

namespace countdown {

DisplayRegistrar::DisplayRegistrar(const std::string& brokerUrl, Store& store, Http& http,
                                   Random& random, Sleeper& sleeper, Log& log, int width, int height)
    : brokerUrl_(brokerUrl), store_(store), http_(http), random_(random), sleeper_(sleeper), log_(log), width_(width), height_(height) {
  while (!brokerUrl_.empty() && brokerUrl_.back() == '/') {
    brokerUrl_.pop_back();
  }
  secret_ = store_.get(kDeviceSecretKey).value_or("");
  if (secret_.empty()) {
    newSecret();
  }
}

void DisplayRegistrar::registerDevice() {
  while (true) {
    // The contract's registerDevice (countdown-api). Both panels take the same frame format. An e-paper screen
    // says no type.
    countdown_api::DeviceRegistration registration;
    registration.secret = secret_;
    registration.width = width_;
    registration.height = height_;
    registration.format = countdown_api::DeviceRegistration::FormatEnum::Mono1bppPacked;
    const countdown_api::Request request = countdown_api::registerDevice(registration);
    const std::string url = brokerUrl_ + request.path;
    const HttpResponse response = http_.request(request.method, url, request.body, secret_, "");
    if (!response.sent) {
      log_.warning("Couldn't register with the broker: " + response.error);
      log_.info("Sleeping for " + std::to_string(kDefaultRetryS) + " seconds");
      sleeper_.sleepSeconds(kDefaultRetryS);
      continue;
    }
    int delay = kDefaultRetryS;
    switch (response.status) {
      case 200:
      case 201:
        log_.info("Registered with the broker");
        return;
      case 429:
      case 503:
        delay = retryAfterSeconds(response.retryAfter);
        break;
      case 400:
        log_.error("The broker rejected the registration: " +
                   std::string(response.body.begin(), response.body.end()));
        break;
      default:
        log_.warning("Unexpected " + std::to_string(response.status) + " from " + url);
        break;
    }
    log_.info("Sleeping for " + std::to_string(delay) + " seconds");
    sleeper_.sleepSeconds(delay);
  }
}

void DisplayRegistrar::newSecret() {
  secret_ = tokenUrlsafe(random_);
  store_.put(kDeviceSecretKey, secret_);
}

}  // namespace countdown
