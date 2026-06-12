#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace microagenda::perf {

struct Sample {
  std::string name;
  std::uint64_t micros = 0;
};

class Recorder {
public:
  static Recorder& instance();
  void add(std::string name, std::uint64_t micros);
  std::vector<Sample> snapshot() const;
  void clear();

private:
  std::vector<Sample> samples_;
};

class ScopeTimer {
public:
  explicit ScopeTimer(std::string name);
  ~ScopeTimer();

private:
  std::string name_;
  std::chrono::steady_clock::time_point start_;
};

}
