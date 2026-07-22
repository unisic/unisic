#pragma once
#include <QPoint>
#include <QRect>
#include <QSize>
#include <climits>
#include <cmath>

// Pure geometry check shared by GifRecorder's wrong-monitor guard (and unit
// tests): can the portal's ScreenCast stream plausibly be the requested
// monitor? A restore token silently replays whatever was picked when it was
// created — including a "whole workspace" share, which arrives as one stream
// spanning every monitor. The guard has to tell that apart from the ONE
// legitimate size mismatch: a same-monitor stream delivered at a different
// scale (GNOME fractional scaling streams the logical size).
namespace StreamGeometry {

// pos          portal-reported stream position in logical workspace
//              coordinates, or (INT_MIN, INT_MIN) when not reported.
// portalSize   portal-reported stream pixel size; may be empty/invalid.
// screenGeom   the target screen's logical geometry.
// dpr          the target screen's device pixel ratio.
// unionLogical the logical size of the whole workspace (virtualGeometry).
// screenCount  total screens — the union check only means anything with >1
//              (on a single monitor the union IS the monitor, and a logical-
//              size stream there is fractional scaling, not a workspace share).
inline bool streamMatchesScreen(const QPoint &pos, const QSize &portalSize,
                                const QRect &screenGeom, qreal dpr,
                                const QSize &unionLogical, int screenCount)
{
    if (pos.x() != INT_MIN && pos != screenGeom.topLeft())
        return false;
    if (portalSize.width() <= 0 || portalSize.height() <= 0)
        return true; // size not reported — the position check is all we have
    const QSize expected(qRound(screenGeom.width() * dpr),
                         qRound(screenGeom.height() * dpr));
    if (expected.isEmpty() || portalSize == expected)
        return true;
    // A rescaled same-monitor stream keeps its aspect: non-uniform scale means
    // a different source (e.g. two side-by-side monitors doubling the width).
    const qreal sx = qreal(portalSize.width()) / expected.width();
    const qreal sy = qreal(portalSize.height()) / expected.height();
    if (std::abs(sx - sy) > 0.02 * std::max(sx, sy))
        return false;
    // Uniform scale can still be the workspace: a grid of equal monitors is an
    // exact multiple of one of them. Catch the two sizes a workspace stream
    // actually arrives at — logical union, or union at this screen's scale.
    if (screenCount > 1
        && (portalSize == unionLogical
            || portalSize == QSize(qRound(unionLogical.width() * dpr),
                                   qRound(unionLogical.height() * dpr))))
        return false;
    return true;
}

} // namespace StreamGeometry
