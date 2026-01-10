#include "Predictor.h"
#include <cmath>

static float brightness(const sf::Color& c) {
    return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
}

Prediction predictTrend(const sf::Image& image) {
    const unsigned w = image.getSize().x;
    const unsigned h = image.getSize().y;

    std::vector<sf::Vector2f> points;

    for (unsigned x = 0; x < w; x += 4) {
        float weightedSum = 0.f;
        float totalWeight = 0.f;

        for (unsigned y = 0; y < h; y++) {
            float b = brightness(image.getPixel(x, y));
            if (b > 200) {   // bright pixels = chart line
                weightedSum += y * b;
                totalWeight += b;
            }
        }

        if (totalWeight > 0) {
            float avgY = weightedSum / totalWeight;
            points.emplace_back(static_cast<float>(x), avgY);
        }
    }

    Prediction p;
    p.trendPoints = points;

    if (points.size() < 2) {
        p.label = "Unknown";
        p.confidence = 0.f;
        return p;
    }

    float startY = points.front().y;
    float endY   = points.back().y;
    float diff   = startY - endY; // inverted y-axis

    p.label = (diff > 0) ? "Bullish" : "Bearish";
    p.confidence = std::min(std::abs(diff) / h * 3.f, 1.f);

    return p;
}


