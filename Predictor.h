// ===============================
// File: Predictor.h
// ===============================
#pragma once
#include <string>
#include <vector>
#include <map>

struct FeatureBreakdown {
    double trendScore = 0.0;
    double momentumScore = 0.0;
    double reversalScore = 0.0;
    double srScore = 0.0;
    double rawScore = 0.0;
    std::vector<std::string> patterns; // e.g. "HH_HL", "DOUBLE_BOTTOM"

    // 🔴 BUY TYPE 2 — Breakout Buy (missing)  ✅ added
    bool breakoutBuy = false;          // did we detect a breakout setup?
    double breakoutScore = 0.0;        // 0..1 confidence of breakout condition itself
    double breakoutLevel = 0.0;        // normalized resistance used for breakout (0..1)
};

struct Prediction {
    double pBull = 0.5;
    double pBear = 0.5;
    std::string label;        // "Bullish"/"Bearish"/"Neutral"
    double confidence = 50.0; // 0..100 (signal strength, not statistical confidence)

    // Trading-friendly fields (NORMALIZED 0..1 if hasScale=false; otherwise real price)
    std::string signal; // "STRONG_BUY","BUY","NEUTRAL","SELL","STRONG_SELL"
    double stopLoss = 0.0;
    double target1 = 0.0;
    double target2 = 0.0;
    double riskRewardRatio = 0.0;

    // Which setup fired (optional but super useful for debugging / UI)
    // examples: "", "TYPE2_BREAKOUT"
    std::string buyType;

    // Multi-timeframe confluence
    bool tf1mBullish = false;
    bool tf5mBullish = false;
    bool tf30mBullish = false;
    int confluence = 0; // 0..3

    // Explainability
    std::vector<double> supportLevels;    // normalized 0..1
    std::vector<double> resistanceLevels; // normalized 0..1
    FeatureBreakdown breakdown;

    // ✅ ACTIVE S/R tagging (normalized if hasScale=false; real price if hasScale=true)
    bool hasActiveSupport = false;
    bool hasActiveResistance = false;
    double activeSupport = 0.0;
    double activeResistance = 0.0;

    // distances in normalized space (0..1). If hasScale=true, these are still normalized distances
    // (so they compare across scales).
    double distToSupport = 1.0;
    double distToResistance = 1.0;
};

struct BacktestResult {
    std::string timestamp; // free-form
    std::string imagePath;
    int timeframeMinutes = -1;
    Prediction prediction;

    double entryPrice = 0.0;
    double exitPrice = 0.0;
    double pnl = 0.0;
    bool wasCorrect = false;
    int barsHeld = 0;
};

class Predictor {
public:
    Predictor();

    // Single-image prediction (timeStr adjusts confidence during open/pre/after-hours)
    Prediction predictWithTime(const std::string& imagePath,
                               const std::string& timeStr,
                               bool hasScale,
                               double minPrice,
                               double maxPrice);

    // ✅ TF-aware convenience overload
    Prediction predictWithTime(const std::string& imagePath,
                               const std::string& timeStr,
                               int tfMinutes);

    static double normToReal(double n, double minP, double maxP) {
        return minP + n * (maxP - minP);
    }
    static double realToNorm(double p, double minP, double maxP) {
        return (maxP <= minP) ? 0.5 : (p - minP) / (maxP - minP);
    }

    // Convenience: parse TF from filename like test1/test5/test30
    Prediction predictAutoTF(const std::string& imagePath,
                             const std::string& timeStr);

    // Multi-timeframe: combine 1m/5m/30m into a single decision
    Prediction predictMultiTimeframe(const std::string& path1m,
                                     const std::string& path5m,
                                     const std::string& path30m,
                                     const std::string& timeStr);

    // Backtesting hooks (simple CSV)
    void addBacktestResult(const BacktestResult& r);
    void saveBacktestCSV(const std::string& filename) const;
    std::map<std::string, double> performanceMetrics() const;

    // Configuration
    void setCandleColors(unsigned char bullR, unsigned char bullG, unsigned char bullB,
                         unsigned char bearR, unsigned char bearG, unsigned char bearB,
                         int tolerance);
    void setWeights(double trendW, double momentumW, double reversalW, double srW);
    void setConfidenceThreshold(double threshold);

private:
    struct ColorConfig {
        unsigned char bullR = 40,  bullG = 220, bullB = 140;
        unsigned char bearR = 220, bearG = 60,  bearB = 220;
        int tolerance = 45;
    } color_;

    struct Weights {
        double trend = 1.6;
        double momentum = 0.35;
        double reversal = 1.2;
        double sr = 0.6;
    } w_;

    double confidenceThreshold_ = 60.0;
    std::vector<BacktestResult> history_;

    struct SwingPoint {
        int idx = 0;
        float value = 0.f; // normalized 0..1
        bool isHigh = false;
    };

    struct Level {
        float price = 0.f;   // normalized 0..1
        int touches = 0;
        float strength = 0.f;
        bool isSupport = false;
    };

    // -------------------------------
    // 🔴 BUY TYPE 2 — Breakout Buy (missing)  ✅ added
    // -------------------------------
    struct Series {
        std::vector<double> open;   // normalized 0..1
        std::vector<double> high;   // normalized 0..1
        std::vector<double> low;    // normalized 0..1
        std::vector<double> close;  // normalized 0..1
        std::vector<double> vol01;  // optional normalized 0..1; can be empty
    };

    void detectBreakoutBuy(const Series& s,
                           const std::vector<double>& resistanceLevels,
                           double trendScore,
                           bool& outBreakout,
                           double& outScore,
                           double& outR) const;

    // Time helpers
    static int timeToMinutes(const std::string& hhmm);
    static double clamp(double x, double lo, double hi);
    static double sigmoid(double x);
    static double timeAdjustmentMultiplier(int minutes);
    static double openConfidenceDecayMultiplier(int minutes);

    // Image helpers
    static bool nearColor(unsigned char r, unsigned char g, unsigned char b,
                          unsigned char tr, unsigned char tg, unsigned char tb,
                          int tol);

    std::vector<float> extractCloseSeries(const std::string& imagePath) const;
    // NOTE: ADDED HERE — volume extraction (normalized 0..1)
    std::vector<float> extractVolumeSeries(const std::string& imagePath) const;

    static std::vector<float> smoothSeries(const std::vector<float>& s, int window);
    static std::vector<SwingPoint> findSwings(const std::vector<float>& s, int window);

    // Features
    static double trendScoreFromSwings(const std::vector<SwingPoint>& swings, FeatureBreakdown& bd);
    static double momentumScoreFromSeries(const std::vector<float>& s);
    static double doubleTopBottomScore(const std::vector<SwingPoint>& swings, FeatureBreakdown& bd);
    static std::vector<Level> findSupportResistance(const std::vector<SwingPoint>& swings);
    static double srScoreFromLevels(const std::vector<float>& series,
                                    const std::vector<Level>& levels,
                                    FeatureBreakdown& bd);

    // Risk plan + signal
    static void buildTradePlan(Prediction& out,
                               const std::vector<float>& series,
                               const std::vector<Level>& levels);

    static std::string signalFromConfidence(double conf, const std::string& label);

    // Core scoring
    double computeRawScore(const std::string& imagePath, FeatureBreakdown& bd,
                           std::vector<double>& supports, std::vector<double>& resistances) const;

    // convenience
    double computeRawScore(const std::string& imagePath) const;

    // Timeframe handling
    static int timeframeFromFilename(const std::string& path);
    static void applyTimeframeWeights(int tfMinutes, double& tW, double& mW, double& rW, double& srW);
};


