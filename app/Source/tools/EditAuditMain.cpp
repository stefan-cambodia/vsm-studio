// D36.4 — LE BANC DES GESTES QUI ÉCHAPPENT À L'ANNULATION.
//
// POURQUOI CE BANC EXISTE. `MainComponent::beginProjectEdit` porte depuis D10.4
// le commentaire « c'est l'endroit qui ne peut pas être oublié, parce
// qu'oublier de l'appeler casserait déjà l'annulation, ce qui se voit tout de
// suite ». Il a été oublié TROIS FOIS en vingt phases, et cela ne s'est jamais
// vu : `SnapshotHistory` restaure le projet ENTIER, si bien qu'un geste qui
// n'empile rien ne supprime pas le Ctrl+Z -- il le DÉCALE. Le Ctrl+Z suivant
// remonte à l'instantané d'avant et annule deux gestes en un, sans le dire.
//
// CE QUE CE BANC MESURE, ET POURQUOI IL LE MESURE AINSI. Il ne vérifie pas une
// LISTE de gestes connus : une liste ne dirait rien du geste qu'on ajoutera
// demain, et c'est précisément le geste ajouté demain qui sera oublié. Il
// PARCOURT les widgets de la ligne de piste, en actionne chacun, et exige de
// chacun un signal de début d'édition. Un widget neuf, non câblé, fait donc
// échouer le banc le jour où on l'ajoute -- pas vingt phases plus tard.
//
// LES EXCEPTIONS SONT NOMMÉES, JAMAIS TACITES. Un geste peut légitimement
// n'empiler aucun pas ; il doit alors figurer ci-dessous avec sa raison. Une
// exception tacite serait exactement la panne muette que ce banc traque.

#include <JuceHeader.h>
#include "ui/TrackListComponent.h"
#include "ui/machines/StepSequencerComponent.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/sequencer/Project.h"
#include <cstdio>
#include <string>
#include <vector>

using vsm::sequencer::Project;
using vsm::sequencer::Track;

namespace {

/// Un geste de l'interface, et ce qu'il a signalé.
struct Geste {
    std::string nom;
    bool aSignale = false;
    bool exception = false;      ///< n'a pas à signaler, et l'on dit pourquoi
    std::string raison;
};

/// LES GESTES QUI N'ONT PAS À EMPILER DE PAS, ET LEUR RAISON.
/// Le texte est celui qu'on lira dans le rapport du banc : il doit se suffire.
struct Exception { const char* geste; const char* raison; };
const Exception kExceptions[] = {
    { "bouton R",
      "l'armement est un etat de SESSION, pas de morceau : il n'est pas ecrit "
      "dans project.json (voir Track.h), et un pas d'historique pour lui "
      "rendrait le Ctrl+Z incomprehensible" },
};

const Exception* exceptionPour(const std::string& nom) {
    for (const auto& e : kExceptions)
        if (nom == e.geste) return &e;
    return nullptr;
}

/// Le libellé sous lequel on désigne un widget dans le rapport.
std::string nommer(juce::Component& c, int rangLabel, int rangSlider, int rangBox) {
    if (auto* b = dynamic_cast<juce::TextButton*>(&c)) {
        const juce::String t = b->getButtonText();
        if (t.isNotEmpty()) return ("bouton " + t).toStdString();
        return "bouton (dossier)";
    }
    if (dynamic_cast<juce::Label*>(&c) != nullptr)
        return "champ texte n" + std::to_string(rangLabel);
    if (dynamic_cast<juce::Slider*>(&c) != nullptr)
        return "curseur n" + std::to_string(rangSlider);
    if (dynamic_cast<juce::ComboBox*>(&c) != nullptr)
        return "liste n" + std::to_string(rangBox);
    return "widget inconnu";
}

/// Actionne un widget comme le ferait l'utilisateur. Rend false si le widget
/// n'est pas d'un type qu'on sache actionner -- auquel cas le banc le DIT
/// plutôt que de le compter comme réussi.
bool actionner(juce::Component& c) {
    if (auto* b = dynamic_cast<juce::Button*>(&c)) {
        // PAS `triggerClick` : il passe par la boucle de messages, et le banc
        // relèverait son compteur avant que le clic ait eu lieu -- il compterait
        // alors muet un geste qui parle. `setToggleState(..., sendNotificationSync)`
        // appelle `sendClickMessage` sur place (juce_Button.cpp:199).
        if (b->getClickingTogglesState()) {
            b->setToggleState(!b->getToggleState(), juce::sendNotificationSync);
            return true;
        }
        if (b->onClick) { b->onClick(); return true; }
        return false;
    }
    if (auto* s = dynamic_cast<juce::Slider*>(&c)) {
        // Une valeur DIFFÉRENTE de celle en place : `setValue` ne notifie pas
        // si rien ne change, et le banc croirait alors le geste muet.
        const double autre = (s->getValue() == s->getMinimum()) ? s->getMaximum() : s->getMinimum();
        s->setValue(autre, juce::sendNotificationSync);
        return true;
    }
    if (auto* l = dynamic_cast<juce::Label*>(&c)) {
        if (!l->isEditableOnDoubleClick() && !l->isEditableOnSingleClick()) return false;
        l->setText(l->getText() == "12" ? "3" : "12", juce::sendNotificationSync);
        return true;
    }
    if (auto* cb = dynamic_cast<juce::ComboBox*>(&c)) {
        if (cb->getNumItems() < 2) return false;
        const int autre = (cb->getSelectedItemIndex() == 0) ? 1 : 0;
        cb->setSelectedItemIndex(autre, juce::sendNotificationSync);
        return true;
    }
    return false;
}

/// Recopié de `StepSequencerComponent` (où il est privé) : la largeur de la
/// colonne des libellés, qu'il faut dépasser pour viser un pas.
constexpr int kLargeurLibelleLigne = 92;

} // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;
    (void)argc; (void)argv;
    // Sans les machines, le sélecteur d'instrument n'a qu'une entrée
    // (« aucune ») et le banc ne peut pas l'actionner : il compterait un geste
    // couvert alors qu'il ne l'a pas essayé.
    vsm::audio::plugin::registerBuiltInPlugins();

    // Un projet minimal, et un GROUPE pour que la liste des sorties ait de
    // quoi proposer un second choix (sans lui, le geste « sortie » n'existe
    // pas et le banc le compterait comme non actionnable).
    Project projet;
    uint64_t ids = 1;
    Track piste;
    piste.name = "Piste";
    piste.channel = 0;
    piste.addNote(0, 480, 60, 100, 0, ids);
    projet.tracks.push_back(piste);
    Track groupe;
    groupe.kind = Track::Kind::Group;
    groupe.name = "Bus";
    projet.tracks.push_back(groupe);

    // UN DOSSIER, parce que le bouton de repli n'existe que sur une ligne de
    // dossier : sans lui, le banc ne l'actionnerait jamais et le compterait
    // comme couvert. Un banc qui saute un geste sans le dire est pire qu'un
    // banc absent.
    Track dossier;
    dossier.kind = Track::Kind::Folder;
    dossier.name = "Dossier";
    projet.tracks.push_back(dossier);

    const std::vector<std::pair<int, std::string>> groupes { { 1, "Bus" } };

    int signaux = 0;
    std::vector<Geste> gestes;

    // DEUX LIGNES, parce qu'une seule ne porte pas tous les gestes : la ligne
    // MIDI a une machine et une sortie, la ligne DOSSIER a le bouton de repli.
    // Les widgets d'une ligne sont privés ; on les atteint comme
    // l'utilisateur : en parcourant ce qu'elle affiche.
    const size_t lignesAtester[] = { 0, 2 };
    for (size_t index : lignesAtester) {
        TrackRowComponent ligne(projet.tracks[index], index, groupes);
        ligne.setBounds(0, 0, 900, 90);
        // CE QUE LE BANC ÉCOUTE : le signal de début d'édition.
        ligne.onEditStarted = [&signaux](const juce::String&) { ++signaux; };

        int rangLabel = 0, rangSlider = 0, rangBox = 0;
        for (int i = 0; i < ligne.getNumChildComponents(); ++i) {
            juce::Component* enfant = ligne.getChildComponent(i);
            if (enfant == nullptr) continue;
            if (dynamic_cast<juce::Label*>(enfant) != nullptr) ++rangLabel;
            if (dynamic_cast<juce::Slider*>(enfant) != nullptr) ++rangSlider;
            if (dynamic_cast<juce::ComboBox*>(enfant) != nullptr) ++rangBox;

            Geste g;
            g.nom = nommer(*enfant, rangLabel, rangSlider, rangBox);
            // Un même geste porté par les deux lignes ne se compte qu'une fois :
            // les noms ne portent donc PAS la ligne d'où ils viennent. La
            // seconde ligne n'est là que pour le bouton de repli, qui n'existe
            // que sur elle.
            bool deja = false;
            for (const auto& v : gestes) if (v.nom == g.nom) deja = true;
            if (deja) continue;
            const int avant = signaux;
            if (!actionner(*enfant)) continue;   // widget d'affichage : rien à annuler
            g.aSignale = (signaux > avant);
            if (const Exception* e = exceptionPour(g.nom)) {
                g.exception = true;
                g.raison = e->raison;
            }
            gestes.push_back(std::move(g));
        }
    }

    // LE SÉQUENCEUR PAS À PAS (D36.3), le plus cher des trois oublis : basculer
    // un pas appelle `writePatternToTrack`, qui RÉÉCRIT le vecteur de notes de
    // la piste. On le mesure ici plutôt que de l'affirmer, et l'on mesure aussi
    // ce que l'annulation rendrait -- un séquenceur qui rend « le motif
    // d'avant » en mangeant les notes qui n'en font pas partie aurait l'air
    // correct sur un projet de banc et perdrait du travail sur un vrai morceau.
    {
        vsm::panels::SequencerSpec spec;
        spec.kind = vsm::panels::SequencerKind::DrumGrid;
        spec.stepCount = 16;
        spec.lanes = { { "KICK", 36 }, { "SNARE", 38 } };

        // Une note QUE LE MOTIF NE DÉCRIT PAS : hors grille, et sur une hauteur
        // qui n'est aucune des deux lignes.
        Track batterie;
        batterie.name = "Batterie";
        uint64_t idsB = 1;
        // (départ, fin, hauteur, vélocité, canal) : la hauteur 64 n'est aucune
        // des deux lignes de la grille, et le départ tombe dans sa fenêtre.
        batterie.addNote(37, 100, 64, 90, 9, idsB);
        const std::vector<vsm::sequencer::Note> avant = batterie.notes;

        StepSequencerComponent grille;
        grille.configure(spec, &batterie, juce::Colours::darkgrey, juce::Colours::white);
        grille.setBounds(0, 0, 600, 120);

        Geste g;
        g.nom = "pas de sequenceur";
        int signauxGrille = 0;
        grille.onEditStarted = [&signauxGrille](const juce::String&) { ++signauxGrille; };
        // Un clic au centre du premier pas de la première ligne.
        const juce::Point<float> cible(static_cast<float>(kLargeurLibelleLigne + 20), 40.0f);
        const juce::MouseEvent ev(juce::Desktop::getInstance().getMainMouseSource(), cible,
                                   juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                   &grille, &grille, juce::Time::getCurrentTime(), cible,
                                   juce::Time::getCurrentTime(), 1, false);
        grille.mouseDown(ev);
        g.aSignale = (signauxGrille > 0);
        gestes.push_back(g);

        // LE GESTE ATTEINT-IL LA PISTE ? Le pas d'historique prouve qu'on a
    // SIGNALÉ ; il ne prouve pas qu'on a ÉCRIT. Les deux se mesurent
    // séparément, sans quoi un signal émis à la place de l'écriture passerait
    // pour une réussite.
    {
        Track seule;
        seule.name = "Essai";
        TrackRowComponent ligneMuet(seule, 0, {});
        ligneMuet.setBounds(0, 0, 900, 90);
        const bool avantMuet = seule.muted;
        ligneMuet.basculerMuet();
        std::printf("=== D36.1 : LE GESTE ATTEINT-IL LA PISTE ? ===\n");
        std::printf("  muted avant=%s apres=%s -> %s\n",
                    avantMuet ? "vrai" : "faux", seule.muted ? "vrai" : "faux",
                    (seule.muted != avantMuet) ? "la piste a bien change"
                                               : "LA PISTE N'A PAS CHANGE");
    }

    std::printf("=== D36.3 : LE SEQUENCEUR PAS A PAS ===\n");
        std::printf("  notes avant le clic : %d ; apres : %d\n",
                    static_cast<int>(avant.size()), static_cast<int>(batterie.notes.size()));
        bool horsMotifSurvit = false;
        for (const auto& n : batterie.notes)
            if (n.number == 64 && n.startTick == 37) horsMotifSurvit = true;
        std::printf("  la note hors motif %s\n",
                    horsMotifSurvit
                        ? "survit au clic (D36.6 : la grille n'efface que ses hauteurs)"
                        : "A ETE MANGEE par le clic -- la grille efface une hauteur qu'elle ne montre pas");
    }

    std::printf("=== D36.4 : LES GESTES DE LA LIGNE DE PISTE ===\n");
    int muets = 0;
    for (const auto& g : gestes) {
        if (g.exception) {
            std::printf("  %-22s  aucun pas ATTENDU : %s\n", g.nom.c_str(), g.raison.c_str());
            continue;
        }
        std::printf("  %-22s  %s\n", g.nom.c_str(),
                    g.aSignale ? "un pas d'historique" : "AUCUN PAS -- inannulable et non photographie");
        if (!g.aSignale) ++muets;
    }
    std::printf("=== %d geste(s) actionne(s), %d sans pas d'historique ===\n",
                static_cast<int>(gestes.size()), muets);
    return muets == 0 ? 0 : 1;
}
