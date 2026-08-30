#pragma once
#include <JuceHeader.h>
#include "../reconstruction/ReconstructionRunner.h"

namespace vsm::app::ui {

/// CE QUE LA CHAÎNE EST EN TRAIN DE FAIRE (D9.2).
///
/// Une reconstruction dure de trois à quinze minutes selon le morceau et la
/// machine. Sans rien à l'écran, ces minutes-là ne se distinguent pas d'un
/// plantage : l'utilisateur finit par tuer l'application, et il a raison de le
/// faire puisque rien ne lui dit le contraire.
///
/// **PAS DE POURCENTAGE INVENTÉ.** Les cinq étapes de la chaîne durent de trois
/// secondes (lecture) à dix minutes (séparation) ; une barre qui les traiterait
/// comme égales passerait 80 % de son temps entre 20 et 40 %, ce qui est pire
/// que pas de barre du tout. On montre donc l'étape ANNONCÉE par la chaîne,
/// telle qu'elle la nomme, et le journal en dessous -- qui, lui, bouge tout le
/// temps et prouve à lui seul que rien n'est bloqué.
///
/// **ET ELLE EST ANNULABLE**, ce qui est la moitié du critère de D9.2 : lancer
/// une analyse de dix minutes sur le mauvais fichier arrive, et devoir tuer
/// l'application pour s'en sortir ferait perdre le morceau ouvert à côté.
class ReconstructionWindow : public juce::Component {
public:
    ReconstructionWindow();

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setSource(const juce::String& nomDuFichier);
    void setProgress(const vsm::app::ReconstructionRunner::Progress& avancement);
    /// Bascule la fenêtre en « terminé » : plus d'annulation, un bouton pour
    /// fermer, et le message final.
    void setFinished(bool succes, const juce::String& message);

    std::function<void()> onCancel;
    std::function<void()> onClose;

private:
    juce::Label titre_, etape_;
    juce::TextEditor journal_;
    juce::TextButton bouton_ { "Annuler" };
    bool termine_ = false;
};

} // namespace vsm::app::ui
