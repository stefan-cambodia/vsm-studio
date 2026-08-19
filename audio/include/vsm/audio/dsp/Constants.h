#pragma once

namespace vsm::audio::dsp {

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTwoPi = 6.28318530717958647692;

/// Test d'IDENTITÉ exacte entre deux flottants (pas d'égalité "à epsilon
/// près"). Écrit avec deux comparaisons d'ordre plutôt qu'avec `==` pour
/// rester propre sous `-Wfloat-equal`, que ce projet active dans ses
/// vérifications strictes : ici, comparer bit à bit est précisément
/// l'intention, pas une maladresse.
///
/// Sert aux setters de filtres : quand un plugin réécrit la MÊME fréquence de
/// coupure à chaque échantillon (cas fréquent d'un paramètre non modulé), il
/// est inutile de recalculer les coefficients -- ce qui coûte un std::tan.
/// Sauter ce recalcul est exact : mêmes entrées, mêmes coefficients.
inline bool isSameValue(float a, float b) { return !(a < b) && !(b < a); }

} // namespace vsm::audio::dsp
