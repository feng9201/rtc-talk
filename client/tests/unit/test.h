#pragma once

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

// Unlike assert(), these checks also run in Release/NDEBUG builds.
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
  std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #condition); } while (false)

template <typename F>
int test_main(F run) {
  try {
    run();
    std::cout << "PASS\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "FAIL: non-standard exception\n";
    return 1;
  }
}
