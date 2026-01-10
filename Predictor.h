#pragma once
#include <SFML/Graphics.hpp>
#include <vector>
#include <string>

struct Prediction {
    std::string label;
    float confidence;
    std::vector<sf::Vector2f> trendPoints; // NEW
};

Prediction predictTrend(const sf::Image& image);

