#pragma once
#include <string>
#include <vector>

struct Prediction {
    double pBull = 0.5;
    double pBear = 0.5;
    std::string label;     // "Bullish" / "Bearish"
    double confidence = 0; // 0..100
};

class Predictor {
public:
    // timeStr example: "09:25" (24-hour local market time)
    Prediction predictWithTime(const std::string& imagePath,
                               const std::string& timeStr);

private:
    // --- utilities ---
    static int timeToMinutes(const std::string& hhmm);
    static double sigmoid(double x);
    static double clamp(double x, double lo, double hi);

    // --- core scoring ---
    double computeRawScore(const std::string& imagePath) const;

    // time logic adjustments
    static double timeAdjustmentMultiplier(int minutes);
    static double openConfidenceDecayMultiplier(int minutes);

    // ========= NEW: image -> price path -> swings -> patterns =========
    struct SwingPoint {
        int idx;     // index in series
        float value; // normalized "price" (0..1), higher = higher price
        bool isHigh; // true=high, false=low
    };

    static bool nearColor(unsigned char r, unsigned char g, unsigned char b,
                          unsigned char tr, unsigned char tg, unsigned char tb,
                          int tol);

    // Extract a normalized price series (0..1) across X columns
    static std::vector<float> extractPriceSeriesFromScreenshot(const std::string& imagePath);

    static std::vector<float> smoothSeries(const std::vector<float>& s, int window);
    static std::vector<SwingPoint> findSwings(const std::vector<float>& s, int window);

    // Trend: HH/HL vs LH/LL
    static double trendScoreFromSwings(const std::vector<SwingPoint>& swings);

    // Momentum: slope + volatility
    static double momentumScore(const std::vector<float>& s);

    // Reversal cues
    static double doubleTopBottomScore(const std::vector<SwingPoint>& swings);
};




