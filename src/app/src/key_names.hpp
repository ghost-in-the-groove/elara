// SPDX-License-Identifier: AGPL-3.0-or-later
// The keys of a shortcut as the platform writes them, for the menus.
#pragma once

#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace elara::app {

/// `KeyNames.native("Ctrl+S")` in QML: "Ctrl+S" on Windows and Linux, the
/// Command sign and an S on macOS.
class KeyNames : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    /// `portable` as QKeySequence writes it everywhere ("Ctrl+Shift+S"), in
    /// the platform's own words.
    Q_INVOKABLE [[nodiscard]] QString native(const QString& portable) const {
        return QKeySequence(portable, QKeySequence::PortableText)
            .toString(QKeySequence::NativeText);
    }
};

} // namespace elara::app
