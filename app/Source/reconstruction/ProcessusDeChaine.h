#pragma once
// D339 : UN PROCESSUS ENFANT DANS SON PROPRE GROUPE, LU SANS BLOQUER, ARRÊTÉ EN ENTIER.
//
// POURQUOI PAS `juce::ChildProcess`. La chaîne d'analyse est un `python` qui
// lance demucs, puis des dizaines de `vsm-render` : des PETITS-ENFANTS, qui
// héritent du tube de sortie. Or `juce::ChildProcess` (JUCE 8) lit ce tube par
// `fread`, qui BLOQUE tant qu'un seul détenteur du tube vit, et son `kill()`
// envoie SIGKILL au seul enfant direct. Résultat mesuré le 15/09/2026 : quitter
// l'application pendant la séparation tue `python`, laisse demucs finir seul,
// bloque le thread de lecture sur le tube que demucs tient encore, et
// `stopThread(4000)` le tue de force -- « killing thread by force », puis
// `terminate called without an active exception`, code 134. Le bouton
// « Annuler » avait le même défaut, sans le crash : la chaîne continuait.
//
// ICI : `fork`, `setpgid(0, 0)` dans l'enfant (il devient chef de son groupe),
// lecture par `poll` avec un délai borné, et arrêt par `kill(-pgid)` -- SIGTERM
// d'abord, SIGKILL si le groupe vit encore après le délai. Linux et POSIX
// seulement, comme le reste de l'application.
#include <JuceHeader.h>
#include <sys/types.h>

namespace vsm::app {

class ProcessusDeChaine {
public:
    ProcessusDeChaine() = default;
    ~ProcessusDeChaine();
    ProcessusDeChaine(const ProcessusDeChaine&) = delete;
    ProcessusDeChaine& operator=(const ProcessusDeChaine&) = delete;

    /// Lance `commande[0]` avec ses arguments ; sortie et erreur réunies dans un
    /// tube. Rend faux si le fork ou l'exec échoue (l'échec d'exec se lit par un
    /// code de sortie 127).
    bool demarrer(const juce::StringArray& commande);

    /// Lit ce qui est disponible, en attendant `attenteMs` au plus. Rend le
    /// nombre d'octets lus, 0 si rien n'est venu dans le délai, -1 quand le
    /// tube est fermé par TOUS ses détenteurs (fin du flux).
    int lire(char* destination, int taille, int attenteMs);

    /// Le chef du groupe vit-il encore ? (récolte son code au passage)
    bool enVie();

    /// Envoie `signal` à tout le groupe (SIGTERM : demande d'arrêt).
    void signalerLeGroupe(int signal);

    /// Arrêt du GROUPE ENTIER : SIGTERM, puis SIGKILL si le chef vit encore
    /// après `delaiMs`. Bloque au plus `delaiMs` plus la récolte.
    void terminer(int delaiMs);

    /// Le code de sortie du chef (récolte bloquante s'il n'est pas encore lu) ;
    /// -1 s'il a été tué par un signal.
    int codeDeSortie();

    pid_t pid() const { return pid_; }

private:
    void recolter(bool bloquer);
    pid_t pid_ = 0;
    int tube_ = -1;
    int code_ = -1;
    bool recolte_ = false;
};

} // namespace vsm::app
