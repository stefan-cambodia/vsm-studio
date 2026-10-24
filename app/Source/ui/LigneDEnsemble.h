#pragma once
#include <JuceHeader.h>
#include "ArrangementComponent.h"
#include "Langue.h"
#include "LookAndFeel/VsmLookAndFeel.h"
#include "vsm/sequencer/Project.h"
#include <algorithm>
#include <cmath>

// D591.1 de docs/ROADMAP-daw.md — LA LIGNE D'ENSEMBLE DU MORCEAU (l'*Overview Line* de Cubase).
//
// L'arrangement n'avait ni barre de défilement ni bande qui peigne le morceau entier : zoomé sur huit mesures
// d'un morceau de 227, rien ne disait où l'on était, et aller ailleurs demandait de dézoomer, viser, rezoomer.
// Cette bande, au-dessus de l'arrangement, peint chaque clip à la hauteur de sa piste et un rectangle sur ce que
// la vue montre ; un clic centre la vue sur le point cliqué, tirer le rectangle la fait défiler avec lui.
//
// UN COMPOSANT À PART, posé par `MainComponent` : l'arrangement compte ses ordonnées depuis son bord haut en cent
// endroits, et les bancs y cliquent à des coordonnées écrites. Éteinte (l'état d'origine), le centre ne bouge pas
// d'un pixel. La peinture, le geste et le relevé (`releve`) lisent la MÊME mise en page (`Mise`) : un relevé qui
// recalculerait à sa façon dirait ce que la peinture ne fait pas (la leçon de D149 et D152).

namespace vsm::app::ui {

/// Hauteur de la bande, en pixels logiques : celle d'une rangée de la règle.
constexpr int kLigneDEnsemble = ArrangementComponent::kRulerHeight;

class LigneDEnsemble : public juce::Component {
public:
    explicit LigneDEnsemble(ArrangementComponent& arrangement) : arrangement_(arrangement) {
        setName("Ligne d'ensemble");   // le nom que visent `clic:` et `glisser:`
    }

    void setProject(const vsm::sequencer::Project* projet) { projet_ = projet; repaint(); }

    /// Appelé par la minuterie d'interface : repeint quand la vue ou le morceau ont changé, et seulement alors.
    void suivre() {
        const Mise m = mise();
        const Signature s{arrangement_.pixelsPerTick(), arrangement_.scrollTick(), arrangement_.getWidth(), m.fin,
                          compterLesClips()};
        if (s == vue_) return;
        vue_ = s;
        repaint();
    }

    /// `VSM_ENSEMBLE : montrée oui ; morceau 0..T ticks ; vue A..B ticks ; rectangle x0..x1 sur L px ; N trait(s)`.
    juce::String releve() const {
        // Cachée, elle n'a pas de largeur : elle ne dit que cela (un « morceau vide » y mentirait).
        if (!isVisible()) return juce::String::fromUTF8("VSM_ENSEMBLE : montr\xc3\xa9" "e non");
        const Mise m = mise();
        const juce::String montree = "oui";
        if (m.largeur <= 0 || m.fin <= 0)
            return juce::String::fromUTF8("VSM_ENSEMBLE : montr\xc3\xa9" "e ") + montree + " ; morceau vide";
        return juce::String::fromUTF8("VSM_ENSEMBLE : montr\xc3\xa9" "e ") + montree + " ; morceau 0.."
               + juce::String(m.fin) + " ticks ; vue " + juce::String(m.vueDebut) + ".." + juce::String(m.vueFin)
               + " ticks ; rectangle " + juce::String(m.x(m.vueDebut) - m.gauche) + ".."
               + juce::String(m.x(m.vueFin) - m.gauche) + " sur " + juce::String(m.largeur) + " px ; "
               + juce::String(compterLesClips()) + " trait(s)";
    }

    void paint(juce::Graphics& g) override {
        namespace Palette = vsm::ui::Palette;
        g.fillAll(Palette::panel);
        const Mise m = mise();
        g.setColour(Palette::textSecondary);
        g.setFont(juce::FontOptions(12.0f));
        g.drawText(tr(u8"Ensemble du morceau"), 6, 0, m.gauche - 10, getHeight(), juce::Justification::centredLeft);
        g.setColour(Palette::border);
        g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
        if (projet_ == nullptr || m.largeur <= 0) return;
        if (m.fin <= 0) {
            g.setColour(Palette::textSecondary);
            g.drawText(tr(u8"(morceau vide)"), m.gauche, 0, m.largeur, getHeight(), juce::Justification::centred);
            return;
        }
        // UN TRAIT PAR CLIP, à la hauteur de sa piste, de sa couleur : au moins un pixel de large et de haut.
        const auto& pistes = projet_->tracks;
        const float haut = 2.0f, bas = static_cast<float>(getHeight()) - 3.0f;
        const float rang = pistes.empty() ? 0.0f : (bas - haut) / static_cast<float>(pistes.size());
        for (size_t i = 0; i < pistes.size(); ++i) {
            const float y = haut + rang * static_cast<float>(i);
            for (const auto& clip : pistes[i].clips) {
                const float x0 = m.xf(clip.startTick), x1 = m.xf(clip.startTick + clip.length);
                g.setColour(juce::Colour(clip.colorRgba));   // la lecture de l'arrangement (ArrangementComponent.cpp)
                g.fillRect(x0, y, std::max(1.0f, x1 - x0), std::max(1.0f, rang - (rang >= 3.0f ? 1.0f : 0.0f)));
            }
        }
        // LE RECTANGLE DE LA VUE : ce que l'arrangement montre, borné à la bande.
        const float r0 = m.xf(m.vueDebut), r1 = m.xf(m.vueFin);
        g.setColour(Palette::accentAmber.withAlpha(0.18f));
        g.fillRect(r0, 0.0f, std::max(2.0f, r1 - r0), static_cast<float>(getHeight()));
        g.setColour(Palette::accentAmber);
        g.drawRect(juce::Rectangle<float>(r0, 0.5f, std::max(2.0f, r1 - r0), static_cast<float>(getHeight()) - 1.0f), 1.5f);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        const Mise m = mise();
        tireLeRectangle_ = false;
        if (projet_ == nullptr || m.largeur <= 0 || m.fin <= 0 || e.x < m.gauche) return;
        const int x0 = m.x(m.vueDebut), x1 = m.x(m.vueFin);
        if (e.x >= x0 - 2 && e.x <= x1 + 2) {
            // SUR LE RECTANGLE : on le tire, la vue suit.
            tireLeRectangle_ = true;
            appuiX_ = e.x;
            defilementALAppui_ = arrangement_.scrollTick();
            return;
        }
        // AILLEURS : la vue centrée sur le point cliqué, le zoom gardé (comme Cubase).
        poserLeDefilement(m.tick(e.x) - (m.vueFin - m.vueDebut) / 2);
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (!tireLeRectangle_) return;
        const Mise m = mise();
        if (m.largeur <= 0) return;
        const double ticksParPixel = static_cast<double>(m.fin) / static_cast<double>(m.largeur);
        poserLeDefilement(defilementALAppui_
                          + static_cast<vsm::midi::Tick>(std::llround((e.x - appuiX_) * ticksParPixel)));
    }

    void mouseUp(const juce::MouseEvent&) override { tireLeRectangle_ = false; }

private:
    /// La mise en page, une seule fois pour la peinture, le geste et le relevé.
    struct Mise {
        int gauche = 0;                 ///< le bord gauche de la bande du morceau (sous les en-têtes, l'étiquette)
        int largeur = 0;                ///< sa largeur en pixels
        vsm::midi::Tick fin = 0;        ///< l'étendue : la fin du morceau, ou celle de la vue si elle va plus loin
        vsm::midi::Tick vueDebut = 0, vueFin = 0;
        float xf(vsm::midi::Tick t) const {
            return static_cast<float>(gauche) + static_cast<float>(static_cast<double>(t) / static_cast<double>(fin) * largeur);
        }
        int x(vsm::midi::Tick t) const { return static_cast<int>(std::lround(xf(t))); }
        vsm::midi::Tick tick(int px) const {
            return static_cast<vsm::midi::Tick>(std::llround(static_cast<double>(px - gauche) / largeur * static_cast<double>(fin)));
        }
    };

    Mise mise() const {
        Mise m;
        m.gauche = ArrangementComponent::kHeaderWidth;
        m.largeur = std::max(0, getWidth() - m.gauche);
        if (projet_ == nullptr) return m;
        const double parMesure = static_cast<double>(projet_->ticksPerQuarterNote) * 4.0;
        const auto visibles = static_cast<vsm::midi::Tick>(std::llround(arrangement_.mesuresVisibles() * parMesure));
        m.vueDebut = arrangement_.scrollTick();
        m.vueFin = m.vueDebut + visibles;
        const vsm::midi::Tick finDuMorceau = projet_->lastSoundingTick();
        m.fin = finDuMorceau > 0 ? std::max(finDuMorceau, m.vueFin) : 0;
        return m;
    }

    size_t compterLesClips() const {
        size_t n = 0;
        if (projet_ != nullptr)
            for (const auto& p : projet_->tracks) n += p.clips.size();
        return n;
    }

    /// Le défilement passe par `reprendreLaVue` : le zoom inchangé, ni pas d'annulation (un défilement n'en fait
    /// pas, D518) ni pas dans la pile des zooms (D557.3 : seul un geste de ZOOM y entre).
    void poserLeDefilement(vsm::midi::Tick defilement) {
        arrangement_.reprendreLaVue(arrangement_.pixelsPerTick(), std::max<vsm::midi::Tick>(0, defilement));
        suivre();
    }

    struct Signature {
        double zoom = -1.0;
        vsm::midi::Tick defilement = -1;
        int largeur = -1;
        vsm::midi::Tick fin = -1;
        size_t clips = 0;
        bool operator==(const Signature&) const = default;
    };

    ArrangementComponent& arrangement_;
    const vsm::sequencer::Project* projet_ = nullptr;
    Signature vue_;
    bool tireLeRectangle_ = false;
    int appuiX_ = 0;
    vsm::midi::Tick defilementALAppui_ = 0;
};

} // namespace vsm::app::ui
