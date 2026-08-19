#pragma once
#include <string>

namespace vsm::audio::plugin {

/// Charger un fichier dans une machine à échantillons.
///
/// POURQUOI UNE INTERFACE À PART : `ISynthPlugin` ne transporte que des
/// flottants (`setParameter`) -- c'est ce qui rend l'automation, le MIDI Learn
/// et l'interop CLAP uniformes pour toutes les machines. Un chemin de fichier
/// n'entre pas dans ce moule, et l'y forcer (index de banque numéroté, table
/// globale de chemins) créerait une indirection fragile pour toutes les
/// machines afin de servir une seule famille.
///
/// Les machines concernées implémentent donc CETTE interface EN PLUS. Les
/// appelants qui en ont besoin (interface, import de projet, service de rendu)
/// font un `dynamic_cast` ; le moteur, lui, continue de ne voir qu'un
/// `ISynthPlugin` et n'a rien à savoir de tout ceci.
///
/// RÈGLE : ces méthodes s'appellent depuis le thread UI ou un thread de
/// chargement, JAMAIS depuis le thread audio -- elles lisent des fichiers.
/// L'échantillon chargé est publié par échange atomique, comme les chaînes
/// d'effets du ProcessGraph.
class ISampleLoader {
public:
    virtual ~ISampleLoader() = default;

    /// Charge un fichier WAV dans l'emplacement `slot`. Renvoie false et
    /// remplit `outError` en cas d'échec -- un échantillon manquant est
    /// SIGNALÉ, jamais remplacé par un autre son.
    virtual bool loadSample(int slot, const std::string& path, std::string& outError) = 0;

    /// Vide un emplacement. L'emplacement devient silencieux, il ne repart pas
    /// sur un son par défaut.
    virtual void clearSample(int slot) = 0;

    /// Chemin de l'échantillon chargé, vide si l'emplacement est libre.
    virtual std::string samplePath(int slot) const = 0;

    virtual int slotCount() const = 0;
};

} // namespace vsm::audio::plugin
