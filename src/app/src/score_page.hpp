// SPDX-License-Identifier: AGPL-3.0-or-later
// One page of a score in the scene graph (ADR-0015).
#pragma once

#include "score_document.hpp"
#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <cstdint>

namespace elara::app {

/// Shows page `index` of `document` as a texture. The page is painted on a
/// worker thread at the item's size in device pixels; while the item is
/// resized (zooming) the last texture is stretched and painted again once the
/// size settles, so zooming stays smooth.
class ScorePage : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(
        elara::app::ScoreDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(int index READ index WRITE setIndex NOTIFY indexChanged)
    Q_PROPERTY(bool painted READ painted NOTIFY paintedChanged)

public:
    explicit ScorePage(QQuickItem* parent = nullptr);

    [[nodiscard]] ScoreDocument* document() const { return document_; }
    void setDocument(ScoreDocument* document);
    [[nodiscard]] int index() const { return index_; }
    void setIndex(int index);
    /// Whether the page has been painted at least once.
    [[nodiscard]] bool painted() const { return painted_; }

    /// Pages painted since the program started (for `elara --smoke-test`).
    [[nodiscard]] static int paintCount();

signals:
    void documentChanged();
    void indexChanged();
    void paintedChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;

private:
    void schedule();
    void repaint();

    QPointer<ScoreDocument> document_;
    int index_ = -1;
    QTimer settle_;
    std::uint64_t ticket_ = 0;
    QImage image_; ///< painted, waiting to become a texture
    bool painted_ = false;
};

} // namespace elara::app
