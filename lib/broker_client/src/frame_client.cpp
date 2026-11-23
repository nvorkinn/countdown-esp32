#include "frame_client.h"

#include <algorithm>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "countdown_api/api.h"
#include "json.h"
#include "line_protocol.h"
#include "retry.h"

namespace countdown {

namespace {

countdown_api::FrameLogEntry::LevelEnum levelOf(LogLevel level) {
  switch (level) {
    case LogLevel::Error:
      return countdown_api::FrameLogEntry::LevelEnum::Error;
    case LogLevel::Warning:
      return countdown_api::FrameLogEntry::LevelEnum::Warn;
    default:
      return countdown_api::FrameLogEntry::LevelEnum::Info;
  }
}

// The metrics' InfluxDB measurement and tags. The broker swaps in the device_id it authenticated
// the poll as, so the board never has to claim one.
constexpr char kMetricsSeriesKey[] = "esp32,device_id={device_id}";

// The shortest wait between polls, so a draw that outlasts the Retry-After (or a Retry-After of 0)
// doesn't poll again straight away.
constexpr uint64_t kMinPollWaitMs = 1000;

// The contract's FrameReport (countdown-api): {"metrics":"<line protocol>","logs":[{"level":...,"_msg":...,
// "uptime_ms":...},...]}. The broker says which service and device each line is from itself.
countdown_api::FrameReport report(const std::vector<Metric>& metrics, const std::deque<LogEntry>& logs) {
  countdown_api::FrameReport report;
  report.metrics = influxLine(kMetricsSeriesKey, metrics);
  report.logs.emplace();
  report.logs->reserve(logs.size());
  for (const LogEntry& entry : logs) {
    countdown_api::FrameLogEntry line;
    line.level = levelOf(entry.level);
    line._msg = entry.message;
    line.uptime_ms = static_cast<int64_t>(entry.uptimeMs);
    report.logs->push_back(std::move(line));
  }
  return report;
}

}  // namespace

FrameClient::FrameClient(DisplayRegistrar& registrar, Http& http, Display& display, Sleeper& sleeper,
                         Clock& clock, LogBuffer& log, Metrics& metrics)
    : registrar_(registrar),
      http_(http),
      display_(display),
      sleeper_(sleeper),
      clock_(clock),
      log_(log),
      metrics_(metrics) {}

void FrameClient::tick() {
  if (!registered_) {
    registrar_.registerDevice();
    registered_ = true;
  }
  std::vector<Metric> metrics = metrics_.read();
  metrics.push_back({"logs_dropped", static_cast<int64_t>(log_.dropped())});
  const std::deque<LogEntry>& logs = log_.entries();
  const uint64_t sentThrough = logs.empty() ? 0 : logs.back().seq;
  // The contract's pollFrame.
  const countdown_api::Request request = countdown_api::pollFrame(report(metrics, logs));
  const std::string url = registrar_.brokerUrl() + request.path;
  const HttpResponse response = http_.request(request.method, url, request.body, registrar_.secret(), etag_);
  // The broker's Retry-After counts from here, not from after the frame is drawn.
  const uint64_t answeredAtMs = clock_.uptimeMs();
  if (!response.sent) {
    log_.warning("Couldn't fetch the frame: " + response.error + "; sleeping for " +
                 std::to_string(kDefaultRetryS) + " seconds");
    sleeper_.sleepSeconds(kDefaultRetryS);
    return;
  }
  // Answered as a poll should be, so the broker has the logs. Anything logged from here on (the
  // lines below included) goes with the next poll.
  const int status = response.status;
  const bool accepted = status == 200 || status == 202 || status == 304 || status == 404;
  if (accepted) {
    log_.dropThrough(sentThrough);
  }
  log_.write(accepted ? LogLevel::Info : LogLevel::Warning,
             "Frame response: HTTP " + std::to_string(status) + " (" +
                 std::to_string(response.body.size()) + " bytes), Retry-After: " +
                 (response.retryAfter ? jsonString(*response.retryAfter) + " seconds" : "<missing>"));
  switch (response.status) {
    case 200:
      // Where the Pi's client would crash (and systemd restart it), this logs and carries on
      // polling: the next frame the broker sends is drawn as usual.
      if (display_.show(response.body)) {
        // Only a frame that's on the panel: one that couldn't be drawn is asked for again.
        etag_ = response.etag.value_or("");
      } else {
        log_.error("Couldn't draw the frame");
      }
      break;
    case 401:  // The broker doesn't know the secret (dropped from the pool, or unlinked)
      registered_ = false;
      return;
    default:
      break;
  }
  const uint64_t waitMs = static_cast<uint64_t>(retryAfterSeconds(response.retryAfter)) * 1000;
  const uint64_t elapsedMs = clock_.uptimeMs() - answeredAtMs;
  sleeper_.sleepMs(std::max(kMinPollWaitMs, waitMs > elapsedMs ? waitMs - elapsedMs : 0));
}

}  // namespace countdown
