// SPDX-License-Identifier: AGPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <json_writer.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

using elara::cli::jsonEscape;
using elara::cli::jsonNumber;
using elara::cli::JsonWriter;

TEST_CASE("JSON: text is escaped", "[cli][json]") {
    CHECK(jsonEscape("plain") == "plain");
    CHECK(jsonEscape("say \"hi\"\\") == R"(say \"hi\"\\)");
    CHECK(jsonEscape("a\nb\tc\rd") == R"(a\nb\tc\rd)");
    CHECK(jsonEscape(std::string("\x01\x1f", 2)) == R"(\u0001\u001f)");
    // UTF-8 passes as it is.
    CHECK(jsonEscape("Canción") == "Canción");
}

TEST_CASE("JSON: numbers have four decimals at most and no exponent", "[cli][json]") {
    CHECK(jsonNumber(3.2760000000000002) == "3.276");
    CHECK(jsonNumber(2.0) == "2");
    CHECK(jsonNumber(0.0003) == "0.0003");
    CHECK(jsonNumber(0.00004) == "0");
    CHECK(jsonNumber(-0.00001) == "0");
    CHECK(jsonNumber(-0.0) == "0");
    CHECK(jsonNumber(-1.5) == "-1.5");
    CHECK(jsonNumber(1e9) == "1000000000");
}

TEST_CASE("JSON: what JSON cannot write as a number is null", "[cli][json]") {
    CHECK(jsonNumber(std::numeric_limits<double>::quiet_NaN()) == "null");
    CHECK(jsonNumber(std::numeric_limits<double>::infinity()) == "null");
    CHECK(jsonNumber(-std::numeric_limits<double>::infinity()) == "null");
}

TEST_CASE("JSON writer: commas, keys and nesting", "[cli][json]") {
    JsonWriter json;
    json.beginObject()
        .field("file", "a \"b\".gp")
        .field("tracks", std::size_t{3})
        .field("offset", -2)
        .field("seconds", 10.40000001)
        .field("loop", false);
    json.key("names").beginArray();
    for (const char* name : {"Bass", "Piano"}) {
        json.value(name);
    }
    json.endArray();
    json.key("empty").beginArray().endArray();
    json.key("none").null();
    json.key("measures").beginArray();
    for (int measure = 1; measure <= 2; ++measure) {
        json.beginObject().field("measure", measure).endObject();
    }
    json.endArray();
    json.key("given").raw(R"({"x":1})");
    json.endObject();
    // Outside CHECK: MSVC's preprocessor mangles raw strings in a macro argument.
    const std::string expected =
        R"({"file":"a \"b\".gp","tracks":3,"offset":-2,"seconds":10.4,"loop":false,)"
        R"("names":["Bass","Piano"],"empty":[],"none":null,)"
        R"("measures":[{"measure":1},{"measure":2}],"given":{"x":1}})";
    CHECK(json.text() == expected);
}

TEST_CASE("JSON writer: values of every kind", "[cli][json]") {
    JsonWriter json;
    json.beginArray()
        .value(std::uint64_t{18446744073709551615ULL})
        .value(std::int64_t{-9007199254740993})
        .value(0.5f)
        .value(true)
        .value(std::string("s"))
        .number(std::numeric_limits<double>::quiet_NaN())
        .endArray();
    CHECK(json.text() == R"([18446744073709551615,-9007199254740993,0.5,true,"s",null])");
}

TEST_CASE("JSON writer: a lone value", "[cli][json]") {
    JsonWriter json;
    json.value(42);
    CHECK(json.text() == "42");
}
