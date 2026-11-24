// SPDX-License-Identifier: AGPL-3.0-or-later
#include "track_list.hpp"

#include <algorithm>

namespace elara::app {

int TrackList::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant TrackList::data(const QModelIndex& index, int role) const {
    if (role != kRowRole || !index.isValid() || index.row() >= rows_.size()) {
        return {};
    }
    return rows_[index.row()];
}

QHash<int, QByteArray> TrackList::roleNames() const {
    return {{kRowRole, QByteArrayLiteral("modelData")}};
}

void TrackList::setTracks(const QVariantList& tracks) {
    const auto before = static_cast<int>(rows_.size());
    const auto after = static_cast<int>(tracks.size());
    int head = 0;
    while (head < before && head < after && rows_[head] == tracks[head]) {
        ++head;
    }
    int tail = 0;
    while (tail < before - head && tail < after - head &&
           rows_[before - 1 - tail] == tracks[after - 1 - tail]) {
        ++tail;
    }
    const int was = before - head - tail; // rows between them before
    const int now = after - head - tail;  // and now
    const int changed = std::min(was, now);
    for (int i = head; i < head + changed; ++i) {
        rows_[i] = tracks[i];
    }
    if (changed > 0) {
        emit dataChanged(index(head), index(head + changed - 1), {kRowRole});
    }
    const int from = head + changed;
    if (was > now) {
        beginRemoveRows({}, from, head + was - 1);
        rows_.remove(from, was - now);
        endRemoveRows();
    } else if (now > was) {
        beginInsertRows({}, from, head + now - 1);
        for (int i = from; i < head + now; ++i) {
            rows_.insert(i, tracks[i]);
        }
        endInsertRows();
    }
}

} // namespace elara::app
