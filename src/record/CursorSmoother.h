#pragma once
#include <QPointF>
#include <QtGlobal>

// Streaming one-euro filter for cursor positions. Raw PipeWire cursor samples are
// integer stream pixels and jitter by a pixel or two at rest; burning that
// straight into the recorded frames makes the pointer overlay twitch. The
// one-euro filter (Casiez, Roussel, Vogel 2012) is the right tool: a low-pass
// whose cutoff RISES with pointer speed, so a slow hover is smoothed hard while a
// fast deliberate move is tracked closely with little lag.
//
// Unlike a batch smoother this one is fed ONE sample at a time and answers
// immediately — the overlay is composited into live frames, so there is no future
// to look at. It therefore carries its filter state across calls; reset() drops
// that state so the next filter() re-seeds (recording start / resume).
//
// Timestamps are irregular (samples arrive per-frame), so dt is taken from each
// sample delta rather than assumed fixed; a non-positive dt (equal or regressed
// timestamps) is guarded by reusing the previous filtered value.
class CursorSmoother
{
public:
    struct Params {
        // Baseline cutoff in Hz at zero speed: lower == smoother/laggier at
        // rest. 1.0 Hz kills resting jitter without visible lag on a hover.
        double minCutoff = 1.0;
        // Speed coupling. cutoff = minCutoff + beta*|speed|, speed in px/s, so
        // beta carries units of Hz per (px/s). 0.007 was tuned on synthetic
        // pointer paths: at a brisk ~800 px/s move it lifts the cutoff to
        // ~6.6 Hz (tracks the intent), while sub-10 px/s resting jitter stays
        // near the 1 Hz floor (damped hard). Raise it if fast moves feel laggy,
        // lower it if fast moves feel jittery.
        double beta = 0.007;
        // Cutoff of the derivative pre-filter, Hz. 1.0 keeps the speed estimate
        // itself from chattering. Rarely needs tuning.
        double dCutoff = 1.0;
    };

    CursorSmoother() = default;
    explicit CursorSmoother(const Params &p) : m_p(p) {}

    // Feeds one raw sample; returns the filtered position. tNs is CLOCK_MONOTONIC
    // ns (the same clock domain as PipeWire frame pts). The first sample after
    // construction or reset() is returned unchanged — it seeds the filter.
    QPointF filter(double x, double y, qint64 tNs);

    // Drops all filter state: the next filter() call re-seeds and is emitted
    // unchanged. Call on recording start / resume, where the pointer may have
    // teleported and the timestamp base has moved.
    void reset();

private:
    // Exponential low-pass that remembers its last output. One instance per axis
    // for the signal, one per axis for the derivative.
    struct LowPass {
        double prev = 0.0;
        bool seeded = false;
        double filter(double x, double alpha);
    };

    // One-euro state for a single scalar channel (x or y tracked independently).
    struct OneEuro {
        LowPass value;   // the signal filter
        LowPass deriv;   // the speed pre-filter
        double prevRaw = 0.0;
        bool have = false;
        double filter(double x, double dt, const Params &p);
    };

    Params m_p;
    OneEuro m_fx;
    OneEuro m_fy;
    qint64 m_prevT = 0;
    double m_lastX = 0.0;
    double m_lastY = 0.0;
    bool m_seeded = false;
};
