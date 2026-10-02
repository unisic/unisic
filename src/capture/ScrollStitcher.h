#pragma once
#include <QImage>
#include <QRect>
#include <QVector>

// Stitches consecutive scrolling viewport frames into a single seamless vertical screenshot.
// Uses fast 1D row profile projection, multi-column luminance sampling, and
// fine-matching verification. Robust against scrollbars, sticky headers, and blank margins.
class ScrollStitcher
{
public:
    enum class Direction {
        Down,
        Up,
        Auto
    };

    static constexpr int kMaxStitchedHeight = 32768;

    explicit ScrollStitcher();
    ~ScrollStitcher() = default;

    // Resets all internal state and canvas.
    void reset();

    // Preferred scroll direction (default: Down).
    void setDirection(Direction dir) { m_direction = dir; }
    Direction direction() const { return m_direction; }

    // Feeds a new frame. Returns true if new content was detected and stitched.
    bool addFrame(const QImage &frame);

    // Current full stitched image.
    QImage stitchedImage() const;

    // Scaled thumbnail for live preview.
    QImage previewThumbnail(int maxW = 140, int maxH = 300) const;

    int stitchedWidth() const { return m_actualWidth; }
    int stitchedHeight() const { return m_actualHeight; }
    int frameCount() const { return m_frameCount; }
    bool isEmpty() const { return m_frameCount == 0; }

    // Detects vertical shift between two frames in pixels.
    // > 0: content moved up / user scrolled down.
    // < 0: content moved down / user scrolled up.
    //   0: no shift or no confident match.
    // confidenceOut (optional): returns confidence in range [0.0 .. 1.0].
    static int detectVerticalShift(const QImage &prev, const QImage &curr,
                                  Direction dir = Direction::Down,
                                  double *confidenceOut = nullptr);

private:
    struct RowProfile {
        double mean = 0.0;
        quint8 samples[16]{};
    };

    static QVector<RowProfile> computeProfiles(const QImage &img, int leftMargin, int rightMargin);
    static int detectStickyHeader(const QVector<RowProfile> &prev, const QVector<RowProfile> &curr);

    Direction m_direction = Direction::Down;
    QImage m_canvas;
    QImage m_prevFrame;
    QVector<RowProfile> m_prevProfiles;
    int m_actualWidth = 0;
    int m_actualHeight = 0;
    int m_frameCount = 0;
    int m_leftMargin = 0;
    int m_rightMargin = 0;
};
