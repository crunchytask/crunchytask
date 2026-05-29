#include "taskqueue/version.h"

#include <string>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Version string is exposed", "[version]") {
  REQUIRE(tq::kVersionString == "0.1.0");
  REQUIRE(std::string(tq::Version()) == "0.1.0");
}
