#include <SFML/Graphics.hpp>
#include <iostream>
#include <sstream>
#include <iomanip>

#include "Predictor.h"

int main() {
    // -----------------------------
    // Window
    // -----------------------------
    sf::RenderWindow window(
        sf::VideoMode(900, 500),
        "C++ Stock Predictor"
    );
    window.setFramerateLimit(60);

    // -----------------------------
    // Font
    // -----------------------------
    sf::Font font;
    if (!font.loadFromFile("/Users/duca/CLionProjects/StockPredictGUI/assets/fonts/DejaVuSans.ttf")) {
        std::cerr << "Font missing\n";
        return 1;
    }



    // -----------------------------
    // Chart Image
    // -----------------------------
    sf::Texture chartTexture;
    if (!chartTexture.loadFromFile("../assets/charts/test.png")) {
        std::cerr << "Chart image missing\n";
        return 1;
    }


    sf::Sprite chart(chartTexture);
    chart.setScale(0.5f, 0.5f);
    chart.setPosition(450.f, 50.f);

    sf::Image chartImage = chartTexture.copyToImage();

    // -----------------------------
    // Text UI
    // -----------------------------
    sf::Text title;
    title.setFont(font);
    title.setCharacterSize(26);
    title.setString("Stock Trend Prediction (C++)");
    title.setPosition(20.f, 20.f);

    sf::Text instructions;
    instructions.setFont(font);
    instructions.setCharacterSize(18);
    instructions.setString("Press P to Predict\nPress ESC to Quit");
    instructions.setPosition(20.f, 70.f);

    sf::Text resultText;
    resultText.setFont(font);
    resultText.setCharacterSize(20);
    resultText.setString("Waiting for prediction...");
    resultText.setPosition(20.f, 150.f);

    // -----------------------------
    // Trend Line
    // -----------------------------
    sf::VertexArray trendLine(sf::LineStrip);
    bool hasPrediction = false;

    // -----------------------------
    // Main Loop
    // -----------------------------
    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed)
                window.close();

            if (event.type == sf::Event::KeyPressed) {
                if (event.key.code == sf::Keyboard::Escape)
                    window.close();

                if (event.key.code == sf::Keyboard::P) {
                    Prediction p = predictTrend(chartImage);

                    trendLine.clear();

                    for (const auto& pt : p.trendPoints) {
                        sf::Vector2f screenPoint;
                        screenPoint.x =
                            chart.getPosition().x + pt.x * chart.getScale().x;
                        screenPoint.y =
                            chart.getPosition().y + pt.y * chart.getScale().y;

                        trendLine.append(
                            sf::Vertex(screenPoint, sf::Color::Red)
                        );
                    }

                    hasPrediction = true;

                    std::ostringstream oss;
                    oss << "Prediction: " << p.label
                        << "\nConfidence: "
                        << std::fixed << std::setprecision(1)
                        << (p.confidence * 100.f) << "%";

                    resultText.setString(oss.str());
                }
            }
        }

        // -----------------------------
        // Draw
        // -----------------------------
        window.clear(sf::Color(25, 25, 25));

        window.draw(title);
        window.draw(instructions);
        window.draw(resultText);
        window.draw(chart);

        if (hasPrediction) {
            window.draw(trendLine);
        }

        window.display();
    }

    return 0;
}
