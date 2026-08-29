#pragma once
#include <cmath>
#include <cstddef>
#include <vector>

namespace vsm::audio::engine {

/// LA MESURE DE LA LATENCE PAR BOUCLE PHYSIQUE (D3.6).
///
/// CE QU'ON MESURE, ET POURQUOI ON NE PEUT PAS LE DEVINER. Entre l'instant où
/// le moteur remet un bloc au pilote et l'instant où ce bloc revient par
/// l'entrée -- câble branché de la sortie vers l'entrée, ou micro devant un
/// haut-parleur -- il s'écoule un aller-retour : convertisseurs, tampons du
/// pilote, tampons du système. Les pilotes ANNONCENT un chiffre ; il est
/// souvent faux, presque toujours sous-estimé, et jamais vérifiable de
/// l'intérieur. Le seul moyen de le connaître est de l'entendre revenir.
///
/// CE QUI EST ENVOYÉ. Un balayage de fréquence (« chirp ») court plutôt qu'un
/// clic. Un clic est simple à fabriquer mais sa détection repose sur un seuil,
/// donc sur le bruit ambiant : dans une pièce, ou sur une entrée à fort gain,
/// on trouve son seuil avant son clic. Un balayage, lui, ne ressemble à rien
/// d'autre : sa corrélation avec lui-même est un pic étroit et sa corrélation
/// avec du bruit est plate. On mesure donc une DATE, pas une amplitude.
///
/// CETTE CLASSE EST PURE et ne connaît ni carte son ni thread : on lui donne un
/// signal capturé, elle rend un décalage en échantillons. C'est ce qui permet
/// de la tester sans matériel, en fabriquant la capture qu'on aurait eue.
class LatencyProbe {
public:
    /// Durée du balayage. Assez long pour être reconnaissable, assez court pour
    /// que la mesure ne dure pas : 30 ms couvrent largement les latences des
    /// cartes usuelles sans que le son émis soit désagréable.
    static constexpr double kProbeSeconds = 0.030;
    /// Ce qu'on écoute après l'émission. Une latence d'aller-retour dépasse
    /// rarement 100 ms ; une demi-seconde laisse de la marge même à une carte
    /// USB lente réglée sur de gros tampons.
    static constexpr double kListenSeconds = 0.5;

    /// Le signal à émettre : un balayage de 200 Hz à 6 kHz, fenêtré aux deux
    /// bouts pour ne pas claquer.
    static std::vector<float> makeProbe(double sampleRate) {
        const int n = static_cast<int>(sampleRate * kProbeSeconds);
        std::vector<float> signal(static_cast<size_t>(n > 0 ? n : 1), 0.0f);
        if (n <= 0) return signal;
        constexpr double f0 = 200.0, f1 = 6000.0;
        const double duree = static_cast<double>(n) / sampleRate;
        double phase = 0.0;
        for (int i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / sampleRate;
            const double f = f0 + (f1 - f0) * (t / duree);
            phase += 2.0 * M_PI * f / sampleRate;
            // Fenêtre de Hann : sans elle, les extrémités claquent, et un clac
            // est précisément ce qu'on cherchait à éviter en n'employant pas un
            // clic.
            const double fenetre = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (n - 1));
            signal[static_cast<size_t>(i)] = static_cast<float>(std::sin(phase) * fenetre * 0.5);
        }
        return signal;
    }

    struct Resultat {
        /// Décalage trouvé, en échantillons depuis le début de la capture.
        int decalageEchantillons = -1;
        /// Rapport entre le pic de corrélation et la moyenne des autres. Une
        /// valeur basse veut dire qu'on a trouvé du bruit, pas le balayage --
        /// et il vaut mieux le dire que publier un chiffre inventé.
        double nettete = 0.0;
        bool trouve() const { return decalageEchantillons >= 0; }
    };

    /// Cherche `sonde` dans `capture` par corrélation croisée.
    ///
    /// La corrélation est calculée en direct, sans transformée de Fourier : sur
    /// une demi-seconde de capture et 30 ms de sonde, cela fait quelques
    /// millions de multiplications, soit quelques millisecondes -- une fois, sur
    /// le thread de l'interface, à la demande de l'utilisateur. Une FFT irait
    /// plus vite et demanderait cent lignes de plus qu'il faudrait tester.
    static Resultat detecter(const std::vector<float>& capture, const std::vector<float>& sonde) {
        Resultat resultat;
        const int n = static_cast<int>(capture.size());
        const int m = static_cast<int>(sonde.size());
        if (m <= 0 || n < m) return resultat;

        double energieSonde = 0.0;
        for (float e : sonde) energieSonde += static_cast<double>(e) * e;
        if (energieSonde <= 0.0) return resultat;

        double meilleur = 0.0;
        int meilleurIndex = -1;
        double somme = 0.0;
        int comptes = 0;
        for (int d = 0; d + m <= n; ++d) {
            double produit = 0.0;
            for (int i = 0; i < m; ++i)
                produit += static_cast<double>(capture[static_cast<size_t>(d + i)]) * sonde[static_cast<size_t>(i)];
            const double valeur = std::abs(produit);
            somme += valeur;
            ++comptes;
            if (valeur > meilleur) { meilleur = valeur; meilleurIndex = d; }
        }
        if (meilleurIndex < 0 || comptes == 0) return resultat;

        const double moyenne = somme / static_cast<double>(comptes);
        resultat.decalageEchantillons = meilleurIndex;
        // NETTETÉ : le pic rapporté à ce que donne le reste du signal. Sur du
        // bruit seul, le « meilleur » vaut à peine plus que la moyenne et le
        // rapport tourne autour de 2 ou 3 ; sur un vrai retour, il dépasse
        // largement 10.
        resultat.nettete = moyenne > 0.0 ? meilleur / moyenne : 0.0;
        return resultat;
    }
};

} // namespace vsm::audio::engine
