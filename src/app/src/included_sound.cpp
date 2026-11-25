// SPDX-License-Identifier: AGPL-3.0-or-later
#include "included_sound.hpp"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace elara::app {

QString includedSoundFont(const QString& appDir) {
    const QStringList folders{QStringLiteral("../share/elara/soundfonts"),
                              QStringLiteral("../Resources/soundfonts")};
    for (const QString& folder : folders) {
        const QDir dir(QDir(appDir).filePath(folder));
        const QStringList fonts =
            dir.entryList({QStringLiteral("*.sf2"), QStringLiteral("*.sf3")}, QDir::Files,
                          QDir::Name | QDir::IgnoreCase);
        if (!fonts.isEmpty()) {
            return QDir::cleanPath(dir.absoluteFilePath(fonts.front()));
        }
    }
    return {};
}

QString startingSoundFont(const std::optional<QString>& saved, const QString& included) {
    if (!saved) {
        return included; // never chosen
    }
    if (saved->isEmpty()) {
        return {}; // the built-in synthesizer, chosen
    }
    return QFileInfo::exists(*saved) ? *saved : included;
}

} // namespace elara::app
