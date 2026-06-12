#pragma once

#include <filesystem>

namespace microagenda::app {

struct ApplicationOptions {
  std::filesystem::path agendaRoot;
  bool headless = false;
};

int run(const ApplicationOptions& options);
ApplicationOptions parseArgs(int argc, char** argv);

}
