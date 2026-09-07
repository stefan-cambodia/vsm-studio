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
#include "ui/MixerComponent.h"
#include "ui/machines/StepSequencerComponent.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/sequencer/Project.h"
#include <cmath>
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

/// Les rangées vivent sous un conteneur, lui-même sous un viewport : on les
/// atteint en descendant l'arbre, comme le regard descend la liste. Un
/// `getChildComponent` sur la liste elle-même n'en rendrait aucune.
void collecterLignes(juce::Component& racine, std::vector<TrackRowComponent*>& sortie) {
    for (int i = 0; i < racine.getNumChildComponents(); ++i) {
        juce::Component* enfant = racine.getChildComponent(i);
        if (enfant == nullptr) continue;
        if (auto* ligne = dynamic_cast<TrackRowComponent*>(enfant)) sortie.push_back(ligne);
        else collecterLignes(*enfant, sortie);
    }
}

int desaccords = 0;   ///< panneaux ou lots qui ne font pas ce qu'ils annoncent

} // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;
    (void)argc; (void)argv;
    // Sans les machines, le sélecteur d'instrument n'a qu'une entrée
    // (« aucune ») et le banc ne peut pas l'actionner : il compterait un geste
    // couvert alors qu'il ne l'a pas essayé.
    vsm::audio::plugin::registerBuiltInPlugins();

    // ------------------------------------------------------------------
    // D38 — LA SÉLECTION MULTIPLE
    // ------------------------------------------------------------------
    // Deux moitiés qui comptent autant l'une que l'autre : SIX pistes tues, et
    // UN SEUL pas d'historique. Un pas unique qui n'en tairait qu'une serait
    // aussi faux que six pas qui en taisent six -- le premier ne fait pas le
    // geste, le second ne s'annule pas d'un coup.
    {
        Project lot;
        uint64_t idsL = 1;
        for (int i = 0; i < 8; ++i) {
            Track t;
            t.name = "Micro " + std::to_string(i + 1);
            t.addNote(0, 240, static_cast<uint8_t>(36 + i), 100, 9, idsL);
            lot.tracks.push_back(t);
        }
        TrackListComponent liste;
        liste.setBounds(0, 0, 260, 900);
        liste.loadProject(lot);

        int pas = 0;
        liste.onEditStarted = [&pas](const juce::String&) { ++pas; };

        liste.setSelectedTracks({0, 1, 2, 3, 4, 5}, 2);
        liste.basculerMuet(2);

        int tues = 0;
        for (const auto& t : lot.tracks) if (t.muted) ++tues;
        std::printf("=== D38 : SIX PISTES CHOISIES, UN CLIC SUR LE M ===\n");
        std::printf("  pistes tues=%d (attendu 6)   pas d'historique=%d (attendu 1) -> %s\n",
                    tues, pas,
                    (tues == 6 && pas == 1) ? "un geste, un pas" : "LE LOT N'EST PAS UN GESTE");
        if (tues != 6 || pas != 1) desaccords = 1;

        // D38.4 : un geste sur une piste HORS sélection ne touche qu'elle.
        pas = 0;
        liste.basculerMuet(7);
        const bool seuleLa7 = lot.tracks[7].muted && !lot.tracks[6].muted;
        const bool ramenee = liste.selectedTracks().size() == 1
                          && liste.selectedTracks().count(7) == 1;
        std::printf("  geste hors selection : %s, selection ramenee=%s, pas=%d -> %s\n",
                    seuleLa7 ? "elle seule est tue" : "D'AUTRES ONT ETE TOUCHEES",
                    ramenee ? "oui" : "NON", pas,
                    (seuleLa7 && ramenee && pas == 1) ? "la regle tient" : "LA REGLE NE TIENT PAS");
        if (!seuleLa7 || !ramenee || pas != 1) desaccords = 1;

        // D38.1 : une piste supprimée ne laisse pas son numéro dans l'ensemble.
        liste.setSelectedTracks({0, 1, 2, 3, 4, 5, 6, 7}, 0);
        lot.tracks.resize(3);
        liste.loadProject(lot);
        bool horsBornes = false;
        for (size_t i : liste.selectedTracks()) if (i >= lot.tracks.size()) horsBornes = true;
        std::printf("  apres suppression de 5 pistes : %d index retenus, hors bornes=%s\n",
                    static_cast<int>(liste.selectedTracks().size()),
                    horsBornes ? "OUI -- le geste suivant tairait une voisine" : "aucun");
        if (horsBornes) desaccords = 1;
    }

    // ------------------------------------------------------------------
    // D36.1 — LE GESTE ATTEINT-IL LA PISTE ?
    // ------------------------------------------------------------------
    // Le pas d'historique prouve qu'on a SIGNALÉ ; il ne prouve pas qu'on a
    // ÉCRIT. Les deux se mesurent séparément, sans quoi un signal émis à la
    // place de l'écriture passerait pour une réussite.
    {
        Project seul;
        uint64_t idsS = 1;
        Track une;
        une.name = "Essai";
        une.addNote(0, 240, 60, 100, 0, idsS);
        seul.tracks.push_back(une);
        TrackListComponent solo;
        solo.setBounds(0, 0, 260, 200);
        solo.loadProject(seul);
        const bool avant = seul.tracks[0].muted;
        solo.basculerMuet(0);
        std::printf("=== D36.1 : LE GESTE ATTEINT-IL LA PISTE ? ===\n");
        std::printf("  muted avant=%s apres=%s -> %s\n",
                    avant ? "vrai" : "faux", seul.tracks[0].muted ? "vrai" : "faux",
                    (seul.tracks[0].muted != avant) ? "la piste a bien change"
                                                    : "LA PISTE N'A PAS CHANGE");
        if (seul.tracks[0].muted == avant) desaccords = 1;
    }

    // ------------------------------------------------------------------
    // D37 — UNE VALEUR, DEUX PANNEAUX
    // ------------------------------------------------------------------
    // On mesure ce que chaque panneau AFFICHE, et non ce que la piste contient :
    // c'est le désaccord entre deux affichages qui est le défaut, et lire la
    // piste des deux côtés donnerait deux fois la même valeur juste.
    {
        Project deux;
        uint64_t idsD = 1;
        Track p;
        p.name = "Avant";
        p.addNote(0, 480, 60, 100, 0, idsD);
        deux.tracks.push_back(p);

        TrackRowComponent ligne(deux.tracks[0], 0, {});
        ligne.setBounds(0, 0, 900, 90);
        ChannelStrip tranche(deux.tracks[0], 0, {});
        tranche.setBounds(0, 0, 90, 400);

        ligne.renommer("Apres");
        ligne.reglerVolume(0.25f);
        std::printf("=== D37 : LA TRANCHE SUIT-ELLE LA LIGNE ? ===\n");
        std::printf("  avant rafraichissement : nom=%s volume=%.2f dB\n",
                    tranche.nomAffiche().toRawUTF8(), tranche.volumeAffiche());
        tranche.refreshFromTrack();
        // LES DEUX PANNEAUX N'AFFICHENT PAS DANS LA MÊME UNITÉ : le curseur de
        // la ligne est en gain linéaire, le fader de la tranche en décibels.
        // « S'accorder » veut donc dire que la conversion retombe sur ses pieds,
        // et non que les deux nombres sont égaux.
        //
        // ET LA TOLÉRANCE EST CELLE DU FADER : il avance par pas de 0,1 dB, si
        // bien qu'exiger l'égalité des GAINS échouait de 0,001 sur 0,25 -- non
        // par désaccord, mais parce qu'un fader n'a pas de position pour cette
        // valeur-là. Une tolérance prise ailleurs que dans la résolution de
        // l'instrument est un chiffre qu'on ajuste jusqu'à ce que le banc passe.
        const double dbAttendu = gainToDb(0.25f);
        const double dbAffiche = tranche.volumeAffiche();
        const bool nomOk = tranche.nomAffiche() == juce::String("Apres");
        const bool volOk = std::abs(dbAffiche - dbAttendu) <= 0.05;
        std::printf("  apres rafraichissement : nom=%s volume=%.2f dB (attendu %.2f dB) -> %s\n",
                    tranche.nomAffiche().toRawUTF8(), dbAffiche, dbAttendu,
                    (nomOk && volOk) ? "les deux panneaux s'accordent"
                                     : "LES DEUX PANNEAUX SE CONTREDISENT");
        if (!nomOk || !volOk) desaccords = 1;
    }

    // ------------------------------------------------------------------
    // D36.3 — LE SÉQUENCEUR PAS À PAS
    // ------------------------------------------------------------------
    // Le plus cher des oublis de D36 : basculer un pas appelle
    // `writePatternToTrack`, qui RÉÉCRIT le vecteur de notes de la piste. On
    // mesure aussi ce qu'il emporte -- une grille qui rend « le motif d'avant »
    // en mangeant les notes qui n'en font pas partie aurait l'air correcte sur
    // un projet de banc et perdrait du travail sur un vrai morceau.
    std::vector<Geste> gestes;
    int signaux = 0;
    {
        vsm::panels::SequencerSpec spec;
        spec.kind = vsm::panels::SequencerKind::DrumGrid;
        spec.stepCount = 16;
        spec.lanes = { { "KICK", 36 }, { "SNARE", 38 } };

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
        const juce::Point<float> cible(static_cast<float>(kLargeurLibelleLigne + 20), 40.0f);
        const juce::MouseEvent ev(juce::Desktop::getInstance().getMainMouseSource(), cible,
                                   juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                   &grille, &grille, juce::Time::getCurrentTime(), cible,
                                   juce::Time::getCurrentTime(), 1, false);
        grille.mouseDown(ev);
        g.aSignale = (signauxGrille > 0);
        gestes.push_back(g);

        std::printf("=== D36.3 : LE SEQUENCEUR PAS A PAS ===\n");
        std::printf("  notes avant le clic : %d ; apres : %d\n",
                    static_cast<int>(avant.size()), static_cast<int>(batterie.notes.size()));
        bool horsMotifSurvit = false;
        for (const auto& n : batterie.notes)
            if (n.number == 64 && n.startTick == 37) horsMotifSurvit = true;
        std::printf("  la note hors motif %s\n",
                    horsMotifSurvit
                        ? "survit au clic (D36.6 : la grille n'efface que ses hauteurs)"
                        : "A ETE MANGEE -- la grille efface une hauteur qu'elle ne montre pas");
        if (!horsMotifSurvit) desaccords = 1;
    }

    // ------------------------------------------------------------------
    // D36.4 — LES GESTES DE LA LIGNE DE PISTE
    // ------------------------------------------------------------------
    // LE BANC CONDUIT LA VRAIE LISTE, ET PLUS UNE RANGÉE NUE (D38.2). Depuis
    // que le muet et le solo passent par la LISTE -- seule à connaître la
    // sélection --, une rangée construite à part ne signale plus rien : le banc
    // aurait déclaré une régression là où il n'y a qu'un déplacement de
    // responsabilité. Un banc qui monte son propre assemblage mesure son
    // assemblage ; celui-ci mesure celui de l'application.
    Project projet;
    {
        uint64_t ids = 1;
        Track piste;
        piste.name = "Piste";
        piste.addNote(0, 480, 60, 100, 3, ids);
        projet.tracks.push_back(piste);
        Track groupe;
        groupe.kind = Track::Kind::Group;
        groupe.name = "Bus";
        projet.tracks.push_back(groupe);
        // UN DOSSIER, parce que le bouton de repli n'existe que sur une ligne
        // de dossier : sans lui, le banc ne l'actionnerait jamais et le
        // compterait comme couvert. Un banc qui saute un geste sans le dire est
        // pire qu'un banc absent.
        Track dossier;
        dossier.kind = Track::Kind::Folder;
        dossier.name = "Dossier";
        projet.tracks.push_back(dossier);
    }

    TrackListComponent liste;
    liste.setBounds(0, 0, 300, 900);
    liste.loadProject(projet);
    liste.onEditStarted = [&signaux](const juce::String&) { ++signaux; };

    std::vector<TrackRowComponent*> lignes;
    collecterLignes(liste, lignes);
    for (TrackRowComponent* ligne : lignes) {
        ligne->onEditStarted = [&signaux](const juce::String&) { ++signaux; };

        int rangLabel = 0, rangSlider = 0, rangBox = 0;
        for (int i = 0; i < ligne->getNumChildComponents(); ++i) {
            juce::Component* enfant = ligne->getChildComponent(i);
            if (enfant == nullptr) continue;
            if (dynamic_cast<juce::Label*>(enfant) != nullptr) ++rangLabel;
            if (dynamic_cast<juce::Slider*>(enfant) != nullptr) ++rangSlider;
            if (dynamic_cast<juce::ComboBox*>(enfant) != nullptr) ++rangBox;

            Geste g;
            g.nom = nommer(*enfant, rangLabel, rangSlider, rangBox);
            // Un même geste porté par plusieurs lignes ne se compte qu'une
            // fois : les noms ne portent donc PAS la ligne d'où ils viennent.
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

    std::printf("=== D36.4 : LES GESTES DE LA LIGNE DE PISTE ===\n");
    int muets = 0;
    for (const auto& g : gestes) {
        if (g.exception) {
            std::printf("  %-22s  aucun pas ATTENDU : %s\n", g.nom.c_str(), g.raison.c_str());
            continue;
        }
        std::printf("  %-22s  %s\n", g.nom.c_str(),
                    g.aSignale ? "un pas d'historique"
                               : "AUCUN PAS -- inannulable et non photographie");
        if (!g.aSignale) ++muets;
    }
    std::printf("=== %d geste(s) actionne(s), %d sans pas d'historique ===\n",
                static_cast<int>(gestes.size()), muets);
    if (desaccords > 0)
        std::printf("=== %d mesure(s) en desaccord avec ce qui est annonce ===\n", desaccords);
    return (muets == 0 && desaccords == 0) ? 0 : 1;
}
