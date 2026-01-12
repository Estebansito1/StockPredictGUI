#include <SFML/Graphics.hpp>
#include <iostream>
#include <filesystem>
#include <vector>
#include <sstream>
#include <iomanip>

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

static int timeframeFromFilename(const std::string& imagePath) {
    // expects ...test1.png / test5.png / test30.png
    if (imagePath.find("test30") != std::string::npos) return 30;
    if (imagePath.find("test5")  != std::string::npos) return 5;
    if (imagePath.find("test1")  != std::string::npos) return 1;
    return -1;
}

int main() {
    sf::RenderWindow window(sf::VideoMode(900, 500), "C++ Stock Predictor");
    window.setFramerateLimit(60);

    // Font
    sf::Font font;
    std::string fontPath = findAsset("assets/fonts/DejaVuSans.ttf");
    if (fontPath.empty() || !font.loadFromFile(fontPath)) {
        std::cerr << "Failed to load font\n";
        return 1;
    }

    // Default chart = test1.png
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

    // Predictor
    Predictor predictor;

    // ====== runtime state ======
    int currentTF = timeframeFromFilename(chartPath);   // 1,5,30
    std::string currentTimeStr = "09:25";               // you can change this later

    // UI text (left panel)
    sf::Text title("Stock Trend Prediction (C++)", font, 34);
    title.setPosition(20.f, 20.f);

    sf::Text instructions(
        "Hotkeys:\n"
        "  P = Predict\n"
        "  1 = Load test1 (1m)\n"
        "  5 = Load test5 (5m)\n"
        "  3 = Load test30 (30m)\n"
        "  ESC = Quit",
        font, 18
    );
    instructions.setPosition(20.f, 80.f);
    instructions.setLineSpacing(1.25f);

    sf::Text statusText("", font, 18);
    statusText.setPosition(20.f, 230.f);
    statusText.setLineSpacing(1.25f);

    sf::Text resultText("Waiting for prediction...", font, 22);
    resultText.setPosition(20.f, 300.f);
    resultText.setLineSpacing(1.25f);

    auto updateStatus = [&]() {
        std::ostringstream oss;
        oss << "Loaded: " << std::filesystem::path(chartPath).filename().string()
            << "\nTimeframe: " << currentTF << "m"
            << "\nMarket time: " << currentTimeStr;
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
        currentTF = timeframeFromFilename(chartPath);
        updateStatus();
        resultText.setString("Switched chart. Press P to Predict.");
    };

    while (window.isOpen()) {
        sf::Event event{};
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) window.close();

            if (event.type == sf::Event::KeyPressed) {
                if (event.key.code == sf::Keyboard::Escape) window.close();

                // Switch images
                if (event.key.code == sf::Keyboard::Num1) {
                    switchChart("assets/charts/test1.png");
                }
                if (event.key.code == sf::Keyboard::Num5) {
                    switchChart("assets/charts/test5.png");
                }
                if (event.key.code == sf::Keyboard::Num3) {
                    switchChart("assets/charts/test30.png");
                }

                // Predict
                if (event.key.code == sf::Keyboard::P) {
                    try {
                        Prediction pred = predictor.predictWithTime(chartPath, currentTimeStr);

                        std::ostringstream oss;
                        oss << "Prediction: " << pred.label
                            << "\nBullish: " << std::fixed << std::setprecision(1) << (pred.pBull * 100.0) << "%"
                            << "\nBearish: " << std::fixed << std::setprecision(1) << (pred.pBear * 100.0) << "%"
                            << "\nConfidence: " << std::fixed << std::setprecision(1) << pred.confidence << "%";

                        resultText.setString(oss.str());
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



