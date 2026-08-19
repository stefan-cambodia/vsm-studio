#pragma once

namespace vsm::audio::plugin {

/// Doit être appelée UNE FOIS, tôt dans main() (avant toute création de
/// plugin), pour garantir que les machines intégrées sont bien liées dans
/// l'exécutable final ET enregistrées auprès de PluginRegistry.
///
/// Pourquoi cette étape est nécessaire malgré l'auto-enregistrement par
/// static-init de VSM_REGISTER_SYNTH_PLUGIN (voir PluginRegistry.h) : quand
/// un plugin est compilé dans une bibliothèque STATIQUE (.a/.lib) et
/// qu'aucun symbole de son .cpp n'est référencé ailleurs dans le
/// programme, l'éditeur de liens élimine purement et simplement toute la
/// traduction unit de l'exécutable final -- le registrar statique
/// disparaît avec elle, SANS erreur de compilation ni de link (juste un
/// PluginRegistry vide au runtime, découvert seulement en testant). C'est
/// vrai même si le plugin est compilé directement comme source de
/// vsm_audio plutôt que dans une bibliothèque "plugins" séparée : vsm_audio
/// elle-même reste une bibliothèque statique du point de vue de tout
/// exécutable qui la lie.
///
/// registerBuiltInPlugins() référence explicitement chaque plugin intégré
/// (en pratique : construit puis détruit immédiatement une instance) pour
/// forcer l'inclusion de sa traduction unit dans l'exécutable. C'est le
/// SEUL fichier à modifier pour ajouter un plugin Phase 3+ à cette liste --
/// AudioEngine/ProcessGraph n'ont eux jamais besoin d'être touchés.
void registerBuiltInPlugins();

} // namespace vsm::audio::plugin
