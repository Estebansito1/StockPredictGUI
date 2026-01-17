// ===============================
// File: Predictor.cpp
// ===============================
#include "Predictor.h"
#include <SFML/Graphics.hpp>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <numeric>
#include <fstream>
#include <sstream>
#include <limits>

Predictor::Predictor() = default;

// ---------- utils ----------
double Predictor::clamp(double x, double lo, double hi) {
    return std::max(lo, std::min(hi, x));
}

double Predictor::sigmoid(double x) {
    if (x > 50) return 1.0;
    if (x < -50) return 0.0;
    return 1.0 / (1.0 + std::exp(-x));
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
    if (minutes >= 240 && minutes < 570) return 0.85; // premarket
    if (minutes >= 570 && minutes <= 960) return 1.00; // regular
    return 0.90; // after-hours
}

double Predictor::openConfidenceDecayMultiplier(int minutes) {
    if (minutes < 0) return 1.0;
    const int open = 570;  // 09:30
    const int window = 10; // +/- 10 min
    int dist = std::abs(minutes - open);
    if (dist > window) return 1.0;
    double t = (double)dist / (double)window; // 0..1
    double mult = 0.70 + 0.30 * t;
    return clamp(mult, 0.70, 1.00);
}

// ---------- config ----------
void Predictor::setCandleColors(unsigned char bullR, unsigned char bullG, unsigned char bullB,
                               unsigned char bearR, unsigned char bearG, unsigned char bearB,
                               int tolerance) {
    color_.bullR = bullR; color_.bullG = bullG; color_.bullB = bullB;
    color_.bearR = bearR; color_.bearG = bearG; color_.bearB = bearB;
    color_.tolerance = tolerance;
}

void Predictor::setWeights(double trendW, double momentumW, double reversalW, double srW) {
    w_.trend = trendW;
    w_.momentum = momentumW;
    w_.reversal = reversalW;
    w_.sr = srW;
}

void Predictor::setConfidenceThreshold(double threshold) {
    confidenceThreshold_ = clamp(threshold, 0.0, 100.0);
}

// ---------- image helpers ----------
bool Predictor::nearColor(unsigned char r, unsigned char g, unsigned char b,
                          unsigned char tr, unsigned char tg, unsigned char tb,
                          int tol) {
    return (std::abs((int)r - (int)tr) <= tol) &&
           (std::abs((int)g - (int)tg) <= tol) &&
           (std::abs((int)b - (int)tb) <= tol);
}

// Improved: estimate CLOSE per column using bull/bear majority and extremum.
std::vector<float> Predictor::extractCloseSeries(const std::string& imagePath) const {
    sf::Image img;
    if (!img.loadFromFile(imagePath)) {
        throw std::runtime_error("Could not load image: " + imagePath);
    }

    const int W = (int)img.getSize().x;
    const int H = (int)img.getSize().y;

    const int topCut    = (int)(0.10 * H);
    const int bottomCut = (int)(0.25 * H); // exclude MACD/RSI panels
    const int leftCut   = (int)(0.03 * W);
    const int rightCut  = (int)(0.02 * W);

    const int y0 = topCut;
    const int y1 = H - bottomCut;
    const int x0 = leftCut;
    const int x1 = W - rightCut;

    std::vector<float> series;
    series.reserve(std::max(0, x1 - x0));

    for (int x = x0; x < x1; x++) {
        int bullCount = 0, bearCount = 0;
        int bullMinY =  std::numeric_limits<int>::max();
        int bearMaxY = -1;

        for (int y = y0; y < y1; y++) {
            sf::Color c = img.getPixel((unsigned)x, (unsigned)y);

            bool isBull = nearColor(c.r, c.g, c.b,
                                    color_.bullR, color_.bullG, color_.bullB,
                                    color_.tolerance);
            bool isBear = nearColor(c.r, c.g, c.b,
                                    color_.bearR, color_.bearG, color_.bearB,
                                    color_.tolerance);

            if (isBull) {
                bullCount++;
                bullMinY = std::min(bullMinY, y);
            } else if (isBear) {
                bearCount++;
                bearMaxY = std::max(bearMaxY, y);
            }
        }

        int closeY = -1;
        if (bullCount == 0 && bearCount == 0) closeY = -1;
        else if (bullCount >= bearCount) closeY = bullMinY; // bull close near top
        else closeY = bearMaxY; // bear close near bottom

        if (closeY < 0) series.push_back(-1.f);
        else {
            float norm = 1.f - (float)(closeY - y0) / (float)(y1 - y0);
            series.push_back((float)clamp(norm, 0.0, 1.0));
        }
    }

    // Gap fill
    float last = -1.f;
    for (auto& v : series) {
        if (v >= 0.f) last = v;
        else if (last >= 0.f) v = last;
    }
    float next = -1.f;
    for (int i = (int)series.size() - 1; i >= 0; i--) {
        if (series[i] >= 0.f) next = series[i];
        else if (next >= 0.f) series[i] = next;
        else series[i] = 0.5f;
    }

    return series;
}

// NOTE: ADDED HERE — Extract volume per column (normalized 0..1)
// Assumes volume bars are in a lower panel (above MACD), colored green/red on dark background.
// Returns one value per candle column, aligned to the same x-range trimming style as extractCloseSeries().
std::vector<float> Predictor::extractVolumeSeries(const std::string& imagePath) const {
    sf::Image img;
    if (!img.loadFromFile(imagePath)) {
        throw std::runtime_error("Could not load image: " + imagePath);
    }

    const int W = (int)img.getSize().x;
    const int H = (int)img.getSize().y;

    // match the same horizontal trimming as close extraction
    const int leftCut   = (int)(0.03 * W);
    const int rightCut  = (int)(0.02 * W);
    const int x0 = leftCut;
    const int x1 = W - rightCut;

    // volume panel band (tuned for your screenshots)
    const int volTop    = (int)(0.74 * H);
    const int volBottom = (int)(0.89 * H);

    std::vector<float> vol;
    vol.reserve(std::max(0, x1 - x0));

    // volume bar colors (green/red) + generous tolerance
    const unsigned char gR = 0,   gG = 200, gB = 120;
    const unsigned char rR = 200, rG = 60,  rB = 60;
    const int tol = 70;

    for (int x = x0; x < x1; x++) {
        int barHeightPx = 0;
        bool started = false;

        for (int y = volBottom; y >= volTop; y--) {
            sf::Color c = img.getPixel((unsigned)x, (unsigned)y);

            bool isVolGreen = nearColor(c.r, c.g, c.b, gR, gG, gB, tol);
            bool isVolRed   = nearColor(c.r, c.g, c.b, rR, rG, rB, tol);

            if (isVolGreen || isVolRed) {
                started = true;
                barHeightPx++;
            } else {
                if (started) break;
            }
        }

        double panelH = std::max(1, (volBottom - volTop));
        double v01 = (double)barHeightPx / panelH;
        vol.push_back((float)clamp(v01, 0.0, 1.0));
    }

    // light gap fill: if totally missing, treat as 0
    float last = -1.f;
    for (auto& v : vol) {
        if (v > 0.f) last = v;
        else if (last >= 0.f) v = last;
    }
    float next = -1.f;
    for (int i = (int)vol.size() - 1; i >= 0; i--) {
        if (vol[i] > 0.f) next = vol[i];
        else if (next >= 0.f) vol[i] = next;
        else vol[i] = 0.f;
    }

    return vol;
}

static double clamp01(double x) {
    if (x < 0.0) return 0.0;
    if (x > 1.0) return 1.0;
    return x;
}

static void applyNeutralCalibration(Prediction& out, double adjustedScore) {
    const double maxTilt = 0.05;
    const double scale   = 2.0;

    double t = adjustedScore / scale;
    t = std::max(-1.0, std::min(1.0, t));

    double tilt = maxTilt * t;
    out.pBull = clamp01(0.5 + tilt);
    out.pBear = 1.0 - out.pBull;

    out.confidence = 50.0 + (std::abs(t) * 10.0); // 50..60
    out.label = "Neutral";
    out.signal = "NEUTRAL";
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

    std::vector<SwingPoint> cleaned;
    const float minMove = 0.02f;
    for (const auto& sp : swings) {
        if (cleaned.empty()) { cleaned.push_back(sp); continue; }

        auto& last = cleaned.back();
        if (sp.isHigh == last.isHigh) {
            if (sp.isHigh && sp.value > last.value) last = sp;
            if (!sp.isHigh && sp.value < last.value) last = sp;
        } else {
            if (std::abs(sp.value - last.value) >= minMove) cleaned.push_back(sp);
        }
    }

    return cleaned;
}

// ---------- features ----------
double Predictor::trendScoreFromSwings(const std::vector<SwingPoint>& swings, FeatureBreakdown& bd) {
    if (swings.size() < 4) return 0.0;

    int N = std::min(10, (int)swings.size());
    std::vector<SwingPoint> tail(swings.end() - N, swings.end());

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
    bool hh = (lastHigh > prevHigh);
    bool hl = (lastLow  > prevLow);

    score += hh ? 1.0 : -1.0;
    score += hl ? 1.0 : -1.0;

    if (hh && hl) bd.patterns.push_back("HH_HL");
    if (!hh && !hl) bd.patterns.push_back("LH_LL");
    if (hh && !hl) bd.patterns.push_back("HH_LL_MIXED");
    if (!hh && hl) bd.patterns.push_back("LH_HL_MIXED");

    return score; // ~[-2,+2]
}

double Predictor::momentumScoreFromSeries(const std::vector<float>& s) {
    if (s.size() < 30) return 0.0;

    int n = (int)s.size();
    int a = std::max(0, n - 140);
    int b = n - 1;

    float start = s[a];
    float end   = s[b];
    float slope = end - start;

    std::vector<float> deltas;
    deltas.reserve(b - a);
    for (int i = a + 1; i <= b; i++) deltas.push_back(s[i] - s[i - 1]);

    float mean = std::accumulate(deltas.begin(), deltas.end(), 0.f) / (float)deltas.size();
    float var = 0.f;
    for (float d : deltas) var += (d - mean) * (d - mean);
    var /= (float)deltas.size();
    float stdev = std::sqrt(var);

    double score = 6.0 * (double)slope - 4.0 * (double)stdev;
    return clamp(score, -1.5, 1.5);
}

double Predictor::doubleTopBottomScore(const std::vector<SwingPoint>& swings, FeatureBreakdown& bd) {
    if (swings.size() < 6) return 0.0;

    std::vector<SwingPoint> highs, lows;
    for (auto& sp : swings) (sp.isHigh ? highs : lows).push_back(sp);

    const float tol = 0.015f;
    double score = 0.0;

    if (highs.size() >= 2) {
        auto h1 = highs[highs.size() - 2];
        auto h2 = highs[highs.size() - 1];
        if (std::abs(h2.value - h1.value) <= tol) {
            score -= 1.2;
            bd.patterns.push_back("DOUBLE_TOP");
        }
    }
    if (lows.size() >= 2) {
        auto l1 = lows[lows.size() - 2];
        auto l2 = lows[lows.size() - 1];
        if (std::abs(l2.value - l1.value) <= tol) {
            score += 1.2;
            bd.patterns.push_back("DOUBLE_BOTTOM");
        }
    }
    return score;
}

std::vector<Predictor::Level> Predictor::findSupportResistance(const std::vector<SwingPoint>& swings) {
    std::vector<Level> levels;
    if (swings.size() < 6) return levels;

    const float tol = 0.012f;

    auto addTouch = [&](float price, bool isSupport) {
        for (auto& L : levels) {
            if (L.isSupport == isSupport && std::abs(L.price - price) <= tol) {
                L.touches += 1;
                L.price = 0.7f * L.price + 0.3f * price;
                return;
            }
        }
        Level L;
        L.price = price;
        L.touches = 1;
        L.isSupport = isSupport;
        levels.push_back(L);
    };

    for (auto& sp : swings) {
        if (sp.isHigh) addTouch(sp.value, false);
        else addTouch(sp.value, true);
    }

    for (auto& L : levels) {
        L.strength = (float)std::min(10, L.touches) / 10.f;
    }

    std::sort(levels.begin(), levels.end(), [](const Level& a, const Level& b) {
        return a.touches > b.touches;
    });
    if (levels.size() > 6) levels.resize(6);

    std::sort(levels.begin(), levels.end(), [](const Level& a, const Level& b) {
        return a.price < b.price;
    });

    return levels;
}

double Predictor::srScoreFromLevels(const std::vector<float>& series,
                                   const std::vector<Level>& levels,
                                   FeatureBreakdown& bd) {
    if (series.empty() || levels.empty()) return 0.0;
    float last = series.back();

    double score = 0.0;

    float bestSupport = -1.f;
    float bestRes = 2.f;
    float sStrength = 0.f, rStrength = 0.f;

    for (auto& L : levels) {
        if (L.isSupport && L.price <= last && L.price > bestSupport) {
            bestSupport = L.price; sStrength = L.strength;
        }
        if (!L.isSupport && L.price >= last && L.price < bestRes) {
            bestRes = L.price; rStrength = L.strength;
        }
    }

    if (bestSupport >= 0.f) {
        float dist = last - bestSupport;
        score += 2.0 * dist * (0.5 + sStrength);
    }
    if (bestRes <= 1.f) {
        float dist = bestRes - last;
        score -= 2.0 * dist * (0.5 + rStrength);
    }

    score = clamp(score, -1.0, 1.0);
    bd.patterns.push_back("SUP_RES_USED");
    return score;
}

// -------------------------------
// 🔴 BUY TYPE 2 — Breakout Buy (missing)  ✅ added
// -------------------------------
void Predictor::detectBreakoutBuy(const Series& s,
                                 const std::vector<double>& resistanceLevels,
                                 double trendScore,
                                 bool& outBreakout,
                                 double& outScore,
                                 double& outR) const {
    outBreakout = false;
    outScore = 0.0;
    outR = 0.0;

    // We only need close for this simple breakout detector.
    if (s.close.size() < 25 || resistanceLevels.empty()) return;

    const double last = s.close.back();

    // Find nearest resistance >= last (or nearest above in general)
    double r = 2.0;
    for (double L : resistanceLevels) {
        if (L >= last) r = std::min(r, L);
    }
    // If none above, use the highest resistance as "breakout level"
    if (r > 1.0) {
        r = *std::max_element(resistanceLevels.begin(), resistanceLevels.end());
    }

    // Margin in normalized space: needs to clear level cleanly
    const double clearMargin = 0.008;   // ~0.8% of chart height (normalized)
    const double holdBelowMargin = 0.003;

    // Need a bullish bias / structure
    if (trendScore < 0.5) return;

    // Confirm we were "below/at" resistance recently (consolidation), then broke above
    const int K = 12;
    int belowCount = 0;
    for (int i = (int)s.close.size() - K - 1; i < (int)s.close.size() - 1; i++) {
        if (i < 0) continue;
        if (s.close[i] <= (r - holdBelowMargin)) belowCount++;
    }

    const bool brokeAbove = (last >= (r + clearMargin));
    const bool wasBelow = (belowCount >= (int)(0.65 * K)); // mostly below

    if (!brokeAbove || !wasBelow) return;

    // Strength score: how far above resistance + micro momentum
    double above = clamp((last - r) / 0.05, 0.0, 1.0); // normalize above-distance
    double mom = 0.0;
    {
        int n = (int)s.close.size();
        double prev = s.close[n - 6];
        mom = clamp((last - prev) / 0.05, 0.0, 1.0);
    }

    // Weighting: above-resistance matters most, then momentum, then trend confirmation
    double trend01 = clamp((trendScore / 2.0), 0.0, 1.0);
    double score = 0.55 * above + 0.30 * mom + 0.15 * trend01;

    outBreakout = true;
    outScore = clamp(score, 0.0, 1.0);
    outR = clamp(r, 0.0, 1.0);
}

// ---------- trade plan ----------
void Predictor::buildTradePlan(Prediction& out,
                               const std::vector<float>& series,
                               const std::vector<Level>& levels) {
    if (series.empty()) return;
    double last = series.back();

    double support = -1.0;
    double resistance = 2.0;

    for (auto& L : levels) {
        if (L.isSupport && L.price <= last) support = std::max(support, (double)L.price);
        if (!L.isSupport && L.price >= last) resistance = std::min(resistance, (double)L.price);
    }

    if (support < 0.0) support = clamp(last - 0.03, 0.0, 1.0);
    if (resistance > 1.0) resistance = clamp(last + 0.03, 0.0, 1.0);

    if (out.label == "Bullish") {
        out.stopLoss = clamp(support - 0.01, 0.0, 1.0);
        out.target1  = clamp(resistance, 0.0, 1.0);
        out.target2  = clamp(resistance + (resistance - last) * 0.8, 0.0, 1.0);

        double risk = std::max(1e-6, last - out.stopLoss);
        double reward = std::max(0.0, out.target1 - last);
        out.riskRewardRatio = reward / risk;
    } else {
        out.stopLoss = clamp(resistance + 0.01, 0.0, 1.0);
        out.target1  = clamp(support, 0.0, 1.0);
        out.target2  = clamp(support - (last - support) * 0.8, 0.0, 1.0);

        double risk = std::max(1e-6, out.stopLoss - last);
        double reward = std::max(0.0, last - out.target1);
        out.riskRewardRatio = reward / risk;
    }
}

std::string Predictor::signalFromConfidence(double conf, const std::string& label) {
    if (conf >= 80.0) return (label == "Bullish") ? "STRONG_BUY" : "STRONG_SELL";
    if (conf >= 65.0) return (label == "Bullish") ? "BUY" : "SELL";
    return "NEUTRAL";
}

// ---------- timeframe ----------
int Predictor::timeframeFromFilename(const std::string& path) {
    if (path.find("test30") != std::string::npos || path.find("_30m") != std::string::npos) return 30;
    if (path.find("test5")  != std::string::npos || path.find("_5m")  != std::string::npos) return 5;
    if (path.find("test1")  != std::string::npos || path.find("_1m")  != std::string::npos) return 1;
    return -1;
}

void Predictor::applyTimeframeWeights(int tfMinutes, double& tW, double& mW, double& rW, double& srW) {
    if (tfMinutes == 1) {
        tW *= 1.0;  mW *= 1.35; rW *= 1.10; srW *= 0.80;
    } else if (tfMinutes == 5) {
        tW *= 1.1;  mW *= 1.15; rW *= 1.10; srW *= 1.00;
    } else if (tfMinutes == 30) {
        tW *= 1.35; mW *= 0.85; rW *= 1.00; srW *= 1.35;
    }
}

namespace {
static double nearestDistanceToLevels(double x, const std::vector<double>& levels) {
    if (levels.empty()) return 1.0;
    double best = 1e9;
    for (double L : levels) best = std::min(best, std::abs(L - x));
    return best;
}

static void tagActiveSR(Prediction& out, double lastN) {
    // Active support = closest support <= lastN
    double bestSup = -1.0;
    for (double s : out.supportLevels) {
        if (s <= lastN) bestSup = std::max(bestSup, s);
    }
    if (bestSup >= 0.0) {
        out.hasActiveSupport = true;
        out.activeSupport = bestSup;
        out.distToSupport = std::abs(lastN - bestSup);
    } else {
        out.hasActiveSupport = false;
        out.distToSupport = 1.0;
    }

    // Active resistance = closest resistance >= lastN
    double bestRes = 2.0;
    for (double r : out.resistanceLevels) {
        if (r >= lastN) bestRes = std::min(bestRes, r);
    }
    if (bestRes <= 1.0) {
        out.hasActiveResistance = true;
        out.activeResistance = bestRes;
        out.distToResistance = std::abs(bestRes - lastN);
    } else {
        out.hasActiveResistance = false;
        out.distToResistance = 1.0;
    }
}

static void suppressPlanIfNoTrade(Prediction& out) {
    if (out.signal == "NEUTRAL") {
        out.stopLoss = 0.0;
        out.target1  = 0.0;
        out.target2  = 0.0;
        out.riskRewardRatio = 0.0;
    }
}
}

// ---------- core scoring ----------
double Predictor::computeRawScore(const std::string& imagePath, FeatureBreakdown& bd,
                                 std::vector<double>& supports, std::vector<double>& resistances) const {
    auto close = extractCloseSeries(imagePath);
    auto smooth = smoothSeries(close, 3);
    auto swings = findSwings(smooth, 8);

    auto levels = findSupportResistance(swings);
    supports.clear(); resistances.clear();
    for (auto& L : levels) {
        if (L.isSupport) supports.push_back(L.price);
        else resistances.push_back(L.price);
    }

    bd = FeatureBreakdown{};
    double t  = trendScoreFromSwings(swings, bd);
    double m  = momentumScoreFromSeries(smooth);
    double r  = doubleTopBottomScore(swings, bd);
    double sr = srScoreFromLevels(smooth, levels, bd);

    bd.trendScore = t;
    bd.momentumScore = m;
    bd.reversalScore = r;
    bd.srScore = sr;

    double raw = w_.trend * t + w_.momentum * m + w_.reversal * r + w_.sr * sr;
    raw = clamp(raw, -8.0, 8.0);
    return raw;
}

double Predictor::computeRawScore(const std::string& imagePath) const {
    FeatureBreakdown bd;
    std::vector<double> sup, res;
    return computeRawScore(imagePath, bd, sup, res);
}

// ---------- public API ----------
Prediction Predictor::predictWithTime(const std::string& imagePath,
                                      const std::string& timeStr,
                                      bool hasScale,
                                      double minPrice,
                                      double maxPrice) {
    int minutes = timeToMinutes(timeStr);

    FeatureBreakdown bd;
    std::vector<double> supports, resistances;
    double rawScore = computeRawScore(imagePath, bd, supports, resistances);

    double m1 = timeAdjustmentMultiplier(minutes);
    double m2 = openConfidenceDecayMultiplier(minutes);
    double adjustedScore = rawScore * m1 * m2;

    double compressed = adjustedScore / 2.5;
    double pBull = sigmoid(compressed);
    double pBear = 1.0 - pBull;

    Prediction out;
    out.pBull = pBull;
    out.pBear = pBear;
    out.label = (pBull >= 0.5) ? "Bullish" : "Bearish";
    out.confidence = 100.0 * std::max(pBull, pBear);

    // confidence calibration
    const double neutralThreshold = 2.0;
    if (std::abs(adjustedScore) < neutralThreshold) {
        applyNeutralCalibration(out, adjustedScore);
    }

    // Explainability
    out.supportLevels = supports;
    out.resistanceLevels = resistances;
    bd.rawScore = adjustedScore;
    out.breakdown = bd;

    // Rebuild same pipeline (for plan + SR tagging)
    auto close = extractCloseSeries(imagePath);
    auto smooth = smoothSeries(close, 3);
    auto swings = findSwings(smooth, 8);
    auto levels = findSupportResistance(swings);

    double lastN = smooth.empty() ? 0.5 : (double)smooth.back();

    // ✅ (1) Active S/R tagging
    tagActiveSR(out, lastN);

    // -------------------------------
    // 🔴 BUY TYPE 2 — Breakout Buy (missing)  ✅ added
    // -------------------------------
    {
        Series s;
        s.close.reserve(smooth.size());
        for (float v : smooth) s.close.push_back((double)v);

        // NOTE: ADDED HERE — extract volume aligned per column and attach to Series
        auto vol = extractVolumeSeries(imagePath);
        s.vol01.reserve(vol.size());
        for (float vv : vol) s.vol01.push_back((double)vv);

        bool breakout = false;
        double bScore = 0.0;
        double bLevel = 0.0;
        detectBreakoutBuy(s, out.resistanceLevels, out.breakdown.trendScore, breakout, bScore, bLevel);

        // Store in breakdown for UI/debug
        out.breakdown.breakoutBuy = breakout;
        out.breakdown.breakoutScore = bScore;
        out.breakdown.breakoutLevel = bLevel;
        if (breakout) out.breakdown.patterns.push_back("TYPE2_BREAKOUT");
    }

    // If neutral, normally force no-trade plan
    // 🔴 BUY TYPE 2 — Breakout Buy (missing)  ✅ added:
    // If we were neutral BUT breakout is true, upgrade to Bullish tradeable signal.
    if (out.label == "Neutral") {
        if (out.breakdown.breakoutBuy) {
            out.label = "Bullish";
            out.buyType = "TYPE2_BREAKOUT";

            // Boost confidence based on breakoutScore (kept conservative)
            out.confidence = clamp(65.0 + 25.0 * out.breakdown.breakoutScore, 0.0, 100.0);
            out.pBull = clamp01(0.65 + 0.25 * out.breakdown.breakoutScore);
            out.pBear = 1.0 - out.pBull;

            // Continue to build plan below (do NOT early-return)
        } else {
            out.signal = "NEUTRAL";
            suppressPlanIfNoTrade(out);
            return out;
        }
    }

    // Build plan
    buildTradePlan(out, smooth, levels);

    // Penalize confidence if too close to barrier
    double distToRes = nearestDistanceToLevels(lastN, out.resistanceLevels);
    double distToSup = nearestDistanceToLevels(lastN, out.supportLevels);

    const double near   = 0.015;
    const double closeT = 0.030;

    if (out.label == "Bullish") {
        if (distToRes < near) out.confidence *= 0.65;
        else if (distToRes < closeT) out.confidence *= 0.80;
    } else {
        if (distToSup < near) out.confidence *= 0.65;
        else if (distToSup < closeT) out.confidence *= 0.80;
    }
    out.confidence = clamp(out.confidence, 0.0, 100.0);

    // ✅ (2) R:R gating + plan suppression
    // HARD rule: if RR < 1.0 => no trade (prevents nonsense like 0.07 RR)
    if (out.riskRewardRatio < 1.0) {
        out.signal = "NEUTRAL";
        suppressPlanIfNoTrade(out);
    } else {
        // RR-aware signal gating
        double rr = out.riskRewardRatio;
        if (rr < 1.2) out.signal = "NEUTRAL";
        else if (rr < 1.8) out.signal = (out.label == "Bullish") ? "BUY" : "SELL";
        else {
            if (out.confidence >= 80.0) out.signal = (out.label == "Bullish") ? "STRONG_BUY" : "STRONG_SELL";
            else if (out.confidence >= 65.0) out.signal = (out.label == "Bullish") ? "BUY" : "SELL";
            else out.signal = "NEUTRAL";
        }

        if (out.confidence < confidenceThreshold_) out.signal = "NEUTRAL";

        // 🔴 BUY TYPE 2 — Breakout Buy (missing)  ✅ added:
        // If breakout triggered, allow BUY even if the general confidenceThreshold would suppress it,
        // but ONLY when RR is acceptable.
        if (out.breakdown.breakoutBuy && out.label == "Bullish" && out.riskRewardRatio >= 1.2) {
            if (out.signal == "NEUTRAL") {
                out.signal = (out.confidence >= 80.0) ? "STRONG_BUY" : "BUY";
            }
        }

        suppressPlanIfNoTrade(out);
    }

    // Convert to real prices if scale known
    if (hasScale) {
        // Convert plan levels
        out.stopLoss = normToReal(out.stopLoss, minPrice, maxPrice);
        out.target1  = normToReal(out.target1,  minPrice, maxPrice);
        out.target2  = normToReal(out.target2,  minPrice, maxPrice);

        // Convert active SR (but keep dist fields normalized)
        if (out.hasActiveSupport) out.activeSupport = normToReal(out.activeSupport, minPrice, maxPrice);
        if (out.hasActiveResistance) out.activeResistance = normToReal(out.activeResistance, minPrice, maxPrice);

        // Also convert stored level vectors so UI prints real levels
        for (double& s : out.supportLevels) s = normToReal(s, minPrice, maxPrice);
        for (double& r : out.resistanceLevels) r = normToReal(r, minPrice, maxPrice);

        // 🔴 BUY TYPE 2 — Breakout Buy (missing)  ✅ added
        // Convert breakoutLevel to real if we have a scale
        if (out.breakdown.breakoutLevel > 0.0) {
            out.breakdown.breakoutLevel = normToReal(out.breakdown.breakoutLevel, minPrice, maxPrice);
        }
    }

    return out;
}

// ✅ TF-aware overload: adjusts weights depending on TF
Prediction Predictor::predictWithTime(const std::string& imagePath,
                                      const std::string& timeStr,
                                      int tfMinutes) {
    // Save current weights
    Weights saved = w_;

    // Apply TF scaling to local weights then install
    double tW = w_.trend, mW = w_.momentum, rW = w_.reversal, srW = w_.sr;
    applyTimeframeWeights(tfMinutes, tW, mW, rW, srW);
    w_.trend = tW; w_.momentum = mW; w_.reversal = rW; w_.sr = srW;

    // predict normalized (no scale)
    Prediction out = predictWithTime(imagePath, timeStr, false, 0.0, 0.0);

    // restore
    w_ = saved;
    return out;
}

Prediction Predictor::predictAutoTF(const std::string& imagePath,
                                   const std::string& timeStr) {
    int tf = timeframeFromFilename(imagePath);
    if (tf > 0) return predictWithTime(imagePath, timeStr, tf);
    return predictWithTime(imagePath, timeStr, false, 0.0, 0.0);
}

// ✅ (3) Multi-TF bias locking
Prediction Predictor::predictMultiTimeframe(const std::string& path1m,
                                           const std::string& path5m,
                                           const std::string& path30m,
                                           const std::string& timeStr) {
    Prediction p1  = predictWithTime(path1m,  timeStr, 1);
    Prediction p5  = predictWithTime(path5m,  timeStr, 5);
    Prediction p30 = predictWithTime(path30m, timeStr, 30);

    int bullCount = 0;
    bullCount += (p1.label == "Bullish");
    bullCount += (p5.label == "Bullish");
    bullCount += (p30.label == "Bullish");

    // Anchor plan on 30m (stability)
    Prediction out = p30;
    out.tf1mBullish  = (p1.label == "Bullish");
    out.tf5mBullish  = (p5.label == "Bullish");
    out.tf30mBullish = (p30.label == "Bullish");
    out.confluence   = bullCount;

    // Fuse probabilities (weighted)
    double pBull = 0.2 * p1.pBull + 0.3 * p5.pBull + 0.5 * p30.pBull;
    out.pBull = clamp(pBull, 0.0, 1.0);
    out.pBear = 1.0 - out.pBull;
    out.confidence = 100.0 * std::max(out.pBull, out.pBear);

    // --- Bias locking rules ---
    // Bias = 30m if it's not neutral, otherwise 5m.
    std::string bias = (p30.label != "Neutral") ? p30.label : p5.label;

    // If bias is neutral => only trade if 1m and 5m agree AND RR good.
    bool agree15 = (p1.label == p5.label) && (p5.label != "Neutral");
    bool agreeWithBias5 = (p5.label == bias) && (bias != "Neutral");

    // Decide final label:
    if (bias == "Neutral") {
        out.label = agree15 ? p5.label : "Neutral";
    } else {
        // lock to bias unless 5m contradicts bias strongly
        out.label = agreeWithBias5 ? bias : "Neutral";
    }

    // Signal gating (requires confluence + bias alignment + RR)
    out.signal = "NEUTRAL";

    // If neutral final, suppress plan
    if (out.label == "Neutral") {
        suppressPlanIfNoTrade(out);
    } else {
        // Use the anchored plan's RR (from out which is p30-derived)
        double rr = out.riskRewardRatio;

        // Must have at least 2/3 confluence
        if (out.confluence < 2) {
            out.signal = "NEUTRAL";
        } else {
            // If bias came from 30m, require 5m aligns with 30m
            if (p30.label != "Neutral" && p5.label != p30.label) {
                out.signal = "NEUTRAL";
            } else if (p30.label == "Neutral") {
                // bias from 5m: require 1m agrees too
                if (!agree15) out.signal = "NEUTRAL";
                else out.signal = (rr >= 1.8 && out.confidence >= 80.0)
                                  ? ((out.label == "Bullish") ? "STRONG_BUY" : "STRONG_SELL")
                                  : ((rr >= 1.2) ? ((out.label == "Bullish") ? "BUY" : "SELL") : "NEUTRAL");
            } else {
                // normal case: 30m bias
                if (rr < 1.0) out.signal = "NEUTRAL";
                else if (rr < 1.2) out.signal = "NEUTRAL";
                else if (rr < 1.8) out.signal = (out.label == "Bullish") ? "BUY" : "SELL";
                else {
                    if (out.confidence >= 80.0) out.signal = (out.label == "Bullish") ? "STRONG_BUY" : "STRONG_SELL";
                    else if (out.confidence >= 65.0) out.signal = (out.label == "Bullish") ? "BUY" : "SELL";
                    else out.signal = "NEUTRAL";
                }
            }
            if (out.confidence < confidenceThreshold_) out.signal = "NEUTRAL";
        }

        suppressPlanIfNoTrade(out);
    }

    // Merge patterns for explainability
    out.breakdown.patterns.clear();
    out.breakdown.patterns.insert(out.breakdown.patterns.end(), p1.breakdown.patterns.begin(), p1.breakdown.patterns.end());
    out.breakdown.patterns.insert(out.breakdown.patterns.end(), p5.breakdown.patterns.begin(), p5.breakdown.patterns.end());
    out.breakdown.patterns.insert(out.breakdown.patterns.end(), p30.breakdown.patterns.begin(), p30.breakdown.patterns.end());

    return out;
}

// ---------- backtesting ----------
void Predictor::addBacktestResult(const BacktestResult& r) {
    history_.push_back(r);
}

void Predictor::saveBacktestCSV(const std::string& filename) const {
    std::ofstream out(filename);
    out << "timestamp,imagePath,timeframe,label,confidence,pBull,pBear,signal,stopLoss,target1,target2,rr,confluence\n";
    for (const auto& r : history_) {
        const auto& p = r.prediction;
        out << r.timestamp << ","
            << r.imagePath << ","
            << r.timeframeMinutes << ","
            << p.label << ","
            << p.confidence << ","
            << p.pBull << ","
            << p.pBear << ","
            << p.signal << ","
            << p.stopLoss << ","
            << p.target1 << ","
            << p.target2 << ","
            << p.riskRewardRatio << ","
            << p.confluence
            << "\n";
    }
}

std::map<std::string, double> Predictor::performanceMetrics() const {
    std::map<std::string, double> m;
    if (history_.empty()) {
        m["trades"] = 0;
        m["win_rate"] = 0;
        return m;
    }
    int wins = 0;
    int trades = 0;
    for (const auto& r : history_) {
        if (r.prediction.signal == "NEUTRAL") continue;
        trades++;
        if (r.wasCorrect) wins++;
    }
    m["trades"] = (double)trades;
    m["win_rate"] = (trades > 0) ? (100.0 * (double)wins / (double)trades) : 0.0;
    return m;
}
