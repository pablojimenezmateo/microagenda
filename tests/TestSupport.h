#pragma once

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace microagenda::tests {

using TestFn = void (*)();

struct TestCase {
  const char* name;
  TestFn fn;
};

std::vector<TestCase>& registry();

struct Registrar {
  Registrar(const char* name, TestFn fn);
};

void require(bool condition, const std::string& message);

}

#define MICROAGENDA_TEST(name) \
  static void name(); \
  static microagenda::tests::Registrar name##_registrar(#name, &name); \
  static void name()

#define MICROAGENDA_REQUIRE(condition) \
  microagenda::tests::require((condition), #condition)
