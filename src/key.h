#pragma once

#include <QColor>
#include <QImage>
#include <QRect>
#include <QRectF>

// Green screen keying on plain QImages. No UI and no threads in here, so all
// of it runs the same from the window, the command line and the tests.
namespace key {

// The backdrop found in a photo.
struct Screen {
    QColor color;
    // How much of the photo is screen. Below a fifth it was not shot against
    // one, and `found` is false.
    double coverage = 0.0;
    bool found = false;
};

// All 0 to 1.
struct Settings {
    // How weak a green still counts as screen. Raise it for shadows and
    // creases on the cloth; lower it if green-ish parts of the subject go.
    double strength = 0.5;
    // How gradually an edge fades out. Soft suits hair and motion blur.
    double softness = 0.6;
    // How much of the green cast reflected onto the subject is taken out.
    double despill = 1.0;
};

// Looks across the whole photo rather than along its edges, because a cloth
// hung at home often stops short of the frame and the room shows around it.
Screen detectScreen(const QImage &image);

// The photo with the screen made transparent, as non-premultiplied ARGB32.
// Edge pixels have the screen's colour taken back out of them, so a cutout
// has no green fringe on any new background.
QImage lift(const QImage &image, const Screen &screen, const Settings &settings);

// Clears what the key left behind that is not the subject: every separate
// piece smaller than `minFraction` of the image (creases, sensor noise), and
// every sliver only a few pixels thick, such as the line left along a crop.
void removeSpecks(QImage &lifted, double minFraction = 0.00005);

// A suggested crop, as fractions of the photo: the left, right and top limits
// of the screen itself, for a backdrop that does not fill the frame. The
// bottom is left alone, since that is where the subject leaves the picture.
QRectF screenExtent(const QImage &image, const Screen &screen);

// The box around everything still visible, with a small margin.
QRect subjectBounds(const QImage &lifted, int margin = 2);

// The whole job: key, crop to `crop` (fractions of the photo), clean up and
// trim to the subject. Null when nothing is left.
QImage cutout(const QImage &image, const Screen &screen, const Settings &settings,
              const QRectF &crop = QRectF(0, 0, 1, 1));

}  // namespace key
