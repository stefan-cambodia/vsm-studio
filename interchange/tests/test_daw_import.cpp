#include "TestFramework.h"
#include "vsm/interchange/DawImport.h"
#include "vsm/interchange/Xml.h"
#include <algorithm>
#include <string>
#include <vector>

using namespace vsm::interchange;

// L'import d'un projet Ableton Live, et la règle qui gouverne tout l'import :
// ce qui n'est pas repris doit être DIT (docs/CDC-import-daw.md § 0).
//
// Le projet d'épreuve est construit DANS le test — un `.als` réel appartient à
// quelqu'un et ne se redistribue pas. Il contient exprès ce qui pose problème :
// deux clips sur une même piste (dont un décalé), une piste muette, un nom
// avec une esperluette échappée, et une piste AUDIO qu'on ne peut pas reprendre.

namespace {
const std::string kAlsClair = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<Ableton MajorVersion="5" MinorVersion="11.0_11300" Creator="Ableton Live 11.3.4">
 <LiveSet>
  <Tracks>
   <MidiTrack Id="8">
    <Name><EffectiveName Value="Basse" /><UserName Value="" /></Name>
    <Color Value="3" />
    <DeviceChain>
     <MainSequencer>
      <ClipTimeable><ArrangerAutomation><Events>
       <MidiClip Id="0" Time="0">
        <CurrentStart Value="0" /><CurrentEnd Value="4" />
        <Notes><KeyTracks>
         <KeyTrack Id="0">
          <Notes>
           <MidiNoteEvent Time="0" Duration="0.5" Velocity="100" IsEnabled="true" />
           <MidiNoteEvent Time="1" Duration="0.25" Velocity="64" IsEnabled="true" />
          </Notes>
          <MidiKey Value="36" />
         </KeyTrack>
         <KeyTrack Id="1">
          <Notes><MidiNoteEvent Time="2" Duration="1" Velocity="127" IsEnabled="true" /></Notes>
          <MidiKey Value="43" />
         </KeyTrack>
        </KeyTracks></Notes>
       </MidiClip>
       <MidiClip Id="1" Time="8">
        <CurrentStart Value="8" /><CurrentEnd Value="12" />
        <Notes><KeyTracks><KeyTrack Id="0">
         <Notes><MidiNoteEvent Time="0" Duration="2" Velocity="90" IsEnabled="true" /></Notes>
         <MidiKey Value="36" />
        </KeyTrack></KeyTracks></Notes>
       </MidiClip>
      </Events></ArrangerAutomation></ClipTimeable>
     </MainSequencer>
    </DeviceChain>
   </MidiTrack>
   <MidiTrack Id="9">
    <Name><EffectiveName Value="Lead &amp; Solo" /></Name>
    <Color Value="7" />
    <DeviceChain><Mixer><Speaker Value="false" /></Mixer>
     <MainSequencer><ClipTimeable><ArrangerAutomation><Events>
      <MidiClip Id="2" Time="0">
       <CurrentStart Value="1.5" /><CurrentEnd Value="3" />
       <Notes><KeyTracks><KeyTrack Id="0">
        <Notes><MidiNoteEvent Time="0" Duration="0.25" Velocity="110" IsEnabled="true" /></Notes>
        <MidiKey Value="72" />
       </KeyTrack></KeyTracks></Notes>
      </MidiClip>
     </Events></ArrangerAutomation></ClipTimeable></MainSequencer>
    </DeviceChain>
   </MidiTrack>
   <AudioTrack Id="10">
    <Name><EffectiveName Value="Voix enregistree" /></Name>
   </AudioTrack>
  </Tracks>
  <MasterTrack>
   <DeviceChain><Mixer>
    <Tempo><Manual Value="128.5" /></Tempo>
   </Mixer></DeviceChain>
  </MasterTrack>
 </LiveSet>
</Ableton>
)XML";
const std::vector<uint8_t> kAlsGzip{0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x9d, 0x56, 0x4d, 0x73, 0xda, 0x30, 0x10, 0xbd, 0xf3, 0x2b, 0x34, 0x3e, 0xf4, 0x56, 0x84, 0x0c, 0x4d, 0xc8, 0x54, 0x38, 0x43, 0x09, 0x9d, 0xc9, 0x34, 0xe4, 0x02, 0xe1, 0xda, 0x51, 0xf1, 0x42, 0xd5, 0x18, 0x8b, 0xca, 0x32, 0x43, 0xfe, 0x7d, 0xd7, 0x1f, 0x32, 0xb6, 0x31, 0x60, 0x7a, 0x93, 0x56, 0xda, 0xd5, 0xdb, 0xa7, 0xa7, 0x67, 0xf3, 0xc7, 0xc3, 0x36, 0x20, 0x7b, 0xd0, 0x91, 0x54, 0xe1, 0xc8, 0x61, 0xdd, 0x9e, 0x43, 0x20, 0x5c, 0x29, 0x5f, 0x86, 0x9b, 0x91, 0xf3, 0xb6, 0xf8, 0xfe, 0x79, 0xe8, 0x3c, 0x7a, 0x1d, 0x3e, 0xfe, 0x15, 0x80, 0x51, 0x21, 0x99, 0x89, 0x3f, 0x4a, 0x2f, 0xed, 0xf6, 0x2f, 0x0e, 0x99, 0xc9, 0xb0, 0x14, 0x60, 0x58, 0xe0, 0x27, 0x63, 0xfd, 0x1e, 0x96, 0x99, 0x68, 0x10, 0x46, 0xe9, 0x91, 0x63, 0x73, 0x5f, 0xe4, 0x1e, 0x08, 0xee, 0xe8, 0x77, 0x07, 0x8e, 0xd7, 0x21, 0x3c, 0x99, 0xcf, 0xc1, 0xe0, 0x90, 0xf0, 0x85, 0x16, 0xab, 0xf7, 0x28, 0x19, 0x12, 0x3e, 0x93, 0xbe, 0x4c, 0xe7, 0xe4, 0xd9, 0x1f, 0x39, 0x43, 0x27, 0x8d, 0x12, 0xfe, 0x2a, 0xb6, 0xe0, 0xf1, 0xe9, 0x7a, 0x0d, 0x2b, 0x83, 0x99, 0xc9, 0x94, 0x2c, 0x45, 0x10, 0xc3, 0xc8, 0xf9, 0x26, 0xa2, 0x08, 0x1c, 0x42, 0x3d, 0xfe, 0x16, 0x81, 0x2e, 0xaf, 0xa4, 0x41, 0x9a, 0xa6, 0x66, 0x55, 0x26, 0x2a, 0x50, 0xda, 0xae, 0xf6, 0x93, 0xe5, 0x2c, 0xfe, 0x04, 0x7b, 0xb9, 0x82, 0xc9, 0x6f, 0x21, 0xc3, 0x2c, 0x82, 0x38, 0x70, 0x3c, 0x87, 0xbf, 0x31, 0xf2, 0x01, 0x3a, 0x0f, 0x62, 0x81, 0x40, 0xee, 0x16, 0x72, 0x0b, 0x02, 0xbb, 0xf2, 0xf8, 0x58, 0x6b, 0x11, 0x6e, 0x40, 0x8f, 0x63, 0xa3, 0xb6, 0xc2, 0x20, 0x09, 0x08, 0x71, 0x0f, 0xa1, 0x89, 0x6c, 0x42, 0xd6, 0x4f, 0x92, 0x95, 0xb6, 0x83, 0xc4, 0x24, 0xd9, 0xc9, 0xa0, 0xd8, 0x81, 0x45, 0x63, 0xad, 0x31, 0x69, 0x6e, 0x84, 0x36, 0x16, 0x5c, 0x2f, 0xc5, 0x9e, 0xaf, 0x4c, 0x43, 0xdf, 0xc6, 0x07, 0x05, 0xe8, 0x8c, 0x16, 0x65, 0x20, 0xf2, 0xf8, 0x0f, 0xf8, 0x28, 0x71, 0x98, 0xaf, 0xd9, 0x60, 0x7e, 0x72, 0x69, 0xc9, 0xe6, 0x95, 0x22, 0x19, 0xd0, 0x24, 0x9c, 0x36, 0x50, 0xc0, 0x24, 0x4f, 0xb1, 0x4e, 0x3b, 0xc3, 0x49, 0x17, 0x6f, 0x7c, 0x09, 0x81, 0x5a, 0x49, 0xf3, 0x81, 0xb7, 0x9d, 0x5c, 0xf3, 0x73, 0x34, 0x0d, 0x13, 0x2e, 0xf0, 0x04, 0xa3, 0x63, 0xa8, 0x80, 0x3b, 0x57, 0x94, 0x55, 0x8b, 0xba, 0x95, 0xaa, 0x77, 0x83, 0x6b, 0x45, 0xf1, 0x42, 0xeb, 0xe0, 0xd3, 0x63, 0xb0, 0xdd, 0xe2, 0x66, 0xef, 0xaa, 0x39, 0x9c, 0x5a, 0x2e, 0xce, 0xf2, 0xc3, 0x9a, 0xf8, 0x69, 0x84, 0xef, 0x96, 0xe1, 0xb3, 0x0a, 0x23, 0xee, 0x7d, 0x23, 0xf8, 0x16, 0x88, 0x07, 0xfd, 0xeb, 0x88, 0x8f, 0xb1, 0xe8, 0xa4, 0x24, 0xa7, 0x56, 0x67, 0x67, 0x94, 0xc7, 0xac, 0xf2, 0x86, 0xd7, 0x94, 0x37, 0x3c, 0xa3, 0x3c, 0xe6, 0x5e, 0x91, 0xde, 0x05, 0xc1, 0x5d, 0xe2, 0xb3, 0xa2, 0x31, 0xb7, 0xcc, 0xe7, 0x43, 0xaf, 0x25, 0x9d, 0x57, 0xee, 0xbf, 0x44, 0xe6, 0x6d, 0x1c, 0x72, 0x9a, 0xbf, 0x66, 0x4e, 0x9b, 0x9e, 0x3a, 0xad, 0xb8, 0x41, 0xc7, 0x16, 0x39, 0x35, 0x0e, 0x4e, 0xeb, 0x06, 0x93, 0x9d, 0x75, 0xbc, 0xe0, 0x9a, 0xed, 0x3d, 0xb4, 0xb0, 0xbd, 0x17, 0x10, 0x3e, 0xf9, 0x24, 0xb6, 0xbb, 0xaf, 0x64, 0x8e, 0xb6, 0x76, 0xd9, 0xeb, 0xee, 0x9b, 0xbd, 0x0e, 0x8f, 0x3d, 0x20, 0x48, 0x3e, 0xdf, 0x81, 0x78, 0x87, 0x62, 0xf7, 0x5a, 0x04, 0xb9, 0xa3, 0xd2, 0x6c, 0x43, 0xa3, 0x27, 0xde, 0x6c, 0x86, 0x55, 0x45, 0xba, 0x0d, 0x5e, 0xd8, 0x28, 0x48, 0x96, 0xf8, 0x4e, 0xa3, 0x24, 0x2b, 0x8f, 0xe6, 0x16, 0x41, 0xb6, 0xd6, 0x63, 0xdd, 0x9e, 0x18, 0x6b, 0xa9, 0xc9, 0xba, 0x24, 0xef, 0x2b, 0xaf, 0xa7, 0x9d, 0x22, 0x4f, 0x04, 0x79, 0x93, 0x1e, 0xff, 0x57, 0x89, 0xe3, 0xd8, 0x97, 0xaa, 0x64, 0x8c, 0xbd, 0x16, 0x5a, 0x5c, 0x2a, 0x79, 0xc0, 0x3f, 0x07, 0x0d, 0x1b, 0x19, 0x19, 0x0d, 0x50, 0x13, 0x23, 0xc2, 0x2d, 0x8a, 0xa6, 0x1f, 0x7c, 0x7a, 0xfc, 0x5a, 0xa1, 0xa8, 0x22, 0x03, 0xba, 0x04, 0xa0, 0x41, 0xa0, 0x19, 0x80, 0x05, 0x6c, 0x77, 0x0a, 0x43, 0x22, 0x8c, 0x45, 0x70, 0xb4, 0xa5, 0x61, 0xae, 0x0f, 0x9a, 0xad, 0xdb, 0xa6, 0x52, 0x61, 0xd7, 0xdb, 0x4d, 0x48, 0x29, 0x1f, 0xc7, 0x69, 0xf1, 0x1f, 0x82, 0x20, 0xb3, 0x5f, 0x15, 0xaf, 0xf3, 0x0f, 0x4d, 0x33, 0xda, 0x2f, 0x18, 0x09, 0x00, 0x00};
const std::vector<uint8_t> kFlp{0x46, 0x4c, 0x68, 0x64, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x60, 0x00, 0x46, 0x4c, 0x64, 0x74, 0xcb, 0x00, 0x00, 0x00, 0xc7, 0x18, 0x32, 0x00, 0x30, 0x00, 0x2e, 0x00, 0x38, 0x00, 0x2e, 0x00, 0x33, 0x00, 0x2e, 0x00, 0x32, 0x00, 0x33, 0x00, 0x30, 0x00, 0x34, 0x00, 0x00, 0x00, 0xc2, 0x18, 0x4d, 0x00, 0x6f, 0x00, 0x6e, 0x00, 0x20, 0x00, 0x6d, 0x00, 0x6f, 0x00, 0x72, 0x00, 0x63, 0x00, 0x65, 0x00, 0x61, 0x00, 0x75, 0x00, 0x00, 0x00, 0x9c, 0x00, 0xf4, 0x01, 0x00, 0x40, 0x00, 0x00, 0xc0, 0x0a, 0x4b, 0x00, 0x69, 0x00, 0x63, 0x00, 0x6b, 0x00, 0x00, 0x00, 0x40, 0x01, 0x00, 0xc0, 0x12, 0x4c, 0x00, 0x65, 0x00, 0x61, 0x00, 0x64, 0x00, 0x20, 0x00, 0xc9, 0x00, 0x74, 0x00, 0xe9, 0x00, 0x00, 0x00, 0x09, 0x01, 0x41, 0x01, 0x00, 0xe0, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x00, 0x00, 0x41, 0x02, 0x00, 0xe0, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x26, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5a, 0x00, 0x00};

const vsm::sequencer::Track* pisteNommee(const vsm::sequencer::Project& projet,
                                         const std::string& nom) {
    for (const auto& piste : projet.tracks) if (piste.name == nom) return &piste;
    return nullptr;
}
bool rapportContient(const DawImportReport& rapport, const std::string& fragment) {
    for (const auto& ligne : rapport.lines)
        if (ligne.find(fragment) != std::string::npos) return true;
    return false;
}
} // namespace

// --- Le lecteur XML --------------------------------------------------------

VSM_TEST(xml_lit_elements_attributs_et_imbrication) {
    const auto doc = parseXml(R"(<a x="1"><b y="deux"/><b y="trois"/></a>)");
    VSM_ASSERT_EQ(doc.root->name, std::string("a"));
    VSM_ASSERT_EQ(doc.root->attribute("x"), std::string("1"));
    VSM_ASSERT_EQ(doc.root->childrenNamed("b").size(), size_t(2));
    VSM_ASSERT_EQ(doc.root->childrenNamed("b")[1]->attribute("y"), std::string("trois"));
}

VSM_TEST(xml_resout_les_entites_et_l_unicode) {
    const auto doc = parseXml(R"(<a t="Lead &amp; Solo &#233;t&#233;"/>)");
    VSM_ASSERT_EQ(doc.root->attribute("t"), std::string("Lead & Solo été"));
}

VSM_TEST(xml_trouve_un_descendant_a_n_importe_quelle_profondeur) {
    // C'est ce qui rend le lecteur robuste aux versions de Live : on cherche
    // la balise, pas le chemin.
    const auto doc = parseXml("<a><b><c><d Value=\"42\"/></c></b></a>");
    const XmlNode* d = doc.root->find("d");
    VSM_ASSERT(d != nullptr);
    VSM_ASSERT_EQ(d->attribute("Value"), std::string("42"));
}

VSM_TEST(xml_refuse_une_balise_mal_fermee) {
    bool leve = false;
    try { parseXml("<a><b></a>"); } catch (const XmlError&) { leve = true; }
    VSM_ASSERT(leve);
}

VSM_TEST(xml_refuse_un_document_tronque) {
    bool leve = false;
    try { parseXml("<a><b attr=\"x\""); } catch (const XmlError&) { leve = true; }
    VSM_ASSERT(leve);
}

// --- L'import Ableton ------------------------------------------------------

VSM_TEST(ableton_lit_le_tempo_les_pistes_et_les_notes) {
    const std::vector<uint8_t> octets(kAlsClair.begin(), kAlsClair.end());
    const auto resultat = importAbletonLive(octets);

    VSM_ASSERT_EQ(resultat.report.midiTracksImported, 2);
    VSM_ASSERT_EQ(resultat.project.tracks.size(), size_t(2));
    // 128,5 BPM : un nombre à virgule venu d'une autre machine, lu en locale C.
    VSM_ASSERT_NEAR(resultat.project.tempoMap.bpmAt(0), 128.5, 0.05);
}

VSM_TEST(ableton_place_les_notes_en_ticks_depuis_les_temps_en_noires) {
    const std::vector<uint8_t> octets(kAlsClair.begin(), kAlsClair.end());
    const auto resultat = importAbletonLive(octets);
    const auto* basse = pisteNommee(resultat.project, "Basse");
    VSM_ASSERT(basse != nullptr);

    // Quatre notes : trois dans le premier clip, une dans le second à 8 noires.
    VSM_ASSERT_EQ(basse->notes.size(), size_t(4));
    const auto tpq = resultat.project.ticksPerQuarterNote;   // 480

    // La première : temps 0, durée une croche (0,5 noire).
    VSM_ASSERT_EQ(basse->notes[0].startTick, vsm::midi::Tick(0));
    VSM_ASSERT_EQ(basse->notes[0].endTick, vsm::midi::Tick(tpq / 2));
    VSM_ASSERT_EQ(int(basse->notes[0].number), 36);
    VSM_ASSERT_EQ(int(basse->notes[0].velocity), 100);

    // LA DERNIÈRE EST DANS LE SECOND CLIP, qui commence à la huitième noire :
    // sa position est celle du clip PLUS la sienne dans le clip. C'est le
    // piège de ce format, et le seul endroit où un import peut décaler tout un
    // morceau sans que rien ne le signale.
    const auto& derniere = basse->notes.back();
    VSM_ASSERT_EQ(derniere.startTick, vsm::midi::Tick(8 * tpq));
    VSM_ASSERT_EQ(derniere.endTick, vsm::midi::Tick(10 * tpq));
}

VSM_TEST(ableton_reprend_le_decalage_d_un_clip_non_aligne) {
    // Le clip de « Lead & Solo » commence à 1,5 noire : la note doit y être.
    const std::vector<uint8_t> octets(kAlsClair.begin(), kAlsClair.end());
    const auto resultat = importAbletonLive(octets);
    const auto* lead = pisteNommee(resultat.project, "Lead & Solo");
    VSM_ASSERT(lead != nullptr);
    VSM_ASSERT_EQ(lead->notes.size(), size_t(1));
    VSM_ASSERT_EQ(lead->notes[0].startTick,
                  vsm::midi::Tick(resultat.project.ticksPerQuarterNote * 3 / 2));
}

VSM_TEST(ableton_reprend_le_nom_avec_son_esperluette_et_l_etat_muet) {
    const std::vector<uint8_t> octets(kAlsClair.begin(), kAlsClair.end());
    const auto resultat = importAbletonLive(octets);
    const auto* lead = pisteNommee(resultat.project, "Lead & Solo");
    VSM_ASSERT(lead != nullptr);
    VSM_ASSERT(lead->muted);                       // Speaker Value="false"
    const auto* basse = pisteNommee(resultat.project, "Basse");
    VSM_ASSERT(basse != nullptr);
    VSM_ASSERT(!basse->muted);
}

VSM_TEST(ableton_lit_aussi_un_fichier_gzippe) {
    // Le vrai format : un `.als` est gzippé. C'est le chemin complet, de
    // l'octet compressé jusqu'à la note.
    const auto resultat = importAbletonLive(kAlsGzip);
    VSM_ASSERT_EQ(resultat.report.midiTracksImported, 2);
    VSM_ASSERT_EQ(resultat.report.notesImported, 5);
    VSM_ASSERT_NEAR(resultat.project.tempoMap.bpmAt(0), 128.5, 0.05);
}

// --- LA RÈGLE : ce qui n'est pas repris est DIT ---------------------------

VSM_TEST(ableton_dit_que_les_pistes_arrivent_sans_instrument) {
    // Une piste importée n'a AUCUN instrument, et c'est un choix : convertir
    // un patch d'Operator en `vsm.dx7` reviendrait à inventer un son que
    // personne n'a écrit. Ce qui compte est que le musicien le sache.
    const auto resultat = importAbletonLive(kAlsGzip);
    VSM_ASSERT_EQ(resultat.report.tracksWithoutInstrument, 2);
    VSM_ASSERT(rapportContient(resultat.report, "AUCUN instrument assigné"));
}

VSM_TEST(ableton_dit_qu_une_piste_audio_n_a_pas_ete_reprise) {
    const auto resultat = importAbletonLive(kAlsGzip);
    VSM_ASSERT_EQ(resultat.report.audioTracksSeen, 1);
    VSM_ASSERT(rapportContient(resultat.report, "Voix enregistree"));
    VSM_ASSERT(rapportContient(resultat.report, "NON importée"));
}

VSM_TEST(ableton_compte_ce_qu_il_a_lu_dans_son_rapport) {
    const auto resultat = importAbletonLive(kAlsGzip);
    VSM_ASSERT_EQ(resultat.report.clipsSeen, 3);
    VSM_ASSERT_EQ(resultat.report.sourceFormat, std::string("Ableton Live"));
    VSM_ASSERT(resultat.report.sourceVersion.find("Live 11") != std::string::npos);
    VSM_ASSERT(rapportContient(resultat.report, "Total :"));
}

// --- Ce qui doit ÉCHOUER, et le dire --------------------------------------

VSM_TEST(ableton_refuse_un_fichier_vide) {
    bool leve = false;
    try { importAbletonLive({}); } catch (const DawImportError&) { leve = true; }
    VSM_ASSERT(leve);
}

VSM_TEST(ableton_refuse_un_xml_qui_n_est_pas_un_projet_live) {
    const std::string autre = "<?xml version=\"1.0\"?><Cubase><Track/></Cubase>";
    const std::vector<uint8_t> octets(autre.begin(), autre.end());
    bool leve = false;
    try { importAbletonLive(octets); } catch (const DawImportError&) { leve = true; }
    VSM_ASSERT(leve);
}

VSM_TEST(ableton_refuse_un_fichier_corrompu_au_lieu_d_un_projet_vide) {
    // LA DIFFÉRENCE QUI COMPTE : « ce projet n'a pas pu être lu » et non « ce
    // projet est vide ». La seconde ferait chercher pendant des heures.
    auto tronque = kAlsGzip;
    tronque.resize(tronque.size() / 2);
    bool leve = false;
    try { importAbletonLive(tronque); } catch (const std::exception&) { leve = true; }
    VSM_ASSERT(leve);
}

// --- L'import FL Studio ----------------------------------------------------
//
// Le projet d'épreuve est construit DANS le test, octet par octet, selon la
// structure du format : en-tête `FLhd`, bloc `FLdt`, événements dont la taille
// se déduit de l'identifiant, et des notes de vingt-quatre octets. Il contient
// deux canaux (dont un au nom accentué, écrit en UTF-16 comme le fait FL
// depuis la version 12), deux motifs, et un événement d'un octet que le
// lecteur doit sauter sans le comprendre.

VSM_TEST(fl_lit_l_entete_le_tempo_et_le_titre) {
    const auto resultat = importFlStudio(kFlp);
    VSM_ASSERT_EQ(resultat.report.sourceFormat, std::string("FL Studio"));
    VSM_ASSERT_EQ(resultat.report.sourceVersion, std::string("20.8.3.2304"));
    VSM_ASSERT_EQ(resultat.project.title, std::string("Mon morceau"));
    VSM_ASSERT_EQ(int(resultat.project.ticksPerQuarterNote), 96);
    // 128 BPM, écrit en MILLIÈMES dans le fichier (128000).
    VSM_ASSERT_NEAR(resultat.project.tempoMap.bpmAt(0), 128.0, 0.05);
}

VSM_TEST(fl_fait_une_piste_par_canal_du_rack_avec_son_nom) {
    const auto resultat = importFlStudio(kFlp);
    VSM_ASSERT_EQ(resultat.report.midiTracksImported, 2);
    VSM_ASSERT(pisteNommee(resultat.project, "Kick") != nullptr);
    // Le nom accentué vient d'un texte UTF-16 : le lire en octets bruts
    // donnerait « L e a d   É t é » haché de zéros.
    VSM_ASSERT(pisteNommee(resultat.project, "Lead Été") != nullptr);
}

VSM_TEST(fl_lit_les_notes_de_vingt_quatre_octets) {
    const auto resultat = importFlStudio(kFlp);
    const auto* kick = pisteNommee(resultat.project, "Kick");
    // ON VÉRIFIE AVANT DE DÉRÉFÉRENCER, et ce n'est pas une précaution de
    // style : une première version de ces tests écrivait `kick->notes` sans
    // contrôle. Le jour où le lecteur n'a rien lu — ce qui est exactement le
    // cas qu'on veut détecter — la suite entière PLANTAIT au lieu d'afficher
    // un échec, et masquait donc tous les autres résultats.
    VSM_ASSERT(kick != nullptr);
    // Deux notes dans le motif 1, une dans le motif 2.
    VSM_ASSERT_EQ(kick->notes.size(), size_t(3));
    VSM_ASSERT_EQ(int(kick->notes[0].number), 36);
    VSM_ASSERT_EQ(int(kick->notes[0].velocity), 100);
    VSM_ASSERT_EQ(kick->notes[0].startTick, vsm::midi::Tick(0));
    VSM_ASSERT_EQ(kick->notes[0].endTick, vsm::midi::Tick(96));

    const auto* lead = pisteNommee(resultat.project, "Lead Été");
    VSM_ASSERT(lead != nullptr);
    VSM_ASSERT_EQ(lead->notes.size(), size_t(1));
    VSM_ASSERT_EQ(int(lead->notes[0].number), 72);
    VSM_ASSERT_EQ(int(lead->notes[0].velocity), 127);
}

VSM_TEST(fl_pose_les_motifs_bout_a_bout_et_le_dit) {
    // L'arrangement n'est PAS repris : les motifs sont posés à la suite, sur
    // la mesure. La note du motif 2 doit donc commencer après la fin arrondie
    // du motif 1 — ici une mesure de 4 noires = 384 ticks.
    const auto resultat = importFlStudio(kFlp);
    const auto* kick = pisteNommee(resultat.project, "Kick");
    VSM_ASSERT(kick != nullptr && !kick->notes.empty());
    VSM_ASSERT_EQ(kick->notes.back().startTick, vsm::midi::Tick(384));
    VSM_ASSERT_EQ(int(kick->notes.back().number), 38);
    // Et surtout : le musicien doit le SAVOIR.
    VSM_ASSERT(rapportContient(resultat.report, "ARRANGEMENT n'est pas repris"));
}

VSM_TEST(fl_compte_ce_qu_il_a_compris_pour_qu_on_puisse_le_juger) {
    // LE GARDE-FOU DU FORMAT BINAIRE. La structure est certaine, le SENS des
    // identifiants est reconstitué : compter ce qu'on reconnaît permet de voir
    // qu'un lecteur se trompe, au lieu de rendre un projet vide sans un mot.
    const auto resultat = importFlStudio(kFlp);
    VSM_ASSERT(resultat.report.eventsRead >= 10);
    VSM_ASSERT(resultat.report.eventsUnderstood >= 8);
    // L'événement d'un octet, lui, n'est pas compris — et c'est normal.
    VSM_ASSERT(resultat.report.eventsUnderstood < resultat.report.eventsRead);
    VSM_ASSERT(rapportContient(resultat.report, "Événements lus"));
}

VSM_TEST(fl_dit_que_les_canaux_arrivent_sans_instrument) {
    const auto resultat = importFlStudio(kFlp);
    VSM_ASSERT_EQ(resultat.report.tracksWithoutInstrument, 2);
    VSM_ASSERT(rapportContient(resultat.report, "AUCUN instrument assigné"));
}

VSM_TEST(fl_refuse_un_fichier_qui_n_est_pas_un_flp) {
    const std::string autre = "Ceci n'est pas un projet FL Studio du tout";
    const std::vector<uint8_t> octets(autre.begin(), autre.end());
    bool leve = false;
    try { importFlStudio(octets); } catch (const DawImportError&) { leve = true; }
    VSM_ASSERT(leve);
}

VSM_TEST(fl_refuse_un_fichier_tronque_au_lieu_d_un_projet_a_moitie) {
    auto tronque = kFlp;
    tronque.resize(tronque.size() - 30);
    bool leve = false;
    try { importFlStudio(tronque); } catch (const DawImportError&) { leve = true; }
    VSM_ASSERT(leve);
}

// --- Cubase : la Track Archive, et le refus motivé du .cpr -----------------

namespace {
// Une archive de pistes telle que Cubase l'exporte : des objets imbriqués, des
// valeurs nommées, et une hiérarchie dont on ne veut PAS dépendre. Le lecteur
// cherche la signature d'une note (début, longueur, hauteur) où qu'elle soit.
const std::string kArchiveCubase = R"XML(<?xml version="1.0" encoding="utf-8"?>
<tracklist2 version="1">
 <list name="track" type="obj">
  <obj class="MMidiTrackEvent" ID="1">
   <string name="Name" value="Piano"/>
   <member name="Node">
    <obj class="MTrackNode">
     <int name="PPQ" value="480"/>
     <float name="Tempo" value="96"/>
     <list name="Events" type="obj">
      <obj class="MMidiPartEvent">
       <member name="Node">
        <obj class="MMidiPartNode">
         <list name="Events" type="obj">
          <obj class="MMidiEvent">
           <int name="Start" value="0"/>
           <int name="Length" value="480"/>
           <int name="PitchOrValue" value="60"/>
           <int name="Velocity" value="90"/>
          </obj>
          <obj class="MMidiEvent">
           <int name="Start" value="960"/>
           <int name="Length" value="240"/>
           <int name="PitchOrValue" value="64"/>
           <int name="Velocity" value="110"/>
          </obj>
         </list>
        </obj>
       </member>
      </obj>
     </list>
    </obj>
   </member>
  </obj>
  <obj class="MMidiTrackEvent" ID="2">
   <string name="Name" value="Basse"/>
   <member name="Node">
    <obj class="MTrackNode">
     <list name="Events" type="obj">
      <obj class="MMidiEvent">
       <int name="Start" value="0"/>
       <int name="Length" value="1920"/>
       <int name="Pitch" value="36"/>
       <int name="Velocity" value="127"/>
      </obj>
     </list>
    </obj>
   </member>
  </obj>
 </list>
</tracklist2>
)XML";
} // namespace

VSM_TEST(cubase_lit_les_pistes_et_leurs_notes_sans_suivre_de_chemin) {
    const std::vector<uint8_t> octets(kArchiveCubase.begin(), kArchiveCubase.end());
    const auto resultat = importCubaseTrackArchive(octets);

    VSM_ASSERT_EQ(resultat.report.midiTracksImported, 2);
    const auto* piano = pisteNommee(resultat.project, "Piano");
    VSM_ASSERT(piano != nullptr);
    VSM_ASSERT_EQ(piano->notes.size(), size_t(2));
    VSM_ASSERT_EQ(int(piano->notes[0].number), 60);
    VSM_ASSERT_EQ(piano->notes[0].endTick, vsm::midi::Tick(480));
    VSM_ASSERT_EQ(piano->notes[1].startTick, vsm::midi::Tick(960));
}

VSM_TEST(cubase_accepte_les_deux_noms_de_hauteur) {
    // « PitchOrValue » sur les événements génériques, « Pitch » ailleurs :
    // accepter les deux plutôt que de parier sur l'un.
    const std::vector<uint8_t> octets(kArchiveCubase.begin(), kArchiveCubase.end());
    const auto resultat = importCubaseTrackArchive(octets);
    const auto* basse = pisteNommee(resultat.project, "Basse");
    VSM_ASSERT(basse != nullptr);
    VSM_ASSERT_EQ(basse->notes.size(), size_t(1));
    VSM_ASSERT_EQ(int(basse->notes[0].number), 36);
}

VSM_TEST(cubase_lit_le_tempo_de_l_archive) {
    const std::vector<uint8_t> octets(kArchiveCubase.begin(), kArchiveCubase.end());
    const auto resultat = importCubaseTrackArchive(octets);
    VSM_ASSERT_NEAR(resultat.project.tempoMap.bpmAt(0), 96.0, 0.05);
}

VSM_TEST(cubase_dit_quand_le_tempo_manque_au_lieu_d_imposer_120_en_silence) {
    const std::string sansTempo =
        R"(<tracklist2><list name="track" type="obj"><obj class="MMidiTrackEvent">)"
        R"(<string name="Name" value="X"/><obj class="MMidiEvent">)"
        R"(<int name="Start" value="0"/><int name="Length" value="480"/>)"
        R"(<int name="Pitch" value="60"/></obj></obj></list></tracklist2>)";
    const std::vector<uint8_t> octets(sansTempo.begin(), sansTempo.end());
    const auto resultat = importCubaseTrackArchive(octets);
    VSM_ASSERT(rapportContient(resultat.report, "Tempo ABSENT"));
}

VSM_TEST(cubase_dit_quand_il_ne_trouve_aucune_note_et_oriente) {
    const std::string vide = R"(<tracklist2><list name="track" type="obj"/></tracklist2>)";
    const std::vector<uint8_t> octets(vide.begin(), vide.end());
    const auto resultat = importCubaseTrackArchive(octets);
    VSM_ASSERT_EQ(resultat.report.notesImported, 0);
    VSM_ASSERT(rapportContient(resultat.report, "aucune note trouvée"));
    VSM_ASSERT(rapportContient(resultat.report, "MIDI Type 1"));
}

// --- Le choix du lecteur d'après le contenu -------------------------------

VSM_TEST(le_format_est_reconnu_au_contenu_et_non_a_l_extension) {
    VSM_ASSERT_EQ(importDawProject(kFlp).report.sourceFormat, std::string("FL Studio"));
    VSM_ASSERT_EQ(importDawProject(kAlsGzip).report.sourceFormat, std::string("Ableton Live"));
    const std::vector<uint8_t> cubase(kArchiveCubase.begin(), kArchiveCubase.end());
    VSM_ASSERT(importDawProject(cubase).report.sourceFormat.find("Cubase") != std::string::npos);
}

VSM_TEST(un_cpr_est_refuse_avec_les_deux_chemins_qui_marchent) {
    // LE SEUL ENDROIT DU DÉPÔT OÙ L'ON REFUSE DE LIRE : mieux vaut un message
    // qui donne les chemins praticables qu'un import qui invente. Le test
    // vérifie que le message les NOMME — sans quoi le refus serait un mur.
    const std::vector<uint8_t> nImporteQuoi{0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    std::string message;
    try {
        importDawProject(nImporteQuoi, "MonProjet.cpr");
    } catch (const DawImportError& erreur) {
        message = erreur.what();
    }
    VSM_ASSERT(!message.empty());
    VSM_ASSERT(message.find("Archive de ") != std::string::npos);
    VSM_ASSERT(message.find("MIDI Type 1") != std::string::npos);
}

VSM_TEST(un_format_inconnu_est_refuse_en_nommant_ce_qui_est_su_lire) {
    const std::string rien = "un fichier texte quelconque, ni als ni flp ni xml";
    const std::vector<uint8_t> octets(rien.begin(), rien.end());
    std::string message;
    try { importDawProject(octets, "truc.dat"); }
    catch (const DawImportError& erreur) { message = erreur.what(); }
    VSM_ASSERT(message.find("Ableton") != std::string::npos);
    VSM_ASSERT(message.find("FL Studio") != std::string::npos);
}
