// SPDX-License-Identifier: AGPL-3.0-or-later
// QML types whose classes live in elara_app_view, where the app tests reach
// them without a QML engine.
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>
#include <command_search.hpp>
#include <recent_scores.hpp>

namespace elara::app {

/// CommandSearch, as `CommandSearch` in Elara.App (ADR-0135).
struct CommandSearchForeign {
    Q_GADGET
    QML_FOREIGN(CommandSearch)
    QML_NAMED_ELEMENT(CommandSearch)
};

/// RecentScores, as `RecentScores` in Elara.App.
struct RecentScoresForeign {
    Q_GADGET
    QML_FOREIGN(RecentScores)
    QML_NAMED_ELEMENT(RecentScores)
};

} // namespace elara::app
