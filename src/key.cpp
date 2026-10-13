#include "key.h"

#include <QVector>

#include <algorithm>
#include <cmath>
#include <vector>

namespace key {

namespace {

// How far green stands above the other two channels. Positive on a green
// screen, zero or less on skin, hair, and almost any clothing.
inline int dominance(QRgb pixel) {
    return qGreen(pixel) - qMax(qRed(pixel), qBlue(pixel));
}

inline bool isScreen(QRgb pixel) {
    return qGreen(pixel) >= 60 && dominance(pixel) >= 30;
}

inline double unit(double value) {
    return qBound(0.0, value, 1.0);
}

inline int channel(double value) {
    return qBound(0, int(std::lround(value)), 255);
}

// Alpha below this is too faint to count as part of the subject.
const int kVisible = 24;

}  // namespace

Screen detectScreen(const QImage &source) {
    Screen screen;
    if (source.isNull())
        return screen;

    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    const int w = image.width();
    const int h = image.height();
    const int step = qMax(1, qMax(w, h) / 300);

    int histogram[3][256] = {};
    int total = 0;
    int matched = 0;
    for (int y = 0; y < h; y += step) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < w; x += step) {
            const QRgb pixel = line[x];
            ++total;
            if (!isScreen(pixel))
                continue;
            ++matched;
            ++histogram[0][qRed(pixel)];
            ++histogram[1][qGreen(pixel)];
            ++histogram[2][qBlue(pixel)];
        }
    }

    if (total == 0 || matched == 0)
        return screen;

    // The median, not the mean: a lamp's hot spot or a dark corner should not
    // drag the colour away from what most of the cloth looks like.
    int median[3] = {};
    for (int c = 0; c < 3; ++c) {
        int seen = 0;
        for (int value = 0; value < 256; ++value) {
            seen += histogram[c][value];
            if (seen * 2 >= matched) {
                median[c] = value;
                break;
            }
        }
    }

    screen.color = QColor(median[0], median[1], median[2]);
    screen.coverage = double(matched) / total;
    screen.found = screen.coverage >= 0.2;
    return screen;
}

QImage lift(const QImage &source, const Screen &screen, const Settings &settings) {
    QImage out = source.convertToFormat(QImage::Format_ARGB32);
    if (out.isNull() || !screen.found)
        return out;

    const int sr = screen.color.red();
    const int sg = screen.color.green();
    const int sb = screen.color.blue();
    const double screenDominance = qMax(1, sg - qMax(sr, sb));

    // Each pixel's green is measured against the screen's own: 1 is pure
    // screen, 0 has none. From `gone` up a pixel vanishes; below `kept` it is
    // untouched; between the two it fades, which is what makes a soft edge.
    const double gone = 0.75 - 0.6 * unit(settings.strength);
    const double kept = gone * (1.0 - unit(settings.softness));
    const double fade = qMax(1e-6, gone - kept);
    const double despill = unit(settings.despill);

    for (int y = 0; y < out.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            const QRgb pixel = line[x];
            const int excess = dominance(pixel);
            if (excess <= 0)
                continue;

            const double alpha = 1.0 - unit((excess / screenDominance - kept) / fade);
            if (alpha <= 0.0) {
                line[x] = 0;
                continue;
            }

            double r = qRed(pixel);
            double g = qGreen(pixel);
            double b = qBlue(pixel);
            // An edge pixel is part subject, part screen. Taking the screen's
            // share back out leaves the subject's own colour.
            if (alpha < 1.0) {
                r = (r - (1.0 - alpha) * sr) / alpha;
                g = (g - (1.0 - alpha) * sg) / alpha;
                b = (b - (1.0 - alpha) * sb) / alpha;
            }
            // What green is left above the other channels is light bounced
            // off the cloth, not the subject.
            const double limit = qMax(r, b);
            if (g > limit)
                g -= despill * (g - limit);

            line[x] = qRgba(channel(r), channel(g), channel(b), channel(alpha * qAlpha(pixel)));
        }
    }
    return out;
}

void removeSpecks(QImage &lifted, double minFraction) {
    if (lifted.isNull() || lifted.format() != QImage::Format_ARGB32)
        return;

    const int w = lifted.width();
    const int h = lifted.height();
    const int smallest = qMax(16, int(double(w) * h * minFraction));
    // A piece this thin is a sliver, however long: the line a crop leaves
    // where it runs along the edge of the cloth, say.
    const int thinnest = qMax(3, qMin(w, h) * 3 / 1000);
    QVector<uchar> seen(w * h, 0);
    std::vector<int> queue;

    const auto visible = [&](int index) {
        const QRgb *line = reinterpret_cast<const QRgb *>(lifted.constScanLine(index / w));
        return qAlpha(line[index % w]) >= kVisible;
    };

    // Walks the piece a pixel belongs to. Measuring marks each pixel as seen;
    // erasing clears each pixel instead, which stops it being walked twice.
    struct Piece {
        int size = 0;
        int left, right, top, bottom;
    };
    const auto walk = [&](int start, bool erase) {
        Piece piece;
        piece.left = piece.right = start % w;
        piece.top = piece.bottom = start / w;
        const auto visit = [&](int index) {
            if (erase)
                reinterpret_cast<QRgb *>(lifted.scanLine(index / w))[index % w] = 0;
            else
                seen[index] = 1;
            queue.push_back(index);
        };
        queue.clear();
        visit(start);
        while (!queue.empty()) {
            const int index = queue.back();
            queue.pop_back();
            const int x = index % w;
            const int y = index / w;
            ++piece.size;
            piece.left = qMin(piece.left, x);
            piece.right = qMax(piece.right, x);
            piece.top = qMin(piece.top, y);
            piece.bottom = qMax(piece.bottom, y);
            const int next[4] = {x > 0 ? index - 1 : -1, x < w - 1 ? index + 1 : -1,
                                 y > 0 ? index - w : -1, y < h - 1 ? index + w : -1};
            for (const int neighbour : next)
                if (neighbour >= 0 && visible(neighbour) && (erase || !seen[neighbour]))
                    visit(neighbour);
        }
        return piece;
    };

    for (int start = 0; start < w * h; ++start) {
        if (seen[start] || !visible(start))
            continue;
        const Piece piece = walk(start, false);
        const int thickness = qMin(piece.right - piece.left, piece.bottom - piece.top) + 1;
        if (piece.size < smallest || thickness <= thinnest)
            walk(start, true);
    }
}

QRectF screenExtent(const QImage &source, const Screen &screen) {
    const QRectF whole(0, 0, 1, 1);
    if (source.isNull() || !screen.found)
        return whole;

    // Plenty of detail to find the edge of a backdrop, and quick on any photo.
    const QImage image = source.scaled(QSize(320, 320), Qt::KeepAspectRatio, Qt::FastTransformation)
                             .convertToFormat(QImage::Format_ARGB32);
    const int w = image.width();
    const int h = image.height();
    QVector<int> columns(w, 0);
    QVector<int> rows(h, 0);
    for (int y = 0; y < h; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            if (isScreen(line[x])) {
                ++columns[x];
                ++rows[y];
            }
        }
    }

    // A row or column belongs to the backdrop once a fair part of it is green.
    const auto first = [](const QVector<int> &counts, int needed) {
        for (int i = 0; i < counts.size(); ++i)
            if (counts[i] >= needed)
                return i;
        return 0;
    };
    const auto last = [](const QVector<int> &counts, int needed) {
        for (int i = counts.size() - 1; i >= 0; --i)
            if (counts[i] >= needed)
                return i + 1;
        return int(counts.size());
    };
    int left = first(columns, qMax(1, h * 15 / 100));
    int right = last(columns, qMax(1, h * 15 / 100));
    int top = first(rows, qMax(1, w * 15 / 100));
    // Where the cloth ends inside the frame its edge is a blur of cloth and
    // room, too mixed to key. The crop steps just inside it.
    const int inset = 2;
    if (left > 0)
        left += inset;
    if (right < w)
        right -= inset;
    if (top > 0)
        top += inset;
    if (right <= left || top >= h)
        return whole;
    return QRectF(double(left) / w, double(top) / h, double(right - left) / w, double(h - top) / h);
}

QRect subjectBounds(const QImage &lifted, int margin) {
    int left = lifted.width();
    int top = lifted.height();
    int right = -1;
    int bottom = -1;
    for (int y = 0; y < lifted.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(lifted.constScanLine(y));
        for (int x = 0; x < lifted.width(); ++x) {
            if (qAlpha(line[x]) < kVisible)
                continue;
            left = qMin(left, x);
            right = qMax(right, x);
            top = qMin(top, y);
            bottom = qMax(bottom, y);
        }
    }
    if (right < 0)
        return QRect();
    return QRect(QPoint(left, top), QPoint(right, bottom))
        .adjusted(-margin, -margin, margin, margin)
        .intersected(lifted.rect());
}

QImage cutout(const QImage &image, const Screen &screen, const Settings &settings, const QRectF &crop) {
    if (image.isNull() || !screen.found)
        return QImage();

    const QRect area = QRectF(crop.x() * image.width(), crop.y() * image.height(),
                              crop.width() * image.width(), crop.height() * image.height())
                           .toRect()
                           .intersected(image.rect());
    if (area.isEmpty())
        return QImage();

    // Cropped before keying, so nothing outside the crop is ever worked on.
    QImage lifted = lift(image.copy(area), screen, settings);
    removeSpecks(lifted);
    const QRect bounds = subjectBounds(lifted);
    return bounds.isEmpty() ? QImage() : lifted.copy(bounds);
}

}  // namespace key
