#pragma once
#include <JuceHeader.h>

namespace vsm::app::ui {

/// D215, D219 : LES FENÊTRES MODALES, RÉPONDUES PAR LE BANC.
///
/// L'application pose dix-huit fenêtres modales qui demandent quelque chose : le
/// nom d'un repère, le nom d'une piste, un nombre de mesures à répéter, une
/// position où aller, un programme MIDI, les neuf réglages des deux exports. Une
/// course sans souris s'arrêtait au seuil de chacune : le geste qui l'ouvre était
/// pilotable, ce qu'elle FAIT ne l'était pas. C'est le même angle mort que les
/// sélecteurs de fichier avant D102-D214, et il a coûté la même chose — des
/// chemins d'usage courant que personne ne relit.
///
/// `VSM_OPTIONS="clef=valeur;…"` : une liste déroulante prend le NUMÉRO de son
/// choix (celui de l'identifiant JUCE, 1 pour le premier), un champ texte prend
/// son texte. La fonction DIT ce qu'elle a posé, avec le libellé obtenu pour une
/// liste, et NOMME les clefs que cette fenêtre ne connaît pas.
///
/// ELLE NE RÉPOND QUE SI ELLE A POSÉ QUELQUE CHOSE. Sans cette règle, une course
/// réglée pour une fenêtre validerait toutes les autres au passage, et l'on
/// mesurerait une cascade que personne n'a demandée.
inline bool repondreAuxChampsDeBanc(juce::AlertWindow& fenetre) {
    const char* brut = std::getenv("VSM_OPTIONS");
    if (brut == nullptr || *brut == '\0') return false;
    juce::StringArray couples;
    couples.addTokens(juce::String::fromUTF8(brut), ";", "");
    juce::StringArray posees, refusees;
    for (const auto& couple : couples) {
        const juce::String clef = couple.upToFirstOccurrenceOf("=", false, false).trim();
        const juce::String valeur = couple.fromFirstOccurrenceOf("=", false, false).trim();
        if (clef.isEmpty()) continue;
        if (auto* liste = fenetre.getComboBoxComponent(clef)) {
            liste->setSelectedId(valeur.getIntValue(), juce::dontSendNotification);
            posees.add(clef + "=" + valeur + " (" + liste->getText() + ")");
        } else if (auto* champ = fenetre.getTextEditor(clef)) {
            champ->setText(valeur, false);
            posees.add(clef + "=" + valeur);
        } else {
            refusees.add(clef);
        }
    }
    if (posees.isEmpty()) {
        // LES ACCENTS PAR `fromUTF8` : `juce::String(const char*)` lit du Latin-1, et
        // cette ligne sortait « fenÃªtre » à sa première course — le piège que
        // `CLAUDE.md` nomme, payé une fois de plus, et au premier coup d'œil cette
        // fois.
        std::fputs((juce::String::fromUTF8(u8"VSM_OPTIONS : aucune clef de cette fenêtre (« ")
                    + fenetre.getName() + juce::String::fromUTF8(u8" ») parmi ")
                    + refusees.joinIntoString(", ") + "\n").toRawUTF8(), stderr);
        return false;
    }
    std::fputs(("VSM_OPTIONS : " + posees.joinIntoString(", ")
                + (refusees.isEmpty()
                       ? juce::String()
                       : juce::String::fromUTF8(u8" — clef(s) inconnue(s) de cette fenêtre : ")
                             + refusees.joinIntoString(", "))
                + "\n").toRawUTF8(), stderr);
    return true;
}

/// Montre la fenêtre, ou la remplit et la valide si le banc l'a demandé. `suite`
/// est le rappel modal, appelé avec 1 (valider) dans le cas du banc : c'est le
/// MÊME code que celui d'un clic, et non une seconde écriture du geste.
template <typename Suite>
void montrerOuRepondre(juce::AlertWindow& fenetre, Suite suite) {
    if (repondreAuxChampsDeBanc(fenetre)) {
        suite(1);
        return;
    }
    // D275 : RÉPONDRE PAR LE BOUTON, EN GARDANT LES DÉFAUTS DE LA FENÊTRE.
    //
    // `VSM_OPTIONS` remplit des champs ; il ne sait pas presser un bouton. Or
    // certaines fenêtres ne demandent AUCUNE saisie — « Tempo du clip » propose
    // « Garder le tempo du projet » ou « Adopter ce tempo pour le projet », et
    // rien d'autre. L'entrée « Le clip fait N mesures… » en ouvre justement deux
    // à la suite, dont celle-là : elle était la seule du menu du clip audio
    // qu'aucune course ne pouvait mener à son effet, et elle laissait donc le
    // projet inchangé — ce qui ressemble exactement à un geste mort.
    //
    // Ce que fait cette branche est ce que fait un utilisateur qui presse OK
    // sans rien taper : les champs gardent les valeurs que la fenêtre y a mises.
    // `VSM_CONFIRMER` porte déjà ce sens pour les boîtes à deux boutons (D235) ;
    // on le lit ici avec la même convention. Elle ne s'applique QUE si
    // `VSM_OPTIONS` n'a rien posé — une course qui remplit des champs décide
    // elle-même, et n'a pas à être devinée.
    //
    // La ligne du journal le DIT, sans quoi une course répondrait « oui » sans
    // laisser de trace de l'avoir fait.
    if (fenetre.getNumButtons() >= 2) {
        if (const char* reponse = std::getenv("VSM_CONFIRMER"); reponse != nullptr && *reponse) {
            const juce::String demande = juce::String(reponse).trim().toLowerCase();
            const bool accepte = demande.startsWith("o") || demande.startsWith("y") || demande == "1";
            std::fputs((juce::String::fromUTF8(u8"VSM_CONFIRMER : ") + (accepte ? "oui" : "non")
                        + juce::String::fromUTF8(u8" — fenêtre « ") + fenetre.getName()
                        + juce::String::fromUTF8(u8" », ses champs gardés tels quels\n")).toRawUTF8(), stderr);
            suite(accepte ? 1 : 0);
            return;
        }
    }
    // D228 : UNE FENÊTRE QUI S'OUVRE LE DIT, même quand personne ne peut y
    // répondre. C'est la règle de D95 pour les boîtes (« une boîte se lit au
    // moment où elle est demandée »), étendue aux dix-huit fenêtres à saisie :
    // sans cette ligne, une course qui bute sur une modale ne laisse aucune trace,
    // et l'on cherche le défaut dans le geste qui précède. Le titre suffit à la
    // reconnaître ; le nombre de boutons dit ce qu'on aurait pu répondre.
    std::fputs((juce::String::fromUTF8(u8"VSM_BOITE : ") + fenetre.getName()
                + juce::String::fromUTF8(u8" — fenêtre modale ouverte (")
                + juce::String(fenetre.getNumButtons())
                + juce::String::fromUTF8(u8" bouton(s)), sans réponse de banc (VSM_OPTIONS)\n"))
                   .toRawUTF8(), stderr);
    fenetre.enterModalState(true, juce::ModalCallbackFunction::create(suite), false);
}

} // namespace vsm::app::ui
