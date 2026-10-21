#include "mb/game/Profile.h"

#include <algorithm>
#include <functional>
#include <iterator>

namespace mb {

bool Profile::qualifies(int score) const {
    return score > 0 && (highScores.size() < kMaxHighScores || score > highScores.back().score);
}

int Profile::addHighScore(std::string name, int score) {
    if (!qualifies(score)) {
        return -1;
    }
    const auto place = std::ranges::upper_bound(highScores, score, std::greater<>(), &HighScore::score);
    const auto index = static_cast<int>(std::distance(highScores.begin(), place));
    highScores.insert(place, HighScore{std::move(name), score});
    if (highScores.size() > kMaxHighScores) {
        highScores.pop_back();
    }
    return index;
}

}  // namespace mb
