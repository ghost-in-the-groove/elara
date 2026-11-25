// SPDX-License-Identifier: AGPL-3.0-or-later
// QML types whose classes live in elara_app_view, where the app tests reach
// them without a QML engine.
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>
#include <recent_scores.hpp>

namespace elara::app {

/// RecentScores, as `RecentScores` in Elara.App.
struct RecentScoresForeign {
    Q_GADGET
    QML_FOREIGN(RecentScores)
    QML_NAMED_ELEMENT(RecentScores)
};

} // namespace elara::app
