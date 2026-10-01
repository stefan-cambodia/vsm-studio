#pragma once

namespace vsm::audio::dsp {

/// La position de lecture d'une ligne à retard circulaire, ramenée DANS le
/// tampon : [0, size).
///
/// D526 (30/09/2026). Le chorus et le flanger calculaient `écriture - retard`
/// en simple précision puis ajoutaient `size` tant que le résultat était
/// négatif. Quand il est négatif d'une fraction infime -- le retard croise un
/// entier au moment où l'écriture y passe --, `position + size` s'ARRONDIT à
/// `size` exactement, et la lecture se faisait une case APRÈS la fin du tampon.
/// Mesuré : un échantillon à 3,9e28 dans un rendu de 26 s à travers l'insert
/// chorus (1,751 Hz, 2,4 ms), et dans le flanger la valeur lue repartait dans
/// la ligne par la réinjection.
///
/// `size` est la même case que 0 : on l'y ramène. Tout autre cas rend le
/// nombre d'avant, au bit près -- les empreintes des effets ne bougent pas.
inline float wrapReadPosition(float readPos, float size) {
    while (readPos < 0.0f) readPos += size;
    if (readPos >= size) readPos -= size;
    return readPos;
}

} // namespace vsm::audio::dsp
