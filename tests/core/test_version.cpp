// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/version.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("version matches the CMake project version", "[core][version]") {
    REQUIRE(elara::core::version() == ELARA_EXPECTED_VERSION);
}
