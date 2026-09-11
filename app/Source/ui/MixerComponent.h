#pragma once
#include <algorithm>
#include <map>
#include <set>
#include <JuceHeader.h>
#include "vsm/audio/engine/Mixer.h"
#include "BulleDeValeur.h"
#include <string>
#include <vector>
#include "LookAndFeel/VsmLookAndFeel.h"
#include "Langue.h"
#include "vsm/audio/engine/MasterBus.h"
#include "vsm/sequencer/Project.h"
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

/// LE FADER D'UNE TRANCHE EST EN DÉCIBELS, LE GAIN D'UNE PISTE EST LINÉAIRE.
/// Les deux conversions vivaient dans un espace anonyme du .cpp, donc
/// invisibles depuis l'en-tête : `refreshFromTrack` y a écrit un gain brut
/// dans un curseur en dB, et 0,25 de gain s'y est affiché « 0,3 dB » sans que
/// rien ne proteste -- la valeur tombait dans la plage sans y avoir de sens.
/// Un même calcul à deux endroits finit toujours par n'être fait qu'à un seul.
inline float gainToDb(float g) { return g > 1.0e-5f ? 20.0f * std::log10(g) : -60.0f; }
inline float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }

// Console de mixage (section 15/21, "Phase 2 UI"). Une tranche par piste +
// une tranche master. Le Mixer n'est PAS une source de vérité : il lit/écrit
// directement les champs de vsm::sequencer::Track (volume/pan/muted/solo) --
// comme le documente Mixer.h côté moteur -- et notifie le parent via
// onMixChanged() pour qu'il republie le snapshot audio. Les paramètres du
// bus master transitent par onMasterParam()/onMasterEnable() (le MasterBus
// est déjà thread-safe : setParameter y est atomique).
//
// Aucune logique DSP ici : cette couche est purement UI, testée à la main
// puisqu'elle dépend de JUCE (le DSP correspondant, lui, est couvert par les
// tests de vsm_audio : Mixer, MasterBus, MeterBank).

/// Petit vu-mètre vertical (échelle dB), rafraîchi par le parent.
///
/// DEPUIS D4.7 IL EN MONTRE DEUX : la barre pleine est la valeur EFFICACE
/// (RMS), le trait fin la CRÊTE. La crête seule disait si ça écrête ; elle ne
/// disait pas si c'était fort, et deux pistes de même crête peuvent être
/// séparées de quinze décibels perçus. Les deux dans le même mètre, c'est ce
/// que fait toute console, et pour la même raison : on lit d'un coup d'œil
/// l'écart entre les deux, qui est la densité de la piste.
class LevelMeter : public juce::Component {
public:
    /// Le niveau efficace, qui remplit la barre.
    void setRms(float linearRms) {
        const float db = linearRms > 1.0e-5f ? 20.0f * std::log10(linearRms) : -100.0f;
        const float pos = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
        if (std::abs(pos - rms_) > 1.0e-4f) { rms_ = pos; repaint(); }
    }
    /// La corrélation de phase, de -1 à +1, peinte en pied de mètre.
    void setCorrelation(float value) {
        if (std::abs(value - correlation_) > 1.0e-3f) { correlation_ = value; repaint(); }
    }
    float correlation() const { return correlation_; }
    float rmsPosition() const { return rms_; }

    void setLevel(float linearPeak) {
        // Amplitude linéaire -> position 0..1 sur une échelle -60..0 dBFS.
        float db = linearPeak > 1.0e-5f ? 20.0f * std::log10(linearPeak) : -100.0f;
        float pos = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
        if (std::abs(pos - level_) > 1.0e-4f || pos > level_) {
            level_ = pos;
            if (pos > peakHold_) peakHold_ = pos;
            else peakHold_ = juce::jmax(0.0f, peakHold_ - 0.01f); // redescente lente
            repaint();
        }
    }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(vsm::ui::Palette::pianoKeyBlack);
        g.fillRoundedRectangle(r, 2.0f);
        // LA BANDE DU BAS EST LA CORRÉLATION DE PHASE : au centre, sans
        // rapport ; à droite, en phase ; à GAUCHE, en opposition -- et c'est le
        // seul endroit du logiciel qui dise qu'une piste va disparaître en mono.
        auto barre = r;
        const float hauteurPhase = 4.0f;
        auto phase = barre.removeFromBottom(hauteurPhase);
        barre.removeFromBottom(2.0f);

        if (level_ > 0.0f) {
            float h = barre.getHeight() * level_;
            juce::Rectangle<float> bar(barre.getX(), barre.getBottom() - h, barre.getWidth(), h);
            juce::ColourGradient grad(vsm::ui::Palette::accentTeal, 0, barre.getBottom(),
                                       vsm::ui::Palette::accentRed, 0, barre.getY(), false);
            grad.addColour(0.75, vsm::ui::Palette::accentAmber);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(bar, 2.0f);
        }
        // LA CRÊTE EST UN TRAIT, le RMS remplit : la barre pleine dit le
        // niveau, le trait dit la marge avant écrêtage, et l'écart entre les
        // deux dit la densité de la piste.
        if (rms_ > 0.0f) {
            const float h = barre.getHeight() * rms_;
            g.setColour(vsm::ui::Palette::textPrimary.withAlpha(0.35f));
            g.fillRect(barre.getX(), barre.getBottom() - h, barre.getWidth(), 1.0f);
        }
        if (peakHold_ > 0.0f) {
            float y = barre.getBottom() - barre.getHeight() * peakHold_;
            g.setColour(vsm::ui::Palette::textPrimary);
            g.fillRect(barre.getX(), y, barre.getWidth(), 1.5f);
        }

        g.setColour(vsm::ui::Palette::pianoKeyBlack);
        g.fillRect(phase);
        const float centre = phase.getCentreX();
        const float x = centre + correlation_ * phase.getWidth() * 0.5f;
        // Rouge dès que la corrélation devient négative : ce n'est pas une
        // nuance, c'est un avertissement.
        g.setColour(correlation_ < 0.0f ? vsm::ui::Palette::accentRed
                                        : vsm::ui::Palette::accentTeal);
        g.fillRect(juce::Rectangle<float>(std::min(centre, x), phase.getY(),
                                           std::abs(x - centre) + 1.0f, phase.getHeight()));
    }
private:
    float level_ = 0.0f, peakHold_ = 0.0f;
    float rms_ = 0.0f;
    float correlation_ = 1.0f;
};

/// Tranche d'une piste.
class ChannelStrip : public juce::Component {
public:
    /// `sendNames` donne un bouton par bus DÉCLARÉ par le projet, dans son
    /// ordre. Deux boutons figés promettaient deux départs qui n'étaient écrits
    /// nulle part et dont rien ne disait le contenu ; un bouton par bus nommé
    /// dit ce qu'on alimente.
    /// Pour un bus de groupe : les pistes routées vers lui, dites en infobulle.
    void setMembers(const juce::StringArray& membres);
    ChannelStrip(vsm::sequencer::Track& track, size_t index,
                  const std::vector<std::string>& sendNames);
    /// D94 : les infobulles et l'unité de transposition, dans la langue
    /// courante -- à la construction, puis à chaque bascule.
    void retraduire();
    void resized() override;
    void paint(juce::Graphics&) override;
    void setMeasurement(const vsm::audio::engine::TrackMeasurement& m) {
        meter_.setLevel(m.peak);
        meter_.setRms(m.rms);
        meter_.setCorrelation(m.correlation);
    }

    std::function<void()> onMixChanged;
    /// D21.2 : Ctrl+clic sur Solo -- l'application met cette piste seule en solo.
    std::function<void(size_t)> onExclusiveSoloRequested;
    /// Relit muet et solo depuis la piste (après un solo exclusif).
    /// D29.3 : voir MixerComponent::applyExternalControl.
    void applyExternalControl(const std::string& parametre, float valeur);
    /// L'index de la piste que cette tranche montre. Nécessaire depuis D35.5 :
    /// les tranches ne sont plus en correspondance de rang avec les pistes, un
    /// dossier n'en ayant pas.
    size_t trackIndex() const { return index_; }

    /// D35.5 : le bouton M s'allume AUSSI quand le silence vient d'un dossier.
    /// Un dossier n'a plus de tranche (voir `MixerComponent::rebuild`) : sans
    /// cela, on aurait une tranche silencieuse dont aucun bouton n'est
    /// enfoncé, et l'on chercherait la panne dans le fader.
    /// D37 : RELIT DE LA PISTE TOUT CE QUI S'AFFICHE AUSSI AILLEURS -- le nom,
    /// le volume, le panoramique. Chacun était posé une seule fois, ici, à la
    /// construction : régler un fader dans la ligne de piste laissait la
    /// tranche montrer l'ancienne valeur, indéfiniment.
    ///
    /// `dontSendNotification` partout : on REFLÈTE, on ne modifie pas. Avec une
    /// notification, rafraîchir la tranche depuis la liste rappellerait la
    /// liste, et les deux panneaux se renverraient la balle sans fin.
    /// D37 : CE QUE LA TRANCHE AFFICHE, et non ce que la piste contient. Les
    /// deux se mesurent séparément : c'est leur DÉSACCORD qui est le défaut, et
    /// le lire dans la piste des deux côtés ne le montrerait jamais.
    /// D42.3 : CE QUE COÛTE CETTE PISTE, et si elle est parmi les plus chères.
    ///
    /// LE CHIFFRE VA DANS L'INFOBULLE, PAS DANS LA TRANCHE. Une console est
    /// étroite, et y glisser un nombre de plus demanderait une police plus
    /// petite -- exactement ce que l'échelle d'interface à 150 % existe pour
    /// éviter. Ce qu'on lit d'un coup d'oeil est donc le NOM EN AMBRE ; le
    /// nombre est là pour qui veut le vérifier.
    ///
    /// « Chère » veut dire chère PAR RAPPORT AUX AUTRES de ce projet, et non
    /// au-dessus d'un seuil absolu : un projet de quatre flûtes n'a pas de
    /// piste chère, et un projet de soixante-quatre additifs en aurait
    /// soixante-quatre. C'est la comparaison qui dit quoi geler.
    void setRenderCost(float micros, bool parmiLesPlusCheres) {
        coutMicros_ = micros;
        if (parmiLesPlusCheres != chere_) {
            chere_ = parmiLesPlusCheres;
            nameLabel_.setColour(juce::Label::textColourId,
                                 chere_ ? vsm::ui::Palette::accentAmber
                                        : vsm::ui::Palette::textPrimary);
            repaint();
        }
        // D94 : traduite ; le nom de la piste, une donnée, entre en dernier.
        nameLabel_.setTooltip(vsm::app::ui::tr(u8"%1 — calcul : %2 µs par bloc")
                                  .replace("%2", juce::String(micros, 0))
                                  .replace("%1", juce::String::fromUTF8(track_.name.c_str()))
            + (chere_ ? vsm::app::ui::tr(u8" (l'une des plus chères de ce projet : « Piste ▸ Geler la piste » "
                                         u8"la remplace par son enregistrement)")
                      : juce::String()));
    }
    float coutMicros() const { return coutMicros_; }
    /// D42.3 : cette tranche est-elle DÉSIGNÉE comme l'une des plus chères ?
    /// Exposé pour être mesuré : un banc qui décrirait la règle sans la relire
    /// ne vérifierait rien.
    bool estChere() const { return chere_; }

    /// D39.4 : cette tranche est-elle celle d'une piste choisie ?
    void setChoisie(bool choisie) { if (choisie != choisie_) { choisie_ = choisie; repaint(); } }

    juce::String nomAffiche() const { return nameLabel_.getText(); }
    double volumeAffiche() const { return volume_.getValue(); }

    void refreshFromTrack() {
        nameLabel_.setText(track_.name.empty() ? "Track" : track_.name, juce::dontSendNotification);
        nameLabel_.setTooltip(juce::String::fromUTF8(track_.name.c_str()));
        // LE FADER DE LA TRANCHE EST EN DÉCIBELS (-60..+6), le curseur de la
        // ligne de piste en gain linéaire (0..1,5). Y poser le gain brut
        // donnait 0,3 dB pour un gain de 0,25 -- la valeur tombait dans la
        // plage sans y avoir de sens, donc sans rien signaler. Écrit ici parce
        // que c'est le banc qui l'a montré : mesurer ce que le panneau AFFICHE,
        // et non ce que la piste contient, est ce qui rend ce défaut visible.
        volume_.setValue(gainToDb(track_.volume), juce::dontSendNotification);
        pan_.setValue(track_.pan, juce::dontSendNotification);
    }
    void refreshMuteSolo(bool tuParUnDossier = false) {
        mute_.setToggleState(track_.muted || tuParUnDossier, juce::dontSendNotification);
        mute_.setTooltip(tuParUnDossier && !track_.muted
                             ? juce::String::fromUTF8(u8"Rendu muet par son dossier")
                             : juce::String());
        rafraichirSolo();   // D30.1 : le libellé et la couleur du solo protégé aussi
    }
    /// Prévenu AVANT qu'un geste ne modifie le mixage : c'est là que
    /// l'application prend son instantané d'annulation. Séparé de
    /// `onMixChanged`, qui arrive après et à chaque échantillon d'un glissé --
    /// s'en servir empilerait trois cents pas d'annulation pour un seul
    /// mouvement de fader.
    std::function<void()> onMixEditStarted;
    /// ÉCRIRE L'AUTOMATION EN JOUANT (D16.8) — le W de Cubase, l'armement de
    /// Live. La tranche a besoin de deux choses que seule l'application sait :
    /// OÙ en est le transport, et s'il roule. Sans elles, elle écrirait une
    /// courbe à la position zéro pendant qu'on écoute la mesure trente.
    std::function<vsm::midi::Tick()> playheadTickProvider;
    std::function<bool()> transportPlayingProvider;
    /// Une passe vient d'être déposée dans la courbe : republier au moteur.
    std::function<void()> onAutomationWritten;
    /// LE TRANSPORT S'EST ARRÊTÉ : c'est ce qui clôt les passes en `Latch`.
    /// Appelée par l'application, parce que la tranche ne l'apprend pas.
    void closeLatchedPasses();

private:
    vsm::sequencer::Track& track_;
    size_t index_;
    juce::Label nameLabel_;
    juce::Slider volume_;
    juce::Slider pan_;
    vsm::app::ui::BulleDeValeur bullePan_ { pan_ };   ///< D135 (après le curseur : détruite avant)
    /// LE DÉCALAGE DE PISTE (D16.7), en millisecondes : une case où l'on TAPE
    /// un nombre, pas un bouton qu'on tourne. C'est un réglage qu'on connaît
    /// (« la basse arrive trois millisecondes trop tard »), pas un réglage
    /// qu'on cherche à l'oreille -- et un bouton de dix pixels ne saurait pas
    /// donner le dixième de milliseconde.
    juce::Slider delay_;
    /// LA TRANSPOSITION DE PISTE (D17.5), en demi-tons. Une case où l'on tape
    /// un nombre, comme le décalage : on transpose de sept demi-tons ou de
    /// douze, on ne cherche pas le chiffre à la molette. Elle est ICI, à côté
    /// du fader, parce que le piano roll montre le matériau et non ce qui
    /// sonne : c'est le seul endroit où le réglage se voit forcément.
    juce::Slider transposition_;
    /// D30.4 : LE TRIM D'ENTRÉE, en décibels. Une case où l'on tape un nombre,
    /// comme le décalage et la transposition -- et ELLE EST EN HAUT DE LA
    /// TRANCHE, avant le panoramique, parce que c'est sa place dans le
    /// signal : ce qui se lit de haut en bas doit être ce qui se traverse du
    /// premier au dernier, sans quoi la tranche raconte une chaîne qui n'est
    /// pas celle qu'on entend.
    juce::Slider trim_;
    /// LE BOUTON W : off → touch → latch → off. Le mot « W » plutôt qu'un
    /// pictogramme, comme chez Cubase, et sa couleur dit lequel des deux
    /// modes est armé (l'ambre pour `touch`, plus vif pour `latch`).
    juce::TextButton armer_ { "W" };
    /// D23.1 : LA POLARITÉ, le Ø de Cubase. À côté du W, sur la même rangée :
    /// deux libellés d'un caractère tiennent là où trois se tronquaient.
    juce::TextButton phase_ { juce::String::fromUTF8("\xc3\x98") };

    /// UNE PASSE EN COURS, par paramètre : on peut tenir le fader d'une main
    /// et le panoramique de l'autre, et ce sont deux courbes.
    struct Passe {
        vsm::midi::Tick debut = 0;
        std::vector<vsm::sequencer::AutomationPoint> points;
        bool relachee = false;   ///< vrai en `latch` après le lâcher
    };
    std::map<std::string, Passe> passes_;

    /// Le geste commence sur `parametre` : ouvre une passe si la piste est
    /// armée et que le transport roule.
    void ouvrirPasse(const std::string& parametre);
    /// La valeur a bougé : la note dans la passe, s'il y en a une.
    void noterDansLaPasse(const std::string& parametre, float valeur);
    /// Le geste finit : dépose en `touch`, laisse courir en `latch`.
    void fermerPasse(const std::string& parametre, bool arretDuTransport);
    /// La courbe de ce paramètre, créée si elle manque.
    vsm::sequencer::AutomationCurve& courbeDe(const std::string& parametre);
    void basculerArmement();
    void rafraichirArmement();
    /// Un bouton par bus de départ du projet. `OwnedArray` et non deux membres :
    /// leur nombre n'est plus connu à la compilation.
    juce::OwnedArray<juce::Slider> sends_;
    /// D135 : une bulle par départ (APRÈS `sends_` : détruites avant les curseurs).
    std::vector<std::unique_ptr<vsm::app::ui::BulleDeValeur>> bullesDesDeparts_;
    /// D94 : ce que `retraduire()` refait -- les noms des bus (les infobulles
    /// des départs) et, pour un bus de groupe, ses membres une fois connus.
    std::vector<std::string> sendNames_;
    juce::StringArray membres_;
    bool membresConnus_ = false;
    void poserInfobulleDuNom();
    juce::TextButton mute_ { "M" };
    juce::TextButton solo_ { "S" };
    /// D39.4 : dessinée comme choisie.
    bool choisie_ = false;
    /// D42.3 : le temps de calcul de la piste, et si elle est parmi les chères.
    float coutMicros_ = 0.0f;
    bool chere_ = false;
    /// D30.1 : le bouton Solo dit s'il est PROTÉGÉ -- « S+ » et l'ambre du
    /// solo à l'état éteint, parce qu'un réglage qui ne se voit pas est un
    /// réglage qu'on croit ne pas avoir posé.
    void rafraichirSolo();
    LevelMeter meter_;
};

/// Tranche master : EQ 3 bandes, compresseur, saturation, plafond limiteur,
/// mètre LUFS + crête. Liée aux paramètres du MasterBus via callbacks.
class MasterStrip : public juce::Component {
public:
    /// D59 : SOUS CETTE HAUTEUR DE RANGÉE, l'étiquette de 12 points et son
    /// potentiomètre ne cohabitent plus. La tranche demande alors à défiler
    /// plutôt qu'à écraser — la lisibilité prime sur « ça tient dans la case ».
    static constexpr int kHauteurRangeeMinimale = 34;

    /// La hauteur en dessous de laquelle cette tranche ne se dispose plus sans
    /// recouvrement. Le mélangeur la consulte pour décider s'il faut défiler.
    int hauteurUtile() const;

    MasterStrip();
    void resized() override;
    void paint(juce::Graphics&) override;

    void setMeters(double lufs, float linearPeak, float linearRms, float correlation) {
        // D48 : LA SATURATION SE VOIT, ET ELLE RESTE VISIBLE.
        //
        // L'aiguille disait le niveau et ne disait pas le DÉPASSEMENT : au-delà
        // de 1, la barre est simplement en butée, et l'on ne distingue pas 1,0
        // de 1,4. Or `children-dream-v7` sort à **1,405** — jouer ce morceau
        // sature, et rien à l'écran ne le disait.
        //
        // LE TÉMOIN GARDE SA MÉMOIRE, comme sur toutes les consoles : une
        // crête dure quelques échantillons, un voyant qui s'éteindrait aussitôt
        // ne serait jamais vu. Il retient aussi le PIRE dépassement, en dB,
        // parce que « ça a saturé » et « ça a saturé de 3 dB » n'appellent pas
        // le même geste. On l'éteint en cliquant dessus, comme partout.
        if (linearPeak > 1.0f) {
            satVue_ = true;
            pireDepassement_ = juce::jmax(pireDepassement_, linearPeak);
            satLabel_.setText(juce::String::fromUTF8(u8"SAT +")
                                  + juce::String(20.0f * std::log10(pireDepassement_), 1),
                              juce::dontSendNotification);
            satLabel_.setColour(juce::Label::textColourId, vsm::ui::Palette::accentRed);
            poserInfobulleSat();
            const bool apparait = !satLabel_.isVisible();
            satLabel_.setVisible(true);
            if (apparait) resized();
        }
        meter_.setLevel(linearPeak);
        meter_.setRms(linearRms);
        meter_.setCorrelation(correlation);
        lufsLabel_.setText(lufs <= vsm::audio::dsp::LufsMeter::kSilence + 1.0
                               ? juce::String("-inf LUFS")
                               : juce::String(lufs, 1) + " LUFS",
                           juce::dontSendNotification);
        // LE CHIFFRE DE LA CORRÉLATION EN CLAIR, à côté de l'aiguille : une
        // bande colorée dit qu'il y a un problème, elle ne dit pas s'il est de
        // -0,1 ou de -0,9, et c'est ce qui décide si on va chercher.
        phaseLabel_.setText("phase " + juce::String(correlation, 2), juce::dontSendNotification);
        phaseLabel_.setColour(juce::Label::textColourId,
                               correlation < 0.0f ? vsm::ui::Palette::accentRed
                                                  : vsm::ui::Palette::textSecondary);
    }

    // Fournit/pousse les paramètres du bus master.
    std::function<void(vsm::audio::plugin::ParamId, float)> onMasterParam;
    /// D144 : le début d'un geste sur un bouton du MASTER (glissé ou double-clic) :
    /// l'hôte y ouvre un pas d'annulation, comme pour un geste de tranche.
    std::function<void()> onMasterEditStarted;
    std::function<void(bool)> onMasterEnable;
    std::function<float(vsm::audio::plugin::ParamId)> masterParamProvider;
    /// D23.5 : l'écoute en mono, un outil d'écoute -- jamais un paramètre du
    /// bus, jamais dans le fichier ni dans un export.
    std::function<void(bool)> onMonoListen;
    void setMonoListen(bool on) { monoButton_.setToggleState(on, juce::dontSendNotification); }
    /// D48 : on efface le témoin de saturation en cliquant dessus, comme sur
    /// une console. Le pire dépassement repart de zéro avec lui : garder
    /// l'ancien ferait réapparaître un chiffre qu'on vient d'acquitter.
    void mouseDown(const juce::MouseEvent&) override {
        if (!satVue_) return;
        satVue_ = false;
        pireDepassement_ = 0.0f;
        satLabel_.setVisible(false);
        resized();
    }
    /// D48 : pour la mesure -- ce que le témoin annonce, ou rien.
    juce::String saturationAffichee() const {
        return satLabel_.isVisible() ? satLabel_.getText() : juce::String();
    }

    /// Synchronise l'UI depuis les valeurs courantes du bus master.
    void syncFromEngine();
    /// D94 : les infobulles, dans la langue courante.
    void retraduire();
    /// D48 / D94 : l'infobulle du témoin de saturation -- la phase y reste
    /// lisible, puisqu'il lui prend sa ligne.
    void poserInfobulleSat();

private:
    juce::Slider& addKnob(vsm::audio::plugin::ParamId id, const juce::String& label,
                          float min, float max, float def, const juce::String& suffix);

    juce::TextButton enableButton_ { "MASTER" };
    juce::TextButton monoButton_ { "MONO" };   ///< D23.5
    juce::Label titleLabel_, lufsLabel_, phaseLabel_;
    /// D48 : le témoin de saturation, et le pire dépassement qu'il retient.
    juce::Label satLabel_;
    bool satVue_ = false;
    float pireDepassement_ = 0.0f;
    LevelMeter meter_;

    struct Knob {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label> label;
        vsm::audio::plugin::ParamId id;
        std::unique_ptr<vsm::app::ui::BulleDeValeur> bulle;   ///< D135 (dernier : détruite d'abord)
    };
    std::vector<Knob> knobs_;
};

/// Console complète : défilement horizontal des tranches de piste + master
/// fixe à droite.
class MixerComponent : public juce::Component {
public:
    /// D59 : LA HAUTEUR EN DESSOUS DE LAQUELLE LA CONSOLE NE SE DISPOSE PLUS
    /// SANS RECOUVREMENT — celle que réclame la tranche master, qui est la plus
    /// haute. `MainComponent` s'en sert pour borner le dock du bas : sans elle,
    /// tirer la poignée vers le bas faisait réapparaître le défaut que cette
    /// phase corrige, et la septième commande du master redevenait invisible.
    int hauteurMinimale() const;

    MixerComponent();
    void resized() override;
    void paint(juce::Graphics&) override;

    /// (Re)construit les tranches depuis le projet.
    void setProject(vsm::sequencer::Project* project);
    /// D94 : les infobulles des tranches et du master, dans la langue courante.
    void retraduire();

    /// Rafraîchit les vu-mètres (appelé par le timer du parent, thread UI).
    /// Rafraîchit tous les mètres. `trackMeasure` rend les trois mesures d'une
    /// piste (D4.7) : la crête seule ne disait pas si c'était fort, et rien ne
    /// disait ce qu'il resterait du mixage en mono.
    void updateMeters(const std::function<vsm::audio::engine::TrackMeasurement(size_t)>& trackMeasure,
                      double masterLufs, float masterPeak, float masterRms,
                      float masterCorrelation);

    std::function<void()> onMixChanged;
    /// D21.2 : une tranche a demandé le solo EXCLUSIF (Ctrl+clic sur Solo).
    std::function<void(size_t)> onExclusiveSoloRequested;
    /// D39.4 : LES TRANCHES DES PISTES CHOISIES SE VOIENT.
    ///
    /// ET LA QUESTION ANNONCÉE AVANT LA MESURE A BIEN EU LIEU : un DOSSIER
    /// n'a plus de tranche depuis D35.5 (il n'est pas un bus, aucun signal n'y
    /// passe). Choisir un dossier dans la liste n'a donc rien à éclairer ici.
    /// **Ce n'est pas un défaut à corriger** : le mélangeur montre les chemins
    /// du signal, et un dossier n'en est pas un. Lui rendre une tranche pour
    /// qu'elle puisse être choisie remettrait les six commandes mortes que
    /// D35.5 a retirées. La sélection est donc dessinée sur les tranches QUI
    /// EXISTENT, et le dossier choisi se voit là où il vit : dans la liste.
    ///
    /// C'est aussi pourquoi l'appariement passe par `trackIndex()` et non par
    /// le rang de la tranche -- la n-ième tranche n'est plus la n-ième piste,
    /// et D35.5 a payé trois fois pour l'avoir oublié.
    void setSelectedTracks(const std::set<size_t>& tracks) {
        for (auto* strip : strips_) strip->setChoisie(tracks.count(strip->trackIndex()) > 0);
    }
    /// D40.3 : FAIRE DÉFILER JUSQU'À LA TRANCHE DE CETTE PISTE.
    ///
    /// À 64 pistes, le mélangeur en montre treize : choisir la piste 41 dans la
    /// liste dessinait son contour ambre sur une tranche **hors de l'écran**.
    /// La marque existait et ne se voyait pas -- le défaut même que D38.1 et
    /// D39.3 ont corrigé ailleurs, revenu par l'échelle. La liste des pistes
    /// avait `faireVoirLaPiste` depuis longtemps, et son commentaire annonçait
    /// déjà le cas (« un projet en parité en a onze ») ; le mélangeur n'avait
    /// pas son jumeau.
    ///
    /// PAR `trackIndex()` ET NON PAR LE RANG DE LA TRANCHE : depuis D35.5 un
    /// dossier n'a plus de tranche, et la n-ième tranche n'est plus la n-ième
    /// piste. Cette confusion a été payée trois fois dans cette phase-là.
    void faireVoirLaTranche(size_t trackIndex);
    /// D42.3 : PUBLIE LE COÛT DE CHAQUE PISTE, et désigne les plus chères.
    ///
    /// LE SEUIL EST RELATIF À LA MÉDIANE, et non absolu : sur un projet de
    /// pistes toutes semblables, aucune n'est « la chère », et c'est juste --
    /// il n'y a rien à geler en particulier.
    ///
    /// LA MÉDIANE ET NON LA MOYENNE : une seule piste très chère tire la
    /// moyenne au point de se cacher elle-même derrière son propre seuil.
    ///
    /// TROIS FOIS LA MÉDIANE ÉTAIT TROP HAUT, ET C'EST UNE VRAIE
    /// RECONSTRUCTION QUI L'A DIT. Le seuil venait du parc : D41 y a mesuré un
    /// rapport de 45 entre les extrêmes, d'où l'idée qu'une piste qui dépasse
    /// trois fois la médiane est « d'une autre famille ». Mais un projet réel
    /// n'est pas le parc. Sur `children-dream-v7` -- six pistes, six machines
    /// différentes, de vraies notes -- les coûts mesurés en jeu sont
    /// 86, 58, 136, 62, 152 et 34 µs : médiane 86, maximum 152, soit **1,8
    /// fois**. Le seuil de trois ne se déclenchait donc JAMAIS, et la fonction
    /// n'aurait servi à personne.
    ///
    /// **CE QU'IL FAUT DÉSIGNER N'EST PAS UNE ABERRATION, C'EST LA PLUS
    /// CHÈRE.** La question de l'utilisateur est « laquelle je gèle ? », et
    /// elle a une réponse même quand l'écart est modeste -- geler la piste à
    /// 152 µs rend cinq fois ce que rend celle à 34. On désigne donc **la
    /// plus chère**, et seulement si elle se détache un peu (1,5 fois la
    /// médiane), pour qu'un projet plat n'en désigne aucune.
    /// D42.3 : les tranches désignées chères, pour la mesure.
    std::vector<size_t> pistesCheres() const {
        std::vector<size_t> v;
        for (auto* strip : strips_) if (strip->estChere()) v.push_back(strip->trackIndex());
        return v;
    }
    /// D42.3 (corrigé) : `enLecture` dit si le transport JOUE.
    ///
    /// AU REPOS, LE CLASSEMENT NE VEUT RIEN DIRE, et c'est une mesure qui l'a
    /// montré : sur quatre pistes dont une en `vsm.additive` -- la machine la
    /// plus chère du parc, 5,8 fois le Minimoog en jeu --, l'additive au repos
    /// coûte **16,3 µs contre 75 à 84** aux trois autres. C'est la MOINS chère.
    /// Une machine sans voix active ne somme rien, tandis qu'un soustractif
    /// fait tourner ses oscillateurs et son filtre quoi qu'il arrive.
    ///
    /// Recalculer la désignation à l'arrêt reviendrait donc à désigner une
    /// piste au hasard dès qu'on appuie sur Stop. On GARDE la dernière
    /// désignation faite en jeu : ce qu'on lit est alors la dernière mesure
    /// qui voulait dire quelque chose, et non un classement de repos.
    void publishRenderCosts(const std::function<float(size_t)>& coutDeLaPiste, bool enLecture = true) {
        if (!enLecture) return;
        std::vector<float> couts;
        couts.reserve(strips_.size());
        for (auto* strip : strips_) couts.push_back(coutDeLaPiste(strip->trackIndex()));
        if (couts.empty()) return;
        std::vector<float> tries = couts;
        std::sort(tries.begin(), tries.end());
        const float mediane = tries[tries.size() / 2];
        const float sommet = tries.back();
        // UNE SEULE désignée : « laquelle je gèle » n'a qu'une réponse, et en
        // désigner trois ferait recommencer le choix.
        //
        // ET LA DÉCISION EST TENUE, PAS RECALCULÉE À CHAQUE IMAGE. Mesuré sur
        // `children-dream-v7` en lecture, deux relevés à une seconde
        // d'intervalle : `86 58 136 62 152 34` puis `80 63 106 17 49 17`. Les
        // coûts d'un bloc oscillent du simple au triple selon les notes qui
        // tombent, si bien qu'une désignation recalculée à chaque fois saute
        // d'une tranche à l'autre. **Une étiquette qui clignote ne se lit
        // pas** -- et celle-ci sert à choisir quoi geler, c'est-à-dire à
        // décider.
        //
        // On ne change donc d'avis que si la prétendante dépasse la désignée
        // en titre d'un QUART. C'est ce qui distingue « une autre piste vient
        // de jouer une note » de « c'est l'autre qui coûte ».
        size_t candidate = strips_.size();
        for (size_t i = 0; i < couts.size(); ++i)
            if (couts[i] >= sommet) { candidate = i; break; }
        const bool ilYAUneCoupable = mediane > 0.0f && sommet > 1.5f * mediane;
        if (!ilYAUneCoupable) {
            designee_ = strips_.size();
        } else if (designee_ >= couts.size()) {
            designee_ = candidate;
        } else if (candidate != designee_ && sommet > 1.25f * couts[designee_]) {
            designee_ = candidate;
        }
        for (size_t i = 0; i < strips_.size(); ++i)
            strips_[static_cast<int>(i)]->setRenderCost(couts[i], i == designee_);
    }
    /// D37 : chaque tranche relit sa piste (nom, volume, panoramique).
    void refreshFromTracks() {
        for (auto* strip : strips_) strip->refreshFromTrack();
    }
    /// Relit muet et solo de chaque tranche depuis sa piste.
    void refreshMuteSolo() {
        for (auto* strip : strips_) {
            // D35.5 : le muet HÉRITÉ d'un dossier s'affiche sur la tranche du
            // membre, puisque le dossier n'en a plus.
            bool herite = false;
            if (project_ != nullptr) {
                const size_t i = strip->trackIndex();
                if (i < project_->tracks.size())
                    herite = !vsm::sequencer::trackAudible(project_->tracks, i, false)
                          && !project_->tracks[i].muted && !project_->tracks[i].disabled;
            }
            strip->refreshMuteSolo(herite);
        }
    }
    /// D29.3 : une valeur venue d'AILLEURS que la souris (MIDI Learn) posée sur
    /// la tranche de la piste : le curseur, la piste, et la passe d'automation
    /// si le W est armé. Faux si la tranche n'existe pas.
    bool applyExternalControl(size_t track, const std::string& parametre, float valeur) {
        // D35.5 : ON CHERCHE LA TRANCHE DE CETTE PISTE plutôt que de prendre la
        // n-ième. Un dossier n'a plus de tranche, donc les rangs ont glissé :
        // au rang, un potentiomètre MIDI appris sur une piste aurait piloté sa
        // voisine, et l'on aurait cherché la panne dans le MIDI Learn.
        for (auto* strip : strips_)
            if (strip->trackIndex() == track) {
                strip->applyExternalControl(parametre, valeur);
                return true;
            }
        return false;
    }
    std::function<void()> onMixEditStarted;

    /// D16.8 : passés à chaque tranche à sa construction (voir ChannelStrip).
    std::function<vsm::midi::Tick()> playheadTickProvider;
    std::function<bool()> transportPlayingProvider;
    std::function<void()> onAutomationWritten;
    /// LE TRANSPORT S'EST ARRÊTÉ : clôt les passes en `Latch` de toutes les
    /// tranches. Sans cet appel, une passe en latch resterait ouverte et se
    /// déposerait au prochain arrêt, longtemps après le geste.
    void closeLatchedPasses();
    std::function<void(vsm::audio::plugin::ParamId, float)> onMasterParam;
    /// D144 : le début d'un geste sur un bouton du MASTER (glissé ou double-clic) :
    /// l'hôte y ouvre un pas d'annulation, comme pour un geste de tranche.
    std::function<void()> onMasterEditStarted;
    std::function<void(bool)> onMasterEnable;
    std::function<float(vsm::audio::plugin::ParamId)> masterParamProvider;
    /// D23.5 : l'écoute en mono, depuis le bouton MONO de la tranche master.
    std::function<void(bool)> onMonoListen;
    void setMonoListen(bool on) { master_.setMonoListen(on); }

private:
    vsm::sequencer::Project* project_ = nullptr;
    /// D42.3 : la tranche désignée la plus chère, tenue d'une image à l'autre
    /// (voir `publishRenderCosts`). Hors bornes = aucune.
    size_t designee_ = static_cast<size_t>(-1);
    juce::Viewport viewport_;
    juce::Component stripContainer_;
    juce::OwnedArray<ChannelStrip> strips_;
    MasterStrip master_;

    /// D30.4 : 76 -> 88 px, pour que « Trim -6.0 dB » tienne en entier. Le
    /// nombre seul se confondait avec le volume, et la règle du projet est
    /// d'agrandir la case plutôt que de rétrécir le texte.
    static constexpr int kStripWidth = 88;
    static constexpr int kMasterWidth = 150;
};
