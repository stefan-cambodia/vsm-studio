#include "vsm/audio/io/ZeroCrossing.h"
#include <cmath>

namespace vsm::audio::io {

int64_t nearestZeroCrossing(const std::function<bool(int64_t, float&, float&)>& frameAt,
                            int64_t frame, int64_t windowFrames) {
    if (windowFrames <= 0) return frame;
    auto valeur = [&](int64_t i, double& v) {
        float g = 0.0f, d = 0.0f;
        if (i < 0 || !frameAt(i, g, d)) return false;
        v = 0.5 * (static_cast<double>(g) + static_cast<double>(d));
        return true;
    };
    // EN S'ÉLOIGNANT DE L'INSTANT DEMANDÉ, des deux côtés à la fois : le
    // premier passage trouvé est le plus proche, et l'on n'a rien lu de plus.
    // Un passage est décelé entre i-1 et i quand les signes diffèrent, ou
    // qu'une des deux valeurs est nulle ; on rend celui des deux échantillons
    // dont la valeur est la plus petite -- c'est là que la coupe est muette.
    auto passageEn = [&](int64_t i, int64_t& trouve) {
        double a = 0.0, b = 0.0;
        if (!valeur(i - 1, a) || !valeur(i, b)) return false;
        if (a == 0.0) { trouve = i - 1; return true; }
        if (b == 0.0) { trouve = i; return true; }
        if ((a < 0.0) != (b < 0.0)) { trouve = std::fabs(a) <= std::fabs(b) ? i - 1 : i; return true; }
        return false;
    };
    int64_t trouve = frame;
    for (int64_t ecart = 0; ecart <= windowFrames; ++ecart) {
        if (passageEn(frame + ecart, trouve)) return trouve;
        if (ecart > 0 && passageEn(frame - ecart, trouve)) return trouve;
    }
    return frame;
}

} // namespace vsm::audio::io
