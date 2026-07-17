#include "CursorSmoother.h"

#include <cmath>

namespace {

// Smoothing factor for a first-order low-pass at cutoff Hz over a step of dt
// seconds: alpha = 1 / (1 + tau/dt), tau = 1/(2*pi*cutoff). Larger alpha ==
// less smoothing (follows the input); smaller == heavier smoothing.
double lowpassAlpha(double cutoffHz, double dt)
{
    const double tau = 1.0 / (2.0 * M_PI * cutoffHz);
    return 1.0 / (1.0 + tau / dt);
}

} // namespace

double CursorSmoother::LowPass::filter(double x, double alpha)
{
    if (!seeded) {
        prev = x;
        seeded = true;
        return x;
    }
    prev = alpha * x + (1.0 - alpha) * prev;
    return prev;
}

double CursorSmoother::OneEuro::filter(double x, double dt, const Params &p)
{
    if (!have) {
        have = true;
        prevRaw = x;
        value.filter(x, 1.0);   // seed: first output == first input
        return x;
    }
    // Speed estimate from the raw signal, then pre-filtered so the adaptive
    // cutoff itself doesn't chatter.
    const double speed = (x - prevRaw) / dt;
    prevRaw = x;
    const double edSpeed = deriv.filter(speed, lowpassAlpha(p.dCutoff, dt));
    const double cutoff = p.minCutoff + p.beta * std::fabs(edSpeed);
    return value.filter(x, lowpassAlpha(cutoff, dt));
}

QPointF CursorSmoother::filter(double x, double y, qint64 tNs)
{
    if (!m_seeded) {
        // First sample seeds the filter and is emitted unchanged, so the
        // overlay starts exactly where the pointer actually is.
        m_seeded = true;
        m_prevT = tNs;
        m_lastX = m_fx.filter(x, 1.0, m_p);
        m_lastY = m_fy.filter(y, 1.0, m_p);
        return QPointF(m_lastX, m_lastY);
    }

    const double dt = double(tNs - m_prevT) / 1e9;
    if (dt <= 0.0) {
        // Equal/regressed timestamp: the filter step is undefined for dt<=0, so
        // hold the previous filtered position rather than divide by zero. The
        // timestamp base is left alone, so the next real sample measures its dt
        // from the last sample that actually advanced the filter.
        return QPointF(m_lastX, m_lastY);
    }

    m_prevT = tNs;
    m_lastX = m_fx.filter(x, dt, m_p);
    m_lastY = m_fy.filter(y, dt, m_p);
    return QPointF(m_lastX, m_lastY);
}

void CursorSmoother::reset()
{
    m_fx = OneEuro{};
    m_fy = OneEuro{};
    m_prevT = 0;
    m_lastX = 0.0;
    m_lastY = 0.0;
    m_seeded = false;
}
