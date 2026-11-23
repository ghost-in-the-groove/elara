// SPDX-License-Identifier: AGPL-3.0-or-later
#include "score_page.hpp"

#include <QFuture>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGTexture>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

namespace elara::app {

namespace {

// How long the size must stay put before the page is painted again.
constexpr int kSettleMilliseconds = 120;
// The largest texture painted, in device pixels: past it a page is painted at
// a lower resolution and stretched (tiles would lift this limit).
constexpr double kMaxSide = 8192.0;
constexpr double kMaxPixels = 32.0 * 1024 * 1024;

std::atomic<int> paints{0};
std::atomic<int> kept{0};

} // namespace

ScorePage::ScorePage(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents);
    settle_.setSingleShot(true);
    settle_.setInterval(kSettleMilliseconds);
    connect(&settle_, &QTimer::timeout, this, &ScorePage::repaint);
}

int ScorePage::paintCount() {
    return paints.load();
}

int ScorePage::keptCount() {
    return kept.load();
}

void ScorePage::setDocument(ScoreDocument* document) {
    if (document == document_) {
        return;
    }
    if (document_ != nullptr) {
        disconnect(document_, nullptr, this, nullptr);
    }
    document_ = document;
    if (document_ != nullptr) {
        connect(document_, &ScoreDocument::layoutChanged, this, &ScorePage::repaint);
    }
    emit documentChanged();
    repaint();
}

void ScorePage::setIndex(int index) {
    if (index == index_) {
        return;
    }
    index_ = index;
    emit indexChanged();
    repaint();
}

void ScorePage::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        schedule();
    }
}

void ScorePage::itemChange(ItemChange change, const ItemChangeData& value) {
    QQuickItem::itemChange(change, value);
    if (change == ItemDevicePixelRatioHasChanged ||
        (change == ItemSceneChange && value.window != nullptr)) {
        schedule();
    }
}

// Before the first paint, paint now; afterwards wait for the size to settle,
// the old texture stretched meanwhile.
void ScorePage::schedule() {
    if (painted_) {
        settle_.start();
    } else {
        repaint();
    }
}

void ScorePage::repaint() {
    settle_.stop();
    if (document_ == nullptr || width() <= 0.0 || height() <= 0.0) {
        return;
    }
    auto page = document_->page(index_);
    if (!page || page->width <= 0.0) {
        return;
    }
    const double ratio = window() != nullptr ? window()->effectiveDevicePixelRatio() : 1.0;
    double scale = width() / page->width;
    const double side = std::max(page->width, page->bottom - page->top) * scale * ratio;
    const double pixels = page->width * (page->bottom - page->top) * scale * scale * ratio * ratio;
    scale *= std::min({1.0, kMaxSide / side, std::sqrt(kMaxPixels / pixels)});
    // The texture already shows this page at this size: an edit elsewhere in
    // the song costs a comparison here, not a paint.
    if (shown_ && scale == shownScale_ && ratio == shownRatio_ && *page == *shown_) {
        shown_ = std::move(page); // lets the previous layout go
        ++kept;
        return;
    }
    shown_ = page;
    shownScale_ = scale;
    shownRatio_ = ratio;
    const std::uint64_t ticket = ++ticket_;
    QtConcurrent::run([page = std::move(page), scale, ratio] {
        return ScoreDocument::painter().render(*page, scale, ratio);
    }).then(this, [this, ticket](const QImage& image) {
        if (ticket != ticket_) {
            return;
        }
        image_ = image;
        ++paints;
        update();
        if (!painted_) {
            painted_ = true;
            emit paintedChanged();
        }
    });
}

QSGNode* ScorePage::updatePaintNode(QSGNode* old, UpdatePaintNodeData* /*data*/) {
    auto* node = static_cast<QSGImageNode*>(old);
    if (!image_.isNull()) {
        if (node == nullptr) {
            node = window()->createImageNode();
            node->setOwnsTexture(true);
            node->setFiltering(QSGTexture::Linear);
        }
        // The node deletes the texture it replaces.
        node->setTexture(window()->createTextureFromImage(image_));
        image_ = QImage();
    }
    if (node != nullptr) {
        node->setRect(boundingRect());
    }
    return node;
}

} // namespace elara::app
