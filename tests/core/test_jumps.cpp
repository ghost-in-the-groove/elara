// SPDX-License-Identifier: AGPL-3.0-or-later
// Segno, coda, fine and the jumps to them in the play order (ADR-0049).
#include <elara/core/song.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

using elara::core::JumpThen;
using elara::core::JumpTo;
using elara::core::Song;

namespace {

using Order = std::vector<std::size_t>;

Song measures(std::size_t count) {
    Song song;
    song.appendMeasures(count);
    return song;
}

void jump(Song& song, std::size_t measure, JumpTo to, JumpThen then = JumpThen::End) {
    song.measureHeader(measure).jump = to;
    song.measureHeader(measure).jumpThen = then;
}

constexpr std::uint8_t ending(int n) {
    return static_cast<std::uint8_t>(1U << (n - 1));
}

} // namespace

TEST_CASE("Da capo plays the song again from the start", "[core][timeline][jumps]") {
    Song song = measures(4);
    jump(song, 2, JumpTo::Start);
    CHECK(song.playOrder() == Order{0, 1, 2, 0, 1, 2, 3});
}

TEST_CASE("A jump is taken once", "[core][timeline][jumps]") {
    Song song = measures(3);
    jump(song, 1, JumpTo::Start);
    CHECK(song.playOrder() == Order{0, 1, 0, 1, 2});
}

TEST_CASE("Da capo al fine stops at the fine only after the jump", "[core][timeline][jumps]") {
    Song song = measures(4);
    song.measureHeader(1).fine = true;
    jump(song, 3, JumpTo::Start, JumpThen::Fine);
    CHECK(song.playOrder() == Order{0, 1, 2, 3, 0, 1});
}

TEST_CASE("Dal segno al coda leaves at the to coda for the coda only after the jump",
          "[core][timeline][jumps]") {
    // 0 | 1 segno | 2 to coda | 3 D.S. al Coda | 4 coda | 5
    Song song = measures(6);
    song.measureHeader(1).segno = true;
    song.measureHeader(2).toCoda = true;
    jump(song, 3, JumpTo::Segno, JumpThen::Coda);
    song.measureHeader(4).coda = true;
    CHECK(song.playOrder() == Order{0, 1, 2, 3, 1, 2, 4, 5});
}

TEST_CASE("Dal segno goes back to the nearest segno before it", "[core][timeline][jumps]") {
    Song song = measures(5);
    song.measureHeader(0).segno = true;
    song.measureHeader(2).segno = true;
    jump(song, 3, JumpTo::Segno);
    CHECK(song.playOrder() == Order{0, 1, 2, 3, 2, 3, 4});
}

TEST_CASE("The double signs pair with their own jumps", "[core][timeline][jumps]") {
    // 0 | 1 segno | 2 segno segno | 3 to coda, to double coda
    // | 4 D.S.S. al Double Coda | 5 coda | 6 double coda
    Song song = measures(7);
    song.measureHeader(1).segno = true;
    song.measureHeader(2).segnoSegno = true;
    song.measureHeader(3).toCoda = true;
    song.measureHeader(3).toDoubleCoda = true;
    jump(song, 4, JumpTo::SegnoSegno, JumpThen::DoubleCoda);
    song.measureHeader(5).coda = true;
    song.measureHeader(6).doubleCoda = true;
    CHECK(song.playOrder() == Order{0, 1, 2, 3, 4, 2, 3, 6});
}

TEST_CASE("A jump without its sign is not taken", "[core][timeline][jumps]") {
    Song song = measures(4);
    jump(song, 2, JumpTo::Segno);
    CHECK(song.playOrder() == Order{0, 1, 2, 3});

    // al Coda without a coda sign: the music plays on past the to coda.
    song.measureHeader(0).segno = true;
    song.measureHeader(1).toCoda = true;
    jump(song, 2, JumpTo::Segno, JumpThen::Coda);
    CHECK(song.playOrder() == Order{0, 1, 2, 0, 1, 2, 3});
}

TEST_CASE("After a jump repeats are not taken and only the last ending is played",
          "[core][timeline][jumps]") {
    // |: 0 |1. 1 :|2. 2 | 3 D.C. | 4
    Song song = measures(5);
    song.measureHeader(0).repeatStart = true;
    song.measureHeader(1).alternateEndings = ending(1);
    song.measureHeader(1).repeatTimes = 2;
    song.measureHeader(2).alternateEndings = ending(2);
    jump(song, 3, JumpTo::Start);
    CHECK(song.playOrder() == Order{0, 1, 0, 2, 3, 0, 2, 3, 4});
}

TEST_CASE("A jump at the end of a repeat waits for its last time through",
          "[core][timeline][jumps]") {
    // 0 |: 1 | 2 D.C. al Fine :| 3 fine
    Song song = measures(4);
    song.measureHeader(1).repeatStart = true;
    song.measureHeader(2).repeatTimes = 2;
    jump(song, 2, JumpTo::Start, JumpThen::Fine);
    song.measureHeader(3).fine = true;
    CHECK(song.playOrder() == Order{0, 1, 2, 1, 2, 0, 1, 2, 3});
}
