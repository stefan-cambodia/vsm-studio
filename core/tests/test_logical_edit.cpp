#include "TestFramework.h"
#include "vsm/sequencer/LogicalEdit.h"
#include <set>
#include <string>
#include <vector>

using namespace vsm::sequencer;
using vsm::midi::Tick;

// D535.1 de docs/ROADMAP-daw.md — L'ÉDITEUR LOGIQUE : des conditions liées par « et », une
// règle qui s'écrit et se relit, cinq actions. Le jeu de notes est fait pour que chaque
// condition réponde pour un ensemble CONNU, et la position change de sens au passage à 3/4.

namespace {
constexpr uint16_t kPpq = 480;

Note n(uint64_t id, Tick debut, Tick duree, uint8_t hauteur, uint8_t velocite, uint8_t canal = 0,
       float confiance = 1.0f, bool muette = false) {
    Note x;
    x.id = id;
    x.startTick = debut;
    x.endTick = debut + duree;
    x.number = hauteur;
    x.velocity = velocite;
    x.channel = canal;
    x.confidence = confiance;
    x.muted = muette;
    return x;
}

/// 4/4 sur deux mesures (0..3840), puis 3/4 : la mesure 3 commence à 3840, la 4 à 5280.
TimeSignatureMap signatures() {
    TimeSignatureMap carte;
    carte.addChange(3840, 3, 2);
    return carte;
}

std::vector<Note> jeu() {
    return {
        n(1, 0, 240, 60, 100),                    // do, au début de la mesure 1
        n(2, 480, 30, 64, 20, 0, 0.3f),           // mi bref, faible, douteux — un fantôme
        n(3, 960, 480, 67, 25, 1, 0.9f),          // sol faible, canal 2
        n(4, 1920, 30, 72, 110, 0, 0.2f, true),   // do aigu bref, muet, au début de la mesure 2
        n(5, 4320, 480, 48, 90),                  // mesure 3 (3/4), un temps après son début
        n(6, 5400, 240, 50, 60),                  // mesure 4 (3/4, à 5280) : position 120
    };
}

std::set<uint64_t> ids(const NoteSelection& s) { return std::set<uint64_t>(s.begin(), s.end()); }

std::set<uint64_t> ou(const std::string& texte, const NoteSelection* parmi = nullptr) {
    LogicalRule regle;
    std::string erreur;
    if (!parseLogicalRule(texte, kPpq, regle, erreur)) return {999};   // une règle refusée ne passe pas pour vide
    return ids(selectNotesWhere(jeu(), regle, signatures(), kPpq, parmi));
}
} // namespace

// CHAQUE CHAMP, SOUS PLUSIEURS OPÉRATEURS, sur un ensemble connu.
VSM_TEST(chaque_champ_repond_sous_chaque_operateur) {
    VSM_ASSERT(ou("hauteur < 60") == (std::set<uint64_t>{5, 6}));
    VSM_ASSERT(ou("hauteur >= 64") == (std::set<uint64_t>{2, 3, 4}));
    VSM_ASSERT(ou("hauteur = 67") == (std::set<uint64_t>{3}));
    VSM_ASSERT(ou("hauteur != 60") == (std::set<uint64_t>{2, 3, 4, 5, 6}));
    VSM_ASSERT(ou("hauteur <= 48") == (std::set<uint64_t>{5}));
    VSM_ASSERT(ou("hauteur > 67") == (std::set<uint64_t>{4}));
    VSM_ASSERT(ou("vélocité < 30") == (std::set<uint64_t>{2, 3}));
    VSM_ASSERT(ou("durée <= 30") == (std::set<uint64_t>{2, 4}));
    VSM_ASSERT(ou("durée > 240") == (std::set<uint64_t>{3, 5}));
    VSM_ASSERT(ou("canal = 2") == (std::set<uint64_t>{3}));
    VSM_ASSERT(ou("confiance < 0.5") == (std::set<uint64_t>{2, 4}));
    VSM_ASSERT(ou("muette = 1") == (std::set<uint64_t>{4}));
    VSM_ASSERT(ou("muette = non") == (std::set<uint64_t>{1, 2, 3, 5, 6}));
}

// « ENTRE » COMPREND SES BORNES, « HORS » LES EXCLUT.
VSM_TEST(entre_comprend_ses_bornes_et_hors_les_exclut) {
    VSM_ASSERT(ou("vélocité entre 20 25") == (std::set<uint64_t>{2, 3}));
    VSM_ASSERT(ou("vélocité hors 20 25") == (std::set<uint64_t>{1, 4, 5, 6}));
    VSM_ASSERT(ou("vélocité hors 20 100") == (std::set<uint64_t>{4}));   // 20 et 100 sont des bornes : dedans
    VSM_ASSERT(ou("hauteur entre C3 B3") == (std::set<uint64_t>{5, 6}));  // 48..59
}

// LA POSITION SE COMPTE DANS SA MESURE, signature comprise : la note 6 est à 120 de la mesure
// 4 (3/4) ; en 4/4 partout, elle serait à 1 560.
VSM_TEST(la_position_se_compte_dans_sa_mesure_signature_comprise) {
    VSM_ASSERT(ou("position = 0") == (std::set<uint64_t>{1, 4}));
    VSM_ASSERT(ou("position = 480") == (std::set<uint64_t>{2, 5}));
    VSM_ASSERT(ou("position = 120") == (std::set<uint64_t>{6}));
    VSM_ASSERT(ou("position = 1/4") == (std::set<uint64_t>{2, 5}));   // une noire = 480
}

VSM_TEST(et_regle_vide_et_champ_d_application) {
    VSM_ASSERT(ou("vélocité < 30 et durée < 60") == (std::set<uint64_t>{2}));
    VSM_ASSERT(ou("") == (std::set<uint64_t>{1, 2, 3, 4, 5, 6}));   // aucune condition : toutes
    const NoteSelection parmi{1, 2, 3};
    VSM_ASSERT(ou("vélocité < 30", &parmi) == (std::set<uint64_t>{2, 3}));
    const NoteSelection une{1};
    VSM_ASSERT(ou("vélocité < 30", &une).empty());
    VSM_ASSERT(ou("", &une) == (std::set<uint64_t>{1}));
}

// LA RÈGLE ÉCRITE : accents facultatifs, noms de note, fractions de ronde, virgule décimale.
VSM_TEST(la_regle_ecrite_se_lit_avec_ou_sans_accents) {
    LogicalRule a, b;
    std::string erreur;
    VSM_ASSERT(parseLogicalRule("vélocité < 30 et durée < 1/32", kPpq, a, erreur));
    VSM_ASSERT(parseLogicalRule("Velocite<30 et duree <1/32", kPpq, b, erreur));
    VSM_ASSERT(a == b);
    VSM_ASSERT_EQ(a.conditions.size(), static_cast<size_t>(2));
    VSM_ASSERT(a.conditions[0].field == NoteField::Velocity && a.conditions[0].op == CompareOp::Less);
    VSM_ASSERT_EQ(a.conditions[1].a, 60.0);   // 1/32 de ronde à 480 ticks la noire
    VSM_ASSERT_EQ(logicalRuleText(a), std::string("velocite < 30 et duree < 60"));
    LogicalRule h;
    VSM_ASSERT(parseLogicalRule("hauteur = F#3 et hauteur != Bb2 et hauteur > c4", kPpq, h, erreur));
    VSM_ASSERT_EQ(h.conditions[0].a, 54.0);
    VSM_ASSERT_EQ(h.conditions[1].a, 46.0);
    VSM_ASSERT_EQ(h.conditions[2].a, 60.0);
    LogicalRule c;
    VSM_ASSERT(parseLogicalRule("confiance < 0,5", kPpq, c, erreur));
    VSM_ASSERT_EQ(c.conditions[0].a, 0.5);
}

// L'ÉCRITURE CANONIQUE RELUE REDONNE LA MÊME RÈGLE, pour chaque champ et chaque opérateur.
VSM_TEST(l_ecriture_canonique_fait_l_aller_retour) {
    size_t relues = 0;
    for (NoteField champ : {NoteField::Pitch, NoteField::Velocity, NoteField::Length, NoteField::BarPosition,
                            NoteField::Channel, NoteField::Confidence, NoteField::Muted})
        for (CompareOp op : {CompareOp::Equal, CompareOp::NotEqual, CompareOp::Less, CompareOp::LessOrEqual,
                             CompareOp::Greater, CompareOp::GreaterOrEqual, CompareOp::Between, CompareOp::Outside}) {
            LogicalRule regle;
            const double a = champ == NoteField::Confidence ? 0.25 : 3.0;
            const double b = champ == NoteField::Confidence ? 0.75 : 9.0;
            regle.conditions.push_back({champ, op, a, (op == CompareOp::Between || op == CompareOp::Outside) ? b : 0.0});
            regle.conditions.push_back({NoteField::Velocity, CompareOp::Greater, 10.0, 0.0});
            LogicalRule relue;
            std::string erreur;
            VSM_ASSERT(parseLogicalRule(logicalRuleText(regle), kPpq, relue, erreur));
            VSM_ASSERT(relue == regle);
            ++relues;
        }
    VSM_ASSERT_EQ(relues, static_cast<size_t>(7 * 8));
}

// UN MORCEAU FAUX EST REFUSÉ, LE MESSAGE LE NOMME, ET LA SORTIE N'EST PAS TOUCHÉE.
VSM_TEST(un_morceau_faux_est_refuse_et_nomme) {
    const std::vector<std::pair<std::string, std::string>> faux = {
        {"vélocité < trente", "trente"},
        {"volume > 3", "volume"},
        {"vélocité ≈ 3", "≈"},
        {"hauteur entre 48", "entre"},
        {"vélocité < 30 et", "vide"},
        {"hauteur = H4", "H4"},
        {"vélocité < 30 40", "40"},
    };
    for (const auto& [texte, nomme] : faux) {
        LogicalRule sortie;
        sortie.conditions.push_back({NoteField::Channel, CompareOp::Equal, 5.0, 0.0});
        const LogicalRule avant = sortie;
        std::string erreur;
        VSM_ASSERT(!parseLogicalRule(texte, kPpq, sortie, erreur));
        VSM_ASSERT(erreur.find(nomme) != std::string::npos);
        VSM_ASSERT(sortie == avant);
    }
}

// LES ACTIONS, et chacune ne touche QUE les notes qui répondent.
VSM_TEST(les_actions_ne_touchent_que_les_notes_qui_repondent) {
    const NoteSelection repondent{2, 3};
    {
        auto notes = jeu();
        const auto r = applyLogicalAction(notes, repondent, LogicalAction::Delete, 0);
        VSM_ASSERT_EQ(r.changed, static_cast<size_t>(2));
        std::set<uint64_t> restent;
        for (const auto& x : notes) restent.insert(x.id);
        VSM_ASSERT(restent == (std::set<uint64_t>{1, 4, 5, 6}));
    }
    {
        auto notes = jeu();
        const auto r = applyLogicalAction(notes, repondent, LogicalAction::Mute, 0);
        VSM_ASSERT_EQ(r.changed, static_cast<size_t>(2));
        for (const auto& x : notes) VSM_ASSERT_EQ(x.muted, x.id == 2 || x.id == 3 || x.id == 4);
    }
    {
        auto notes = jeu();
        const auto r = applyLogicalAction(notes, repondent, LogicalAction::Transpose, 12);
        VSM_ASSERT_EQ(r.changed, static_cast<size_t>(2));
        VSM_ASSERT_EQ(static_cast<int>(notes[1].number), 76);
        VSM_ASSERT_EQ(static_cast<int>(notes[2].number), 79);
        VSM_ASSERT_EQ(static_cast<int>(notes[0].number), 60);
    }
    {
        // D536 : une note qui sortirait de 0..127, et RIEN ne bouge — comptée.
        auto notes = jeu();
        notes.push_back(n(7, 6000, 240, 120, 80));
        const auto r = applyLogicalAction(notes, {1, 7}, LogicalAction::Transpose, 12);
        VSM_ASSERT_EQ(r.changed, static_cast<size_t>(0));
        VSM_ASSERT_EQ(r.refused, static_cast<size_t>(1));
        VSM_ASSERT_EQ(static_cast<int>(notes[0].number), 60);
        VSM_ASSERT_EQ(static_cast<int>(notes[6].number), 120);
    }
    {
        auto notes = jeu();
        const auto r = applyLogicalAction(notes, repondent, LogicalAction::SetVelocity, 64);
        VSM_ASSERT_EQ(r.changed, static_cast<size_t>(2));
        for (const auto& x : notes)
            VSM_ASSERT_EQ(static_cast<int>(x.velocity), (x.id == 2 || x.id == 3) ? 64 : static_cast<int>(jeu()[x.id - 1].velocity));
        // Une vélocité hors de 1..127 est refusée, jamais bornée.
        auto intactes = jeu();
        const auto refus = applyLogicalAction(intactes, repondent, LogicalAction::SetVelocity, 0);
        VSM_ASSERT_EQ(refus.changed, static_cast<size_t>(0));
        VSM_ASSERT_EQ(refus.refused, static_cast<size_t>(2));
        VSM_ASSERT_EQ(static_cast<int>(intactes[1].velocity), 20);
    }
    {
        auto notes = jeu();
        const auto r = applyLogicalAction(notes, repondent, LogicalAction::Select, 0);
        VSM_ASSERT(ids(r.selection) == (std::set<uint64_t>{2, 3}));
        VSM_ASSERT_EQ(notes.size(), static_cast<size_t>(6));
        VSM_ASSERT_EQ(static_cast<int>(notes[1].number), 64);
    }
}

// L'INTERFACE EST BILINGUE : la règle se tape aussi en anglais, et l'erreur se rend en GENRE et
// MORCEAU tapé — c'est le modèle de phrase que l'interface traduit, pas une phrase assemblée.
VSM_TEST(la_regle_se_lit_en_anglais_et_l_erreur_se_rend_en_genre_et_morceau) {
    LogicalRule fr, en;
    std::string erreur;
    VSM_ASSERT(parseLogicalRule("vélocité < 30 et durée entre 10 60 et muette = oui et canal != 10", kPpq, fr, erreur));
    VSM_ASSERT(parseLogicalRule("Velocity < 30 AND length between 10 60 and muted = yes and channel != 10", kPpq, en, erreur));
    VSM_ASSERT(fr == en);
    VSM_ASSERT_EQ(fr.conditions.size(), static_cast<size_t>(4));
    LogicalRule sortie;
    LogicalParseError detail;
    VSM_ASSERT(!parseLogicalRule("vélocité < Trente", kPpq, sortie, erreur, &detail));
    VSM_ASSERT(detail.kind == LogicalParseErrorKind::NotANumber);
    VSM_ASSERT_EQ(detail.piece, std::string("Trente"));
    VSM_ASSERT_EQ(erreur, std::string("« Trente » n'est pas un nombre"));
    VSM_ASSERT(!parseLogicalRule("pitch outside 40", kPpq, sortie, erreur, &detail));
    VSM_ASSERT(detail.kind == LogicalParseErrorKind::NeedsTwoValues);
    VSM_ASSERT_EQ(detail.piece, std::string("outside"));
    // Chaque modèle porte « %1 », sauf celui de la condition vide.
    for (auto genre : {LogicalParseErrorKind::NotANumber, LogicalParseErrorKind::NotANote,
                       LogicalParseErrorKind::NotTicksOrFraction, LogicalParseErrorKind::NotAField,
                       LogicalParseErrorKind::MissingField, LogicalParseErrorKind::NotAnOperator,
                       LogicalParseErrorKind::MissingValue, LogicalParseErrorKind::NeedsTwoValues,
                       LogicalParseErrorKind::ExtraValue})
        VSM_ASSERT(std::string(logicalParseErrorTemplate(genre)).find("%1") != std::string::npos);
}

// D535.1 bis, attendu 8 : L'ÉCRITURE LISIBLE se relit en la même règle, dans les deux langues, et
// garde son SENS d'une résolution à l'autre — ce que la canonique, en ticks, ne fait pas.
VSM_TEST(l_ecriture_lisible_fait_l_aller_retour_dans_les_deux_langues) {
    size_t relues = 0;
    for (RuleLanguage langue : {RuleLanguage::French, RuleLanguage::English})
        for (NoteField champ : {NoteField::Pitch, NoteField::Velocity, NoteField::Length, NoteField::BarPosition,
                                NoteField::Channel, NoteField::Confidence, NoteField::Muted})
            for (CompareOp op : {CompareOp::Equal, CompareOp::NotEqual, CompareOp::Less, CompareOp::LessOrEqual,
                                 CompareOp::Greater, CompareOp::GreaterOrEqual, CompareOp::Between, CompareOp::Outside}) {
                LogicalRule regle;
                // Des valeurs qui prennent chaque forme : nom de note, fraction, ticks, décimale, oui/non.
                double a = 3.0, b = 9.0;
                if (champ == NoteField::Pitch) { a = 61.0; b = 72.0; }
                if (champ == NoteField::Length) { a = 60.0; b = 50.0; }        // 1/32, et 50 qui reste en ticks
                if (champ == NoteField::BarPosition) { a = 0.0; b = 720.0; }  // 0, et 3/8
                if (champ == NoteField::Confidence) { a = 0.25; b = 0.75; }
                if (champ == NoteField::Muted) { a = 0.0; b = 1.0; }
                regle.conditions.push_back({champ, op, a, (op == CompareOp::Between || op == CompareOp::Outside) ? b : 0.0});
                regle.conditions.push_back({NoteField::Velocity, CompareOp::Greater, 10.0, 0.0});
                LogicalRule relue;
                std::string erreur;
                const std::string texte = logicalRuleReadableText(regle, kPpq, langue);
                VSM_ASSERT(parseLogicalRule(texte, kPpq, relue, erreur));
                VSM_ASSERT(relue == regle);
                ++relues;
            }
    VSM_ASSERT_EQ(relues, static_cast<size_t>(2 * 7 * 8));

    LogicalRule fantomes;
    std::string erreur;
    VSM_ASSERT(parseLogicalRule("velocite < 30 et duree < 60", kPpq, fantomes, erreur));
    VSM_ASSERT_EQ(logicalRuleReadableText(fantomes, kPpq, RuleLanguage::French), std::string("vélocité < 30 et durée < 1/32"));
    VSM_ASSERT_EQ(logicalRuleReadableText(fantomes, kPpq, RuleLanguage::English), std::string("velocity < 30 and length < 1/32"));
    LogicalRule formes;
    VSM_ASSERT(parseLogicalRule("hauteur entre 0 61 et confiance < 0.5 et muette = 1 et duree hors 1920 50 et position = 720",
                                kPpq, formes, erreur));
    VSM_ASSERT_EQ(logicalRuleReadableText(formes, kPpq, RuleLanguage::French),
                  std::string("hauteur entre C-1 C#4 et confiance < 0,5 et muette = oui et durée hors 1/1 50 et position = 3/8"));
    VSM_ASSERT_EQ(logicalRuleReadableText(formes, kPpq, RuleLanguage::English),
                  std::string("pitch between C-1 C#4 and confidence < 0.5 and muted = yes and length outside 1/1 50 and position = 3/8"));

    // LE SENS GARDÉ : retenue à 480 ppq, relue dans un projet à 960.
    const std::string retenue = logicalRuleReadableText(fantomes, kPpq, RuleLanguage::French);
    LogicalRule a960, canonique960;
    VSM_ASSERT(parseLogicalRule(retenue, 960, a960, erreur));
    VSM_ASSERT_EQ(a960.conditions[1].a, 120.0);
    VSM_ASSERT(parseLogicalRule(logicalRuleText(fantomes), 960, canonique960, erreur));
    VSM_ASSERT_EQ(canonique960.conditions[1].a, 60.0);   // le témoin : la canonique, elle, change de sens
}
