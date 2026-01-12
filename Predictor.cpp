#include "Predictor.h"
#include <SFML/Graphics.hpp>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <numeric>

// -------------------- utils --------------------

double Predictor::sigmoid(double x) {
    if (x > 50) return 1.0;
    if (x < -50) return 0.0;
    return 1.0 / (1.0 + std::exp(-x));
}

double Predictor::clamp(double x, double lo, double hi) {
    return std::max(lo, std::min(hi, x));
}

int Predictor::timeToMinutes(const std::string& hhmm) {
    if (hhmm.size() != 5 || hhmm[2] != ':') return -1;
    int hh = std::stoi(hhmm.substr(0, 2));
    int mm = std::stoi(hhmm.substr(3, 2));
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return -1;
    return hh * 60 + mm;
}

double Predictor::timeAdjustmentMultiplier(int minutes) {
    if (minutes < 0) return 1.0;
    if (minutes >= 240 && minutes < 570) return 0.80; // premarket
    if (minutes >= 570 && minutes <= 960) return 1.00; // regular
    return 0.85; // after-hours
}

double Predictor::openConfidenceDecayMultiplier(int minutes) {
    if (minutes < 0) return 1.0;
    const int open = 570;      // 09:30
    const int window = 10;     // +/- 10 min
    int dist = std::abs(minutes - open);
    if (dist > window) return 1.0;
    double t = (double)dist / (double)window; // 0..1
    double mult = 0.70 + 0.30 * t;
    return clamp(mult, 0.70, 1.00);
}

// -------------------- NEW: color + extraction --------------------

bool Predictor::nearColor(unsigned char r, unsigned char g, unsigned char b,
                          unsigned char tr, unsigned char tg, unsigned char tb,
                          int tol) {
    return (std::abs((int)r - (int)tr) <= tol) &&
           (std::abs((int)g - (int)tg) <= tol) &&
           (std::abs((int)b - (int)tb) <= tol);
}

/*
  We are trying to extract a "price path" from candle colors.
  Your screenshots typically have:
   - bullish candles: green-ish
   - bearish candles: magenta/pink-ish
  We'll scan each x column in the chart area and find the highest candle pixel.
  Then normalize y -> value.
*/
std::vector<float> Predictor::extractPriceSeriesFromScreenshot(const std::string& imagePath) {
    sf::Image img;
    if (!img.loadFromFile(imagePath)) {
        throw std::runtime_error("Could not load image: " + imagePath);
    }

    const auto W = (int)img.getSize().x;
    const auto H = (int)img.getSize().y;

    // Crop out toolbars/labels/indicators by percentage.
    // TUNE THESE if needed based on your screenshot style.
    const int topCut    = (int)(0.10 * H);
    const int bottomCut = (int)(0.25 * H);
    const int leftCut   = (int)(0.03 * W);
    const int rightCut  = (int)(0.02 * W);

    const int y0 = topCut;
    const int y1 = H - bottomCut;
    const int x0 = leftCut;
    const int x1 = W - rightCut;

    const int tol = 45;

    // Target candle colors (approx)
    // Green bullish
    const unsigned char gR =  40, gG = 220, gB = 140;
    // Magenta bearish
    const unsigned char mR = 220, mG =  60, mB = 220;

    std::vector<float> series;
    series.reserve(x1 - x0);

    for (int x = x0; x < x1; x++) {
        int bestY = -1;

        // Scan from top down: first candle pixel we hit is the "highest"
        for (int y = y0; y < y1; y++) {
            sf::Color c = img.getPixel((unsigned)x, (unsigned)y);

            bool isBull = nearColor(c.r, c.g, c.b, gR, gG, gB, tol);
            bool isBear = nearColor(c.r, c.g, c.b, mR, mG, mB, tol);

            if (isBull || isBear) {
                bestY = y;
                break;
            }
        }

        // If no candle pixel found, mark as NaN-ish sentinel
        if (bestY < 0) {
            series.push_back(-1.f);
        } else {
            // Convert y to "price value": higher price = larger value
            // y0 is top (high), y1 is bottom (low)
            float norm = 1.f - (float)(bestY - y0) / (float)(y1 - y0);
            series.push_back(clamp(norm, 0.0, 1.0));
        }
    }

    // Fill gaps: replace -1 with nearest valid neighbor
    // forward pass
    float last = -1.f;
    for (auto& v : series) {
        if (v >= 0.f) last = v;
        else if (last >= 0.f) v = last;
    }
    // backward pass
    float next = -1.f;
    for (int i = (int)series.size() - 1; i >= 0; i--) {
        if (series[i] >= 0.f) next = series[i];
        else if (next >= 0.f) series[i] = next;
        else series[i] = 0.5f; // fallback
    }

    return series;
}

std::vector<float> Predictor::smoothSeries(const std::vector<float>& s, int window) {
    if (window <= 1) return s;
    std::vector<float> out(s.size(), 0.f);

    int w = std::max(1, window);
    for (int i = 0; i < (int)s.size(); i++) {
        int a = std::max(0, i - w);
        int b = std::min((int)s.size() - 1, i + w);
        float sum = 0.f;
        for (int j = a; j <= b; j++) sum += s[j];
        out[i] = sum / (float)(b - a + 1);
    }
    return out;
}

// window = how many points left/right define a local max/min
std::vector<Predictor::SwingPoint> Predictor::findSwings(const std::vector<float>& s, int window) {
    std::vector<SwingPoint> swings;
    if ((int)s.size() < 2 * window + 1) return swings;

    for (int i = window; i < (int)s.size() - window; i++) {
        float v = s[i];
        bool isMax = true;
        bool isMin = true;

        for (int k = 1; k <= window; k++) {
            if (s[i - k] >= v || s[i + k] >= v) isMax = false;
            if (s[i - k] <= v || s[i + k] <= v) isMin = false;
            if (!isMax && !isMin) break;
        }

        if (isMax) swings.push_back({i, v, true});
        else if (isMin) swings.push_back({i, v, false});
    }

    // Reduce noise: keep alternating highs/lows and remove very tiny swings
    std::vector<SwingPoint> cleaned;
    const float minMove = 0.02f; // tune
    for (const auto& sp : swings) {
        if (cleaned.empty()) {
            cleaned.push_back(sp);
            continue;
        }
        auto& last = cleaned.back();
        if (sp.isHigh == last.isHigh) {
            // keep the more extreme one
            if (sp.isHigh && sp.value > last.value) last = sp;
            if (!sp.isHigh && sp.value < last.value) last = sp;
        } else {
            if (std::abs(sp.value - last.value) >= minMove) cleaned.push_back(sp);
        }
    }

    return cleaned;
}

// HH/HL bullish, LH/LL bearish (using last few swings)
double Predictor::trendScoreFromSwings(const std::vector<SwingPoint>& swings) {
    if (swings.size() < 4) return 0.0;

    // Take last N swings
    int N = std::min(10, (int)swings.size());
    std::vector<SwingPoint> tail(swings.end() - N, swings.end());

    // Collect highs and lows in order
    std::vector<float> highs, lows;
    for (auto& sp : tail) {
        if (sp.isHigh) highs.push_back(sp.value);
        else lows.push_back(sp.value);
    }
    if (highs.size() < 2 || lows.size() < 2) return 0.0;

    float lastHigh = highs.back();
    float prevHigh = highs[highs.size() - 2];
    float lastLow  = lows.back();
    float prevLow  = lows[lows.size() - 2];

    double score = 0.0;

    // HH / HL => bullish
    if (lastHigh > prevHigh) score += 1.0;
    else score -= 1.0;

    if (lastLow > prevLow) score += 1.0;
    else score -= 1.0;

    // amplify if both align
    return score; // range roughly [-2, +2]
}

// slope + volatility: bullish if slope positive, penalize chop
double Predictor::momentumScore(const std::vector<float>& s) {
    if (s.size() < 20) return 0.0;

    int n = (int)s.size();
    int a = std::max(0, n - 120); // last chunk
    int b = n - 1;

    float start = s[a];
    float end   = s[b];
    float slope = end - start; // normalized

    // volatility as stddev of deltas
    std::vector<float> deltas;
    deltas.reserve(b - a);
    for (int i = a + 1; i <= b; i++) deltas.push_back(s[i] - s[i - 1]);

    float mean = std::accumulate(deltas.begin(), deltas.end(), 0.f) / (float)deltas.size();
    float var = 0.f;
    for (float d : deltas) var += (d - mean) * (d - mean);
    var /= (float)deltas.size();
    float stdev = std::sqrt(var);

    // Score: slope helps, volatility hurts slightly (choppy)
    double score = 6.0 * (double)slope - 4.0 * (double)stdev;
    return score; // typical small range
}

// Simple double top/bottom:
// If last two highs are close and price is failing => bearish
// If last two lows are close and price is bouncing => bullish
double Predictor::doubleTopBottomScore(const std::vector<SwingPoint>& swings) {
    if (swings.size() < 6) return 0.0;

    // Find last two highs and last two lows
    std::vector<SwingPoint> highs, lows;
    for (auto& sp : swings) {
        if (sp.isHigh) highs.push_back(sp);
        else lows.push_back(sp);
    }
    if (highs.size() < 2 && lows.size() < 2) return 0.0;

    const float tol = 0.015f; // closeness tolerance

    double score = 0.0;

    if (highs.size() >= 2) {
        auto h1 = highs[highs.size() - 2];
        auto h2 = highs[highs.size() - 1];
        if (std::abs(h2.value - h1.value) <= tol) {
            // looks like double top => bearish bias
            score -= 1.2;
        }
    }

    if (lows.size() >= 2) {
        auto l1 = lows[lows.size() - 2];
        auto l2 = lows[lows.size() - 1];
        if (std::abs(l2.value - l1.value) <= tol) {
            // looks like double bottom => bullish bias
            score += 1.2;
        }
    }

    return score;
}

// -------------------- public API --------------------

Prediction Predictor::predictWithTime(const std::string& imagePath,
                                      const std::string& timeStr) {
    int minutes = timeToMinutes(timeStr);

    double rawScore = computeRawScore(imagePath);

    double m1 = timeAdjustmentMultiplier(minutes);
    double m2 = openConfidenceDecayMultiplier(minutes);
    double adjustedScore = rawScore * m1 * m2;

    double pBull = sigmoid(adjustedScore);
    double pBear = 1.0 - pBull;

    Prediction out;
    out.pBull = pBull;
    out.pBear = pBear;
    out.label = (pBull >= 0.5) ? "Bullish" : "Bearish";
    out.confidence = 100.0 * std::max(pBull, pBear);
    return out;
}

// -------------------- THE KEY UPGRADE: real feature score --------------------

double Predictor::computeRawScore(const std::string& imagePath) const {
    // 1) Extract series
    auto series = extractPriceSeriesFromScreenshot(imagePath);

    // 2) Smooth (reduces candle noise)
    auto smooth = smoothSeries(series, 3);

    // 3) Swings
    auto swings = findSwings(smooth, 8);

    // 4) Scores
    double tScore = trendScoreFromSwings(swings);      // ~[-2,+2]
    double mScore = momentumScore(smooth);             // small
    double rScore = doubleTopBottomScore(swings);      // ~[-1.2,+1.2]

    // Combine:
    // Trend is primary, momentum supports, reversal can flip/soften.
    double raw = 1.6 * tScore + 1.0 * mScore + 1.2 * rScore;

    // Clamp to keep probabilities stable
    raw = clamp(raw, -6.0, 6.0);

    return raw;
}





