// SPDX-License-Identifier: AGPL-3.0-or-later
#include <QVariantList>
#include <QVariantMap>
#include <catch2/catch_test_macros.hpp>
#include <track_list.hpp>

#include <utility>
#include <vector>

using elara::app::TrackList;

namespace {

QVariantMap track(const QString& name, bool mute = false) {
    return {{QStringLiteral("name"), name}, {QStringLiteral("mute"), mute}};
}

QString nameAt(const TrackList& list, int row) {
    return list.data(list.index(row), TrackList::kRowRole)
        .toMap()
        .value(QStringLiteral("name"))
        .toString();
}

// What a view hears from the list: rows inserted, removed and changed, and
// resets, each as (first, last).
struct Heard {
    std::vector<std::pair<int, int>> inserted;
    std::vector<std::pair<int, int>> removed;
    std::vector<std::pair<int, int>> changed;
    int resets = 0;

    explicit Heard(TrackList& list) {
        QObject::connect(&list, &TrackList::rowsInserted,
                         [this](const QModelIndex&, int first, int last) {
                             inserted.emplace_back(first, last);
                         });
        QObject::connect(
            &list, &TrackList::rowsRemoved,
            [this](const QModelIndex&, int first, int last) { removed.emplace_back(first, last); });
        QObject::connect(&list, &TrackList::dataChanged,
                         [this](const QModelIndex& from, const QModelIndex& to) {
                             changed.emplace_back(from.row(), to.row());
                         });
        QObject::connect(&list, &TrackList::modelReset, [this] { ++resets; });
    }
};

} // namespace

TEST_CASE("Track list: the view sees each track as one row's modelData") {
    TrackList list;
    list.setTracks({track(QStringLiteral("Guitar")), track(QStringLiteral("Bass"))});
    CHECK(list.rowCount() == 2);
    CHECK(nameAt(list, 0) == QStringLiteral("Guitar"));
    CHECK(nameAt(list, 1) == QStringLiteral("Bass"));
    CHECK(list.roleNames().value(TrackList::kRowRole) == "modelData");
}

TEST_CASE("Track list: a track added at the end inserts one row and keeps the others") {
    TrackList list;
    list.setTracks({track(QStringLiteral("Guitar")), track(QStringLiteral("Bass"))});
    Heard heard(list);
    list.setTracks({track(QStringLiteral("Guitar")), track(QStringLiteral("Bass")),
                    track(QStringLiteral("Drums"))});
    CHECK(heard.inserted == std::vector<std::pair<int, int>>{{2, 2}});
    CHECK(heard.removed.empty());
    CHECK(heard.changed.empty());
    CHECK(heard.resets == 0);
    CHECK(nameAt(list, 2) == QStringLiteral("Drums"));
}

TEST_CASE("Track list: a track removed from the middle removes only its row") {
    TrackList list;
    list.setTracks({track(QStringLiteral("Guitar")), track(QStringLiteral("Bass")),
                    track(QStringLiteral("Drums"))});
    Heard heard(list);
    list.setTracks({track(QStringLiteral("Guitar")), track(QStringLiteral("Drums"))});
    CHECK(heard.removed == std::vector<std::pair<int, int>>{{1, 1}});
    CHECK(heard.inserted.empty());
    CHECK(heard.changed.empty());
    CHECK(heard.resets == 0);
    CHECK(list.rowCount() == 2);
    CHECK(nameAt(list, 1) == QStringLiteral("Drums"));
}

TEST_CASE("Track list: a mute changes one row in place") {
    TrackList list;
    list.setTracks({track(QStringLiteral("Guitar")), track(QStringLiteral("Bass"))});
    Heard heard(list);
    list.setTracks({track(QStringLiteral("Guitar")), track(QStringLiteral("Bass"), true)});
    CHECK(heard.changed == std::vector<std::pair<int, int>>{{1, 1}});
    CHECK(heard.inserted.empty());
    CHECK(heard.removed.empty());
    CHECK(heard.resets == 0);
    CHECK(list.data(list.index(1), TrackList::kRowRole)
              .toMap()
              .value(QStringLiteral("mute"))
              .toBool());
}

TEST_CASE("Track list: the same tracks again tell the view nothing") {
    TrackList list;
    list.setTracks({track(QStringLiteral("Guitar"))});
    Heard heard(list);
    list.setTracks({track(QStringLiteral("Guitar"))});
    CHECK(heard.inserted.empty());
    CHECK(heard.removed.empty());
    CHECK(heard.changed.empty());
    CHECK(heard.resets == 0);
}

TEST_CASE("Track list: another song replaces rows that differ and keeps the count right") {
    TrackList list;
    list.setTracks({track(QStringLiteral("Guitar")), track(QStringLiteral("Bass"))});
    list.setTracks({track(QStringLiteral("Piano")), track(QStringLiteral("Violin")),
                    track(QStringLiteral("Cello"))});
    REQUIRE(list.rowCount() == 3);
    CHECK(nameAt(list, 0) == QStringLiteral("Piano"));
    CHECK(nameAt(list, 1) == QStringLiteral("Violin"));
    CHECK(nameAt(list, 2) == QStringLiteral("Cello"));
    list.setTracks({});
    CHECK(list.rowCount() == 0);
}
