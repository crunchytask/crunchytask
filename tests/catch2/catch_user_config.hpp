// Catch2 pulls this in via <catch2/catch_user_config.hpp> before other headers.
// Avoid __COUNTER__ (C2y under strict C++20/clang); use __LINE__ for test names.
#define CATCH_CONFIG_NO_COUNTER
