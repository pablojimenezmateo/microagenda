#include "perf/Perf.h"

namespace microagenda::perf {

Recorder& Recorder::instance() {
  static Recorder recorder;
  return recorder;
}

void Recorder::add(std::string name, std::uint64_t micros) {
  samples_.push_back({std::move(name), micros});
}

std::vector<Sample> Recorder::snapshot() const {
  return samples_;
}

void Recorder::clear() {
  samples_.clear();
}

ScopeTimer::ScopeTimer(std::string name) : name_(std::move(name)), start_(std::chrono::steady_clock::now()) {}

ScopeTimer::~ScopeTimer() {
  const auto end = std::chrono::steady_clock::now();
  const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(end - start_).count();
  Recorder::instance().add(name_, static_cast<std::uint64_t>(micros));
}

}
