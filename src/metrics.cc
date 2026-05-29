#include "taskqueue/metrics.h"

#include <algorithm>
#include <sstream>
#include <vector>

namespace tq {

void MetricsCollector::IncrementCounter(const std::string& name,
                                      const std::int64_t delta) {
  std::lock_guard<std::mutex> lock(mutex_);
  counters_[name] += delta;
}

void MetricsCollector::SetGauge(const std::string& name, const std::int64_t value) {
  std::lock_guard<std::mutex> lock(mutex_);
  gauges_[name] = value;
}

void MetricsCollector::ObserveDurationMs(const std::string& name,
                                         const std::int64_t duration_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  DurationHistogram& histogram = histograms_[name];
  histogram.count += 1;
  histogram.sum_ms += duration_ms;
  histogram.max_ms = std::max(histogram.max_ms, duration_ms);
}

MetricsSnapshot MetricsCollector::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  MetricsSnapshot snapshot;
  snapshot.counters = counters_;
  snapshot.gauges = gauges_;
  snapshot.histograms = histograms_;
  return snapshot;
}

namespace {

void AppendCounter(std::ostringstream& output, const std::string& name,
                   const std::int64_t value) {
  output << name << ' ' << value << '\n';
}

void AppendGauge(std::ostringstream& output, const std::string& name,
                 const std::int64_t value) {
  output << name << ' ' << value << '\n';
}

}  // namespace

std::string FormatMetricsPlainText(const MetricsSnapshot& snapshot) {
  std::ostringstream output;

  output << "# counters\n";
  std::vector<std::string> counter_names;
  counter_names.reserve(snapshot.counters.size());
  for (const auto& entry : snapshot.counters) {
    counter_names.push_back(entry.first);
  }
  std::sort(counter_names.begin(), counter_names.end());
  for (const auto& name : counter_names) {
    AppendCounter(output, name, snapshot.counters.at(name));
  }

  output << "# gauges\n";
  std::vector<std::string> gauge_names;
  gauge_names.reserve(snapshot.gauges.size());
  for (const auto& entry : snapshot.gauges) {
    gauge_names.push_back(entry.first);
  }
  std::sort(gauge_names.begin(), gauge_names.end());
  for (const auto& name : gauge_names) {
    AppendGauge(output, name, snapshot.gauges.at(name));
  }

  output << "# histograms\n";
  std::vector<std::string> histogram_names;
  histogram_names.reserve(snapshot.histograms.size());
  for (const auto& entry : snapshot.histograms) {
    histogram_names.push_back(entry.first);
  }
  std::sort(histogram_names.begin(), histogram_names.end());
  for (const auto& name : histogram_names) {
    const DurationHistogram& histogram = snapshot.histograms.at(name);
    output << name << "_count " << histogram.count << '\n';
    output << name << "_sum_ms " << histogram.sum_ms << '\n';
    output << name << "_max_ms " << histogram.max_ms << '\n';
  }

  return output.str();
}

std::string FormatMetricsPrometheus(const MetricsSnapshot& snapshot) {
  std::ostringstream output;

  std::vector<std::string> counter_names;
  counter_names.reserve(snapshot.counters.size());
  for (const auto& entry : snapshot.counters) {
    counter_names.push_back(entry.first);
  }
  std::sort(counter_names.begin(), counter_names.end());
  for (const auto& name : counter_names) {
    output << "# TYPE " << name << " counter\n";
    AppendCounter(output, name, snapshot.counters.at(name));
  }

  std::vector<std::string> gauge_names;
  gauge_names.reserve(snapshot.gauges.size());
  for (const auto& entry : snapshot.gauges) {
    gauge_names.push_back(entry.first);
  }
  std::sort(gauge_names.begin(), gauge_names.end());
  for (const auto& name : gauge_names) {
    output << "# TYPE " << name << " gauge\n";
    AppendGauge(output, name, snapshot.gauges.at(name));
  }

  std::vector<std::string> histogram_names;
  histogram_names.reserve(snapshot.histograms.size());
  for (const auto& entry : snapshot.histograms) {
    histogram_names.push_back(entry.first);
  }
  std::sort(histogram_names.begin(), histogram_names.end());
  for (const auto& name : histogram_names) {
    const DurationHistogram& histogram = snapshot.histograms.at(name);
    output << "# TYPE " << name << " summary\n";
    output << name << "_count " << histogram.count << '\n';
    output << name << "_sum " << histogram.sum_ms << '\n';
    output << name << "_max " << histogram.max_ms << '\n';
  }

  return output.str();
}

}  // namespace tq
