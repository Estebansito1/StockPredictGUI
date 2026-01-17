// ===============================
// File: main.cpp
// Clean SFML GUI (single TF + multi-TF option) + REAL-TIME CLOCK
// Keys:
//   1/5/3 load charts
//   P = predict current TF
//   M = multi-timeframe predict (test1/test5/test30)
//   ESC = quit
// ===============================
#include <SFML/Graphics.hpp>
#include <iostream>
#include <filesystem>
#include <vector>
#include <sstream>
#include <iomanip>
#include <regex>
#include <chrono>
#include <ctime>

#include "Predictor.h"

static std::string findAsset(const std::string& relPath) {
    namespace fs = std::filesystem;
    std::vector<fs::path> bases = {
        fs::current_path(),
        fs::current_path() / "..",
        fs::current_path() / ".." / "..",
        fs::current_path() / ".." / ".." / ".."
    };
    for (const auto& base : bases) {
        fs::path candidate = base / relPath;
        if (fs::exists(candidate)) return candidate.string();
    }
    return "";
}

static bool loadChart(const std::string& path, sf::Texture& tex, sf::Sprite& sprite) {
    if (!tex.loadFromFile(path)) return false;
    sprite.setTexture(tex, true);
    sprite.setScale(0.5f, 0.5f);
    sprite.setPosition(450.f, 50.f);
    return true;
}

static std::string levelsToString(const std::vector<double>& lv) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < lv.size(); i++) {
        oss << std::fixed << std::setprecision(3) << lv[i];
        if (i + 1 < lv.size()) oss << ", ";
    }
    oss << "]";
    return oss.str();
}

struct ChartMeta {
    int tfMin = -1;                 // 1, 5, 30
    bool hasScale = false;
    double minPrice = 0.0;          // bottom of chart
    double maxPrice = 0.0;          // top of chart
};

// Supports: XRP_1m_2.0325_2.0697.png  OR  test1.png (no scale)
static ChartMeta parseMetaFromFilename(const std::string& imagePath) {
    ChartMeta meta;

    // timeframe
    if (imagePath.find("test30") != std::string::npos || imagePath.find("_30m") != std::string::npos) meta.tfMin = 30;
    else if (imagePath.find("test5") != std::string::npos || imagePath.find("_5m") != std::string::npos) meta.tfMin = 5;
    else if (imagePath.find("test1") != std::string::npos || imagePath.find("_1m") != std::string::npos) meta.tfMin = 1;

    // scale parse: ..._MIN_MAX.png
    std::regex re(R"(_([0-9]+(?:\.[0-9]+)?)_([0-9]+(?:\.[0-9]+)?)\.png$)");
    std::smatch m;
    if (std::regex_search(imagePath, m, re) && m.size() == 3) {
        meta.minPrice = std::stod(m[1].str());
        meta.maxPrice = std::stod(m[2].str());
        if (meta.maxPrice > meta.minPrice) meta.hasScale = true;
    }

    return meta;
}

static std::string nowHHMM() {
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t t = system_clock::to_time_t(now);

    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &t);
#else
    local = *std::localtime(&t);
#endif

    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << local.tm_hour
        << ":"
        << std::setw(2) << std::setfill('0') << local.tm_min;
    return oss.str();
}

int main() {
    sf::RenderWindow window(sf::VideoMode(1000, 750), "C++ Stock Predictor");
    window.setFramerateLimit(60);

    sf::Font font;
    std::string fontPath = findAsset("assets/fonts/DejaVuSans.ttf");
    if (fontPath.empty() || !font.loadFromFile(fontPath)) {
        std::cerr << "Failed to load font. Put a .ttf at assets/fonts/DejaVuSans.ttf\n";
        return 1;
    }

    std::string chartPath = findAsset("assets/charts/test1.png");

    if (chartPath.empty()) {
        std::cerr << "Missing assets/charts/test1.png\n";

        return 1;
    }

    sf::Texture chartTexture;
    sf::Sprite  chartSprite;
    if (!loadChart(chartPath, chartTexture, chartSprite)) {
        std::cerr << "Failed to load chart image\n";
        return 1;
    }

    Predictor predictor;
    predictor.setConfidenceThreshold(60.0);

    ChartMeta meta = parseMetaFromFilename(chartPath);
    int currentTF = meta.tfMin;

    // Real-time clock string (auto-updated)
    std::string currentTimeStr = nowHHMM();

    sf::Text title("Stock Trend Prediction (C++)", font, 30);
    title.setPosition(20.f, 20.f);

    sf::Text instructions(
        "Hotkeys:\n"
        "  P = Predict (current chart)\n"
        "  M = Multi-TF Predict (test1/test5/test30)\n"
        "  1 = Load test1 (1m)\n"
        "  5 = Load test5 (5m)\n"
        "  3 = Load test30 (30m)\n"
        "  ESC = Quit",
        font, 13
    );
    instructions.setPosition(20.f, 80.f);
    instructions.setLineSpacing(1.20f);

    sf::Text statusText("", font, 13);
    statusText.setPosition(20.f, 250.f);
    statusText.setLineSpacing(1.20f);

    sf::Text resultText("Waiting for prediction...", font, 13);
    resultText.setPosition(20.f, 320.f);
    resultText.setLineSpacing(1.20f);

    auto updateStatus = [&]() {
        std::ostringstream oss;
        oss << "Loaded: " << std::filesystem::path(chartPath).filename().string()
            << "\nTimeframe: " << currentTF << "m"
            << "\nLocal time: " << currentTimeStr;

        if (meta.hasScale) {
            oss << "\nScale: [" << meta.minPrice << " .. " << meta.maxPrice << "]";
        } else {
            oss << "\nScale: (normalized 0..1)  <-- add _min_max to filename";
        }

        statusText.setString(oss.str());
    };

    updateStatus();

    auto switchChart = [&](const std::string& rel) {
        std::string newPath = findAsset(rel);
        if (newPath.empty()) {
            resultText.setString("Error: missing " + rel);
            return;
        }
        if (!loadChart(newPath, chartTexture, chartSprite)) {
            resultText.setString("Error: failed loading " + rel);
            return;
        }
        chartPath = newPath;
        meta = parseMetaFromFilename(chartPath);
        currentTF = meta.tfMin;
        updateStatus();
        resultText.setString("Switched chart. Press P to Predict.");
    };

    auto renderPrediction = [&](const Prediction& pred, const std::string& header) {
        std::ostringstream oss;
        oss << header
            << "\nPrediction: " << pred.label
            << " | Signal: " << pred.signal
            << "\nBullish: " << std::fixed << std::setprecision(1) << (pred.pBull * 100.0) << "%"
            << "  Bearish: " << std::fixed << std::setprecision(1) << (pred.pBear * 100.0) << "%"
            << "\nStrength: " << std::fixed << std::setprecision(1) << pred.confidence << "%";

        // Only show a plan when it's meaningful (not Neutral and rr>0)
        const bool hasPlan = (pred.label != "Neutral") && (pred.riskRewardRatio > 0.0);

        if (hasPlan) {
            if (meta.hasScale) {
                oss << "\n\nPlan (real price):"
                    << "\n  Stop:   " << std::fixed << std::setprecision(4) << pred.stopLoss
                    << "\n  T1:     " << std::fixed << std::setprecision(4) << pred.target1
                    << "\n  T2:     " << std::fixed << std::setprecision(4) << pred.target2
                    << "\n  R:R:    " << std::fixed << std::setprecision(2) << pred.riskRewardRatio;
            } else {
                oss << "\n\nPlan (normalized 0..1):"
                    << "\n  Stop:   " << std::fixed << std::setprecision(3) << pred.stopLoss
                    << "\n  T1:     " << std::fixed << std::setprecision(3) << pred.target1
                    << "\n  T2:     " << std::fixed << std::setprecision(3) << pred.target2
                    << "\n  R:R:    " << std::fixed << std::setprecision(2) << pred.riskRewardRatio;
            }
        } else {
            oss << "\n\nPlan: (neutral / no-trade)";
        }

        oss << "\n\nLevels:"
            << "\n  Support:    " << levelsToString(pred.supportLevels)
            << "\n  Resistance: " << levelsToString(pred.resistanceLevels);

        oss << "\n\nBreakdown:"
            << "\n  trend:    " << std::fixed << std::setprecision(2) << pred.breakdown.trendScore
            << "\n  momentum: " << std::fixed << std::setprecision(2) << pred.breakdown.momentumScore
            << "\n  reversal: " << std::fixed << std::setprecision(2) << pred.breakdown.reversalScore
            << "\n  sr:       " << std::fixed << std::setprecision(2) << pred.breakdown.srScore
            << "\n  raw:      " << std::fixed << std::setprecision(2) << pred.breakdown.rawScore;

        if (!pred.breakdown.patterns.empty()) {
            oss << "\n  patterns: ";
            for (size_t i = 0; i < pred.breakdown.patterns.size(); i++) {
                oss << pred.breakdown.patterns[i];
                if (i + 1 < pred.breakdown.patterns.size()) oss << " | ";
            }
        }

        if (pred.confluence > 0) {
            oss << "\n\nConfluence: " << pred.confluence << "/3"
                << " (1m=" << (pred.tf1mBullish ? "Bull" : "Bear")
                << ", 5m=" << (pred.tf5mBullish ? "Bull" : "Bear")
                << ", 30m=" << (pred.tf30mBullish ? "Bull" : "Bear") << ")";
        }

        resultText.setString(oss.str());
    };

    // Clock to refresh time string
    sf::Clock realtimeClock;
    std::string lastHHMM = currentTimeStr;

    while (window.isOpen()) {
        // update time about once per second
        if (realtimeClock.getElapsedTime().asSeconds() >= 1.0f) {
            realtimeClock.restart();
            currentTimeStr = nowHHMM();
            if (currentTimeStr != lastHHMM) {
                lastHHMM = currentTimeStr;
                updateStatus();
            }
        }

        sf::Event event{};
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) window.close();

            if (event.type == sf::Event::KeyPressed) {
                if (event.key.code == sf::Keyboard::Escape) window.close();

                if (event.key.code == sf::Keyboard::Num1) switchChart("assets/charts/test1.png");
                if (event.key.code == sf::Keyboard::Num5) switchChart("assets/charts/test5.png");
                if (event.key.code == sf::Keyboard::Num3) switchChart("assets/charts/test30.png");


                if (event.key.code == sf::Keyboard::P) {
                    try {
                        Prediction pred = predictor.predictWithTime(
                            chartPath, currentTimeStr,
                            meta.hasScale, meta.minPrice, meta.maxPrice
                        );
                        renderPrediction(pred, "Single-timeframe");
                    } catch (const std::exception& e) {
                        resultText.setString(std::string("Error: ") + e.what());
                    }
                }

                if (event.key.code == sf::Keyboard::M) {
                    try {
                        std::string p1  = findAsset("assets/charts/test1.png");
                        std::string p5  = findAsset("assets/charts/test5.png");
                        std::string p30 = findAsset("assets/charts/test30.png");


                        if (p1.empty() || p5.empty() || p30.empty()) {
                            resultText.setString("Error: missing test1/test5/test30 images");
                        } else {
                            Prediction pred = predictor.predictMultiTimeframe(p1, p5, p30, currentTimeStr);
                            renderPrediction(pred, "Multi-timeframe (1m/5m/30m)");
                        }
                    } catch (const std::exception& e) {
                        resultText.setString(std::string("Error: ") + e.what());
                    }
                }
            }
        }

        window.clear(sf::Color(25, 25, 25));
        window.draw(title);
        window.draw(instructions);
        window.draw(statusText);
        window.draw(resultText);
        window.draw(chartSprite);
        window.display();
    }

    return 0;
}









