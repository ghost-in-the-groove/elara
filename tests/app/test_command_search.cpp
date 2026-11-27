// SPDX-License-Identifier: AGPL-3.0-or-later
// Finding a command of the window by what is typed (ADR-0135): which commands
// a few letters find, in which order, and which letters of a name they lit.
#include <QList>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>
#include <catch2/catch_test_macros.hpp>
#include <command_search.hpp>

#include <vector>

using elara::app::CommandSearch;
using elara::app::PaletteCommand;
using elara::app::PaletteMatch;
using elara::app::searchCommands;

namespace {

// A few commands of the window and of the caret, as the palette lists them.
std::vector<PaletteCommand> commands() {
    return {
        {.id = "file.open", .name = "Open…", .group = "File", .words = {}, .enabled = true},
        {.id = "file.save", .name = "Save", .group = "File", .words = {}, .enabled = true},
        {.id = "edit.paste", .name = "Paste over", .group = "Edit", .words = {}, .enabled = true},
        {.id = "fx.palm",
         .name = "Palm mute",
         .group = "Note effects",
         .words = "P.M.",
         .enabled = true},
        {.id = "fx.harmonic",
         .name = "Natural harmonic",
         .group = "Note effects",
         .words = "N.H.",
         .enabled = true},
        {.id = "bend.bend", .name = "Bend", .group = "Bend", .words = {}, .enabled = true},
        {.id = "bend.release",
         .name = "Bend and release",
         .group = "Bend",
         .words = {},
         .enabled = true},
        {.id = "beat.tremolo",
         .name = "Tremolo picking in eighths",
         .group = "Beat",
         .words = {},
         .enabled = true},
        {.id = "ornament.trill",
         .name = "Trill",
         .group = "Vibrato, accent and ornaments",
         .words = "tr",
         .enabled = true},
        {.id = "edit.measure",
         .name = "Insert a measure",
         .group = "Edit",
         .words = {},
         .enabled = false},
        {.id = "harmonic.es",
         .name = "Armónico artificial",
         .group = "Armónico",
         .words = "A.H.",
         .enabled = true},
        {.id = "ottava.up",
         .name = "Octave line up",
         .group = "Phrasing and lines",
         .words = "8va 15ma",
         .enabled = true},
    };
}

// The ids of what `text` finds, best first.
QStringList found(const QString& text, const QStringList& recent = {}) {
    const std::vector<PaletteCommand> list = commands();
    QStringList ids;
    for (const PaletteMatch& match : searchCommands(text, list, recent)) {
        ids.append(list.at(static_cast<std::size_t>(match.command)).id);
    }
    return ids;
}

// The letters of the first command found that what was typed matched.
QString lit(const QString& text) {
    const std::vector<PaletteCommand> list = commands();
    const QList<PaletteMatch> matches = searchCommands(text, list, {});
    REQUIRE_FALSE(matches.isEmpty());
    const QString& name = list.at(static_cast<std::size_t>(matches.front().command)).name;
    QString letters;
    for (const qsizetype at : matches.front().marks) {
        letters.append(name.at(at));
    }
    return letters;
}

} // namespace

TEST_CASE("Command search: a word's start finds a command, the name before its group",
          "[app][palette]") {
    CHECK(found("palm") == QStringList{"fx.palm"});
    CHECK(found("Sav") == QStringList{"file.save"});
    // "bend" names two commands and is the group of both; the one called
    // just that comes first.
    CHECK(found("bend").mid(0, 2) == QStringList{"bend.bend", "bend.release"});
}

TEST_CASE("Command search: every word typed must be found, in any order", "[app][palette]") {
    CHECK(found("bend rel") == QStringList{"bend.release"});
    CHECK(found("release bend") == QStringList{"bend.release"});
    CHECK(found("bend tremolo").isEmpty());
}

TEST_CASE("Command search: the initials of a name find it", "[app][palette]") {
    CHECK(found("pm").front() == "fx.palm");
    CHECK(found("nh").front() == "fx.harmonic");
    CHECK(lit("pm") == "Pm");
}

TEST_CASE("Command search: the signs the score writes find what writes them", "[app][palette]") {
    CHECK(found("P.M.").front() == "fx.palm");
    CHECK(found("8va") == QStringList{"ottava.up"});
    CHECK(found("tr").front() == "ornament.trill");
}

TEST_CASE("Command search: accents and capitals do not matter", "[app][palette]") {
    CHECK(found("armonico art") == QStringList{"harmonic.es"});
    CHECK(found("ARMÓNICO").front() == "harmonic.es");
    CHECK(lit("armonico") == "Armónico");
}

TEST_CASE("Command search: letters in order find a name, after closer matches", "[app][palette]") {
    // t-r-m in "Tremolo": no word starts with "trm".
    CHECK(found("trm").front() == "beat.tremolo");
    CHECK(lit("trm") == "Trm");
    CHECK(found("zzz").isEmpty());
}

TEST_CASE("Command search: what cannot run comes after what can", "[app][palette]") {
    // "Insert a measure" starts with what was typed, the tremolo only has
    // its letters in order; but the measure cannot be inserted: found, last.
    const QStringList ids = found("ins");
    CHECK(ids.size() > 1);
    REQUIRE(ids.contains("edit.measure"));
    CHECK(ids.back() == "edit.measure");
    CHECK(found("a").back() == "edit.measure");
}

TEST_CASE("Command search: the commands used last come first among equals", "[app][palette]") {
    // "p" starts Palm mute and Paste over alike: the one used last wins.
    CHECK(found("p", {"edit.paste"}).front() == "edit.paste");
    CHECK(found("p", {"fx.palm"}).front() == "fx.palm");
    // A better match still beats a recent one.
    CHECK(found("palm", {"edit.paste"}).front() == "fx.palm");
}

TEST_CASE("Command search: with nothing typed, the recent commands, then all in order",
          "[app][palette]") {
    const QStringList ids = found("", {"bend.release", "file.save", "gone.command"});
    REQUIRE(ids.size() == static_cast<qsizetype>(commands().size()));
    CHECK(ids.mid(0, 2) == QStringList{"bend.release", "file.save"});
    CHECK(ids.at(2) == "file.open");
    // What cannot run is listed last.
    CHECK(ids.back() == "edit.measure");

    const std::vector<PaletteCommand> list = commands();
    const QList<PaletteMatch> matches = searchCommands("  ", list, {"file.save"});
    CHECK(matches.front().recent);
    CHECK_FALSE(matches.at(1).recent);
}

TEST_CASE("Command search: the palette's object lists, marks and remembers", "[app][palette]") {
    CommandSearch search;
    search.setCommands(QVariantList{
        QVariantMap{{"id", "file.save"}, {"name", "Save <now>"}, {"group", "File"}},
        QVariantMap{{"id", "fx.palm"},
                    {"name", "Palm mute"},
                    {"group", "Note effects"},
                    {"words", "P.M."},
                    {"enabled", false}},
    });
    const QVariantList results = search.search("sav");
    REQUIRE(results.size() == 1);
    const QVariantMap first = results.front().toMap();
    CHECK(first.value("index").toInt() == 0);
    // The name as rich text, what was typed in bold, the rest escaped.
    CHECK(first.value("marked").toString() == "<b>Sav</b>e &lt;now&gt;");

    search.used("fx.palm");
    search.used("file.save");
    search.used("fx.palm");
    CHECK(search.recent() == QStringList{"fx.palm", "file.save"});
    for (int i = 0; i < 20; ++i) {
        search.used(QStringLiteral("other.%1").arg(i));
    }
    CHECK(search.recent().size() == CommandSearch::kRecentLimit);
    CHECK(search.recent().front() == "other.19");
}

TEST_CASE("Command search: the recent commands are kept only while remembering", "[app][palette]") {
    const QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString file = dir.filePath("settings.ini");
    {
        CommandSearch search;
        search.setSettingsFile(file);
        search.used("file.save"); // a check: nothing is written
        search.setRemember(true);
        CHECK(search.recent().isEmpty());
        search.used("fx.palm");
        search.used("bend.bend");
    }
    CommandSearch again;
    again.setSettingsFile(file);
    again.setRemember(true);
    CHECK(again.recent() == QStringList{"bend.bend", "fx.palm"});
}
