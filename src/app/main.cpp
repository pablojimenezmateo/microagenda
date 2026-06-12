#include "app/Application.h"

int main(int argc, char** argv) {
  return microagenda::app::run(microagenda::app::parseArgs(argc, argv));
}
