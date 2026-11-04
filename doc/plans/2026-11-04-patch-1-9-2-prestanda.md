# Patch 1.9.2 — prestanda och korrekt sparning

**Status:** utförd, version 1.9.2 (utfallet sist i filen). Underlag: [analysen](../research/2026-11-04-prestanda-och-fullstandighet.md).
Numren 1–6 är analysens fel. Radnummer gäller källan vid 1.9.1.

## Bakgrund och mål

Öppning är redan snabb, men sparning kan hänga och utbredningen blir fel. Patchen ska:

1. Få utbredningen att aldrig fastna på ACIS-ytor eller nästlade block.
2. Räkna `$EXTMIN/$EXTMAX` som AutoCAD (osynliga entiteter utesluts) och skriva dem när geometrin i en läst 2000+-ritning har ändrats.
3. Läsa proxy-objekt typade, med AutoCAD:s `bits/8 + 1` byte.
4. Ta bort stack overflow vid nästlade block, utan något nytt tak.
5. Låta VERTEX och SEQEND följa en ändrad POLYLINE.
6. Skriva 16-bitarsvärden över gränsen som AutoCAD, i ASCII och binärt, och säga till i `warnings()`.

Dessutom: snabb väg för oförändrade poster, tal utan `snprintf`/`atof`, och färre kopior.

**Ingår inte:** minnesomarbetning, binärläsning utan textrepresentation, versionskonvertering (fas 8) och MSVC-raden i
[TODO.md](../../TODO.md), som står kvar olöst.

**Mätt med prototyper** (analysen): hel utbredning på en tung ritning från över 74 CPU-minuter till 0,49 s, oförändrad sparning till
0,16 s, R12-sparning 30 s till 6–10 s, binär öppning 5,7 s till 2,4 s. Oförändrade filer förblir byte-identiska.

## Beslut

| Beslut | Innebörd |
|---|---|
| **B133** | Alla sex fel löses här. Det som saknar AutoCAD-facit kallas **ej testat**. |
| **B134** | Inga nya publika metoder. `Drawing::entity_extents` behåller signaturen; ett batchanrop skulle bara spara tolkningen av kropparna, eftersom varje svar måste ha egen låda. |
| **B135** | Utbredningen räknas i en läst ritning bara när något ändrats. Oförändrad: filens värden. Ändrad post: nuvarande mot ursprunglig, som R12:s regel. Borttagen entitet: nuvarande skrivs direkt, eftersom ursprunget saknar den. Programmets eget värde går före. |
| **B136** | Ingen klassning av "ren färgändring": beräkningen är så billig att en ändrad post får jämföras med två körningar. |
| **B137** | R12 skriver brödtexten före headern, som `write_r2000`. Bytena blir desamma, och beslutet om utbredning kan tas efter brödtexten. |
| **B138** | Blockvandring sker med explicita stackar, i utbredning, multileader och tabell. Inget tak på blockdjup. |
| **B139** | Snabba vägen får bara ta poster som `merge_record` bevisligen skriver oförändrade; allt osäkert går den vanliga vägen. |
| **B140** | Talhjälparen läser som `atof` men alltid med decimalpunkt. Den strikta `GroupCursor::real` lämnas orörd. |
| **B141** | 16-bitarsvärden utanför −32768…32767 skrivs som AutoCAD: låga 16 bitar som tal med tecken, i ASCII och binärt (facit: polyface `71 = -25536`, tabell `70 = -25535`). Inget nytt tak, inget fel. Ett typat värde ger en varning i `warnings()`; TABLE:s `70` ger ingen. Text i en ASCII-fil som läses och skrivs oförändrad behålls som den var. |

## Steg

### Steg 1 — utbredning utan explosion (fel 1)

Filer: [src/acis_extents.cpp](../../src/acis_extents.cpp), [src/acis.hpp](../../src/acis.hpp), [src/extents.cpp](../../src/extents.cpp).

**`ExtentsBuilder::entity`, INSERT-grenen (`extents.cpp:1174`):**
- Ett varv per verkligt hörn: `{0, cols - 1}` × `{0, rows - 1}` utan dubletter. En INSERT med en rad och en kolumn räknas en gång.
- `block_extents` bara i grenen för multiplar av 90°. Vridna INSERT behöver inte blockets låda.

**Två pass (`drawing_extents:1376`, intern `entity_extents:1387`):**
- Ny `enum class AcisExtentsPass { Edges, Faces, All }` i `acis.hpp`; `acis_extents(bodies, map, out, pass = All)`.
- Pass ett: bara INSERT och kroppar, bara kanter. Lådan är en delmängd av den sanna och därför en giltig startlåda.
- Pass två: allt. Kroppar räknar bara ytor där pass ett gått samma väg. Blockets låda startar från sin kantlåda ur en egen kantcache;
  finns ingen (block nått via DIMENSION) gäller `All`.

**Höljeskontroll (`BodyExtents::face:1862`):** ny `box_holds`. Spline: alla kontrollpunkter i lådan efter transform, alla vikter
positiva och ändliga. Sfär och torus: kuben runt ytan. Tom låda eller icke-ändliga värden ger ingen genväg.

**`spline_face:2187`:** `best` startar från det bästa av ytans kant och den kända lådan (`out.max[i] - off`, `-(out.min[i] - off)`). `have_point`
sätts bara av en verklig kandidat. **`knot_lines:2262`:** kandidat i lådan testas inte med `inside`.

**3D-stopp (`add_coedge:1314`, `refine:1461`):**
```text
same_point = max(1e-9 × diagonalen i kroppens hörnlåda, 8 × eps × största absoluta hörnkoordinat)
```
Räknas en gång per kropp i `BodyExtents`, och förs in i `FaceRegion`. Lika ändpunkter räcker inte, eftersom en sluten kurva kan ha samma
ändar: intervallets hölje ska också rymmas (rak: ändpunkterna; ellips och spline: `curve_spans:2336`, flyttas före `FaceRegion`). Saknas
ett säkert hölje förfinas som vanligt. Vid stopp:
- `add_coedge`: ett `settled = true`, inget extra `band`.
- `refine`: `slack = eps_`, **`last = false`**, kontrolleras före grenen med `last = true`. Annars ger `crossings:1590` falskt `on_loop`.

**Förfiningsträd:** ny `RefineNode` på namespace-nivå (känd, `dev`, mittpunkt, två barnindex), ett träd per segment i `FaceRegion`. Bara
besökta noder skapas. Index, inga referenser över `push_back`.

**Kroppscache:** tolkade kroppar per `const Entity*` i `ExtentsBuilder`. Temporärer får ingen plats i cachen: `Entity rep`
(`representing_`) tolkas utan cache. `Entity a` och tabellens temporära INSERT är inga kroppar.

### Steg 2 — icke-rekursiv blockvandring (fel 4)

Filer: `src/extents.cpp`, `src/extents.hpp`, [src/mleader_graphics.cpp](../../src/mleader_graphics.cpp), [src/table_graphics.cpp](../../src/table_graphics.cpp),
[src/table_layout.cpp](../../src/table_layout.cpp); gränssnitt i `src/TypedRecordTypes.hpp:508`.

| Väg | Åtgärd |
|---|---|
| `block_extents:764`, DIMENSION `:1235` | Blocklådorna räknas i beroendeordning med explicit stack före användning, så att inre lådor redan finns. |
| Vriden INSERT `:1205` | Ny `InsertFrame` på namespace-nivå (block, transform, index) på en stack på heapen. Inre INSERT läggs på stacken, övriga entiteter går till `entity`. |
| `mleader_extents`-återanropet `:1255` | Inre INSERT går till samma stack. |
| `busy_` | Skyddar aktiv väg. En cykel avbryter bara sin gren, och en låda som påverkats av en cykel cachas inte som färdig. |
| `MLeaderDrawer::draw_block_entity:598`, `draw_insert:987`, `draw_dimension:1026`, inre `MLeaderDrawer:782` | Arbetsstack som bevarar `push`/`pop`, attributordning och ärvda egenskaper. `drawing_` blir uppslag i konstant tid. |
| `TableProxy::draw_insert:304` och `draw_nested:429` | Arbetsstack. |
| `table_block_extents:519` | `TypedExtentsSink::defer_insert_box`: beräkningen mäter blocket på sin egen stack efter att codec:en returnerat och lägger till hörnen. Blir lådan tom frågas codec:en igen med `block_box_empty`, som då faller tillbaka på sitt rutnät (`dxf_writer_table.cpp:359`). |
| `block_box` i `table_layout.cpp:53`, `stamp_loaded_tables:1265` | Samma säkra ingång; omfattas av testen vid läsning. |

Redan säkra, ändras inte: `mleader_dependency_removed`, `generate_dim_blocks`, `older_version_error`, `DxfReader::parse_blocks`, `dimension_block.cpp`.

### Steg 3 — synlighet (fel 2, första delen)

`ExtentsBuilder::entity:785`: `!e.props.visible` utesluts före annotation, codec och blockvandring. Osynlig INSERT utesluter block och ATTRIB.
ATTRIB med `attr.props.visible == false` eller `flags & 1`, och konstant ATTDEF med `flags & 1`, utesluts. Allt är belagt i facit `extents_visibility`, i modell och paper space.
`Drawing::entity_extents` svarar `false` för en osynlig entitet; kommentaren i `hdxf.hpp:7952` skrivs om. Inga regler för frysta eller släckta lager
införs: de påverkade inte resultatet i analysen.

### Steg 4 — header i läst ritning (fel 2, andra delen)

Filer: [src/dxf_writer.cpp](../../src/dxf_writer.cpp), [src/dxf_writer.hpp](../../src/dxf_writer.hpp), [src/dxf_writer_2000.cpp](../../src/dxf_writer_2000.cpp),
[src/dxf_writer_template.cpp](../../src/dxf_writer_template.cpp), [src/drawing.cpp](../../src/drawing.cpp).

- **R12 skriver brödtexten först:** sentinel, tabeller, block, entiteter och resten till en `ostringstream`, sedan header, sedan `std::move(body).str()`. Mönster: `write_r2000:296–318`.
- **Beslutet:** i mallläge körs `compute_extents` inte före brödtexten. Det körs i `write_header_template:1229`, efter att headerns egna skillnader räknats in i `changed_`:
  - `changed_` falskt och inget borttaget: ingen beräkning, inga teckenvarningar.
  - `changed_` sant: nuvarande mot ursprunglig per rymd, med den befintliga koden i `compute_extents:218–231` utvidgad från `r12_template()` till `template_mode()`.
  - Borttagen entitet: nuvarande skrivs direkt för berörd rymd, utan jämförelse (`Drawing::write_dxf:1518` bygger ursprunget ur kvarvarande råposter, så den borttagna saknas där).
- **Borttaget upptäcks:** ny `Drawing::loaded_entity_records_` (modell, papper, block) sätts i `note_loaded:1473`: antalet entiteter med råpost. Färre nu än då betyder borttaget, oavsett tillägg. Block räknas som båda rymderna.
- **`view_min_`/`view_max_`:** behövs bara när en läst fil saknar `*Active` (`dxf_writer_2000.cpp:952`, `dxf_writer.cpp:432`). Då körs beräkningen tidigt, och resultatet återanvänds.
- **`write_header_template:1257`:** R12-spärren tas bort, men företrädesregeln behålls. Talformat: R12 `%.17g` som idag; R13, R14 och 2000+ `format_double(..., NumberStyle::AutoCad)` som `dxf_writer_2000.cpp:649`. Tom rymd följer `compute_extents:238–259`.

### Steg 5 — proxy (fel 3)

Fil: [src/proxy_record.cpp](../../src/proxy_record.cpp). Ny hjälpare `bit_length_agrees(bytes, bits)`, sann vid `(bits + 7) / 8` byte, eller `bits > 0 && bits % 8 == 0` och `bits / 8 + 1` byte.
Används vid `:195` (bara 96 och 162), `:215` och `lengths_agree:401`. **92 och 160 räknas i byte och förblir exakta.** Extra byten bevaras som den är (den kan vara annat än noll).

### Steg 6 — POLYLINE:s underposter (fel 5)

Ny `Drawing::sync_polyline_properties(const Drawing* original)` i nya `src/drawing_polyline.cpp`, anropad från `write_dxf` efter att originalet byggts. Mall: `sync_mtext_columns` i `src/drawing_mtext.cpp:147`.

**Facit `polyline_owner`, alla fyra sorter av POLYLINE, R12 och 2018:** VERTEX och SEQEND har ägarens `8`, `6`, `62`, och från 2000 även `48`, `370`, `440`. INSERT:s SEQEND har ägarens `8`, `62`, `48`, `370`, men inte `6` och `440`. ATTRIB följer inte.

- **Läst:** matcha på handtag; R12 utan handtag på plats (`pair_entities`, `dxf_writer_template.cpp:1574`). Per egenskap (färg, linjetyp, lager): följer ägaren om ägaren ändrats, underposten var lika med ägaren i originalet och inte själv ändrats. Avvikande värden från AutoCAD:s export bevaras.
- **Byggd i minnet:** tomt lager och tom linjetyp följer ägaren; ByLayer följer ägarens färg. API:t kan inte skilja satt ByLayer från standardvärdet, och det dokumenteras.
- **SEQEND:** POLYLINE:s får samma regel.
- **`48`, `370`, `440`:** VERTEX har inga typade fält för dem. Skrivaren tar dem från ägaren när VERTEX och SEQEND skrivs (`write_vertex`, `dxf_writer.cpp:1355`; `dxf_writer_2000.cpp:2615–2653`), från 2000. Oförändrad post skrivs ändå från råposten.
- **INSERT:s SEQEND:** skrivaren ger den ägarens `8`, `62`, och från 2000 `48`, `370` (`dxf_writer_2000.cpp:2688`, och motsvarande för R12).
- **Misslyckad sparning:** fälten återställs (`take_back_all`, `drawing.cpp:1569`).

### Steg 7 — 16 bitar (fel 6)

[src/group_io.cpp](../../src/group_io.cpp): gemensam funktion för 16-bitarsvärden i `write_int16:323`, `write_int:411` och `write_typed:441` (med `strtoll` så att brett värde inte kapas i tolkningen), och därmed `write_raw:561` i binärt. Gäller slutlig skrivning, inte fångst i `sink_`.

- Värde inom −32768…32767: som idag.
- Annat värde: låga 16 bitar som tal med tecken, i binärt som i ASCII (`aligned`). Räknas per posttyp och kod, så att en fil med många värden ger en rad:
```text
POLYLINE group 71: 3 values beyond 16 bits were written as AutoCAD writes them, cut to 16 bits (the first: 40000 as -25536)
```
- `write_typed` och `write_raw` i ASCII behåller texten som den var, så en läst fil förblir byte-identisk. I binärt kapas de med varning.
- Typade kontroller som `wr16` i `dxf_writer_extension_types.cpp:12` ändras inte: de gäller grupper AutoCAD inte skriver så.
- Del E hämtar `wrapped_value_warnings()` till `warnings_` efter skrivningen.

**TABLE `70`:** `GroupWriter::write_table_count`, kapad och utan varning, används i `write_table_head` (`dxf_writer_2000.cpp:819`), `write_table_head_r12` och `write_table_head_template` (`dxf_writer_template.cpp:1508`, `:1191`).

**Läsning:** ett negativt tal behålls som det är. `PolylineData::m_count` kan alltså vara negativt i en läst fil; det dokumenteras i `hdxf.hpp`.

### Steg 8 — snabb väg för oförändrade poster

`src/dxf_writer_template.cpp`, `emit:677` och `emit_r12:1440`. Ny medlem `nothing_removed_`, satt efter relationsförberedelsen. Råposten skrivs med `write_raw`, med `code_columns`, när alla gäller:

1. Original finns och `n == o`, inga skrivfel.
2. `emit`: varje post i `n` har sin råpost via handtag, utan rest åt något håll. `emit_r12`: `n` är inte tom.
3. Inget är borttaget och inga nya handtag är givna; annars krävs att `is_live` godtar alla pekargrupper, och posttypen har inga `RefDescriptor` med andra koder.
4. Inga tomma kända 102-block (`write_block:855`), och ingen `101` i `n` som råposten saknar (`:1147`).

`convert_raw_text` ligger i `write_raw`, så ändrad `$DWGCODEPAGE` fungerar. `changed_` och `dangling_` ändras inte.

### Steg 9 — öppning och grupp-I/O

- **Ny `src/number_parse.hpp`:** `parse_double_compat(const std::string&)`. Hoppar över blanktecken och ett plustecken, kör `from_chars`, och godtar skräp efteråt. Till `atof` vid `0x`, `nan(`, över- och underflöde, och misslyckad tolkning. Ersätter `atof` för gruppvärden i `dxf_reader.cpp` (361), `mleader.cpp:17`, `object_reader.cpp:108`, `mtext_data.cpp` (17), `drawing_roundtrip.cpp:251`. Ändras inte: `dimension_block.cpp`, `fonts.cpp`, `mtext_layout.cpp`.
- **`read_binary`, `group_io.cpp:181`:** `to_chars(..., general, 17)` ger samma text som `%.17g`, med `ec` kontrollerad. NaN-formen med bitmönster behålls. `format_double` ändras inte.
- **Gruppkoder:** `snprintf` bort i `read_ascii:149` (bara när bredden inte är tre), `write_string:301`, `aligned:317`, `write_raw:578`, med `to_chars` och utfyllnad.
- **Strömmar:** `read_null_terminated` med `getline(in, s, '\0')`, fast längd med `sgetn` och rätt feltillstånd vid kort läsning.
- **`preserved_handles`, `template_2000.cpp:70`:** ny intern läsning som stannar vid första `5`/`105` eller `100`, utan `groups()`. Mall: `head_xdictionary`, `dxf_reader.cpp:212`.
- **`entity.data = std::move(d)`** på de 17 ställena. Har `d.vertices` överkapacitet flyttas elementen till en vektor med exakt storlek, så att minnestoppen inte stiger.
- **Text:** `std::move(oss).str()` (`drawing.cpp:1629`), `std::move(body).str()` (`dxf_writer_2000.cpp:318`), CRLF med `memchr`/`memcpy` och en allokering (`drawing.cpp:1631`).

## Mönster att följa

| Mall | Till |
|---|---|
| `src/drawing_mtext.cpp:147` | Synkronisering mot originalet |
| `src/dxf_writer_template.cpp:1574` | R12:s parning utan handtag |
| `src/dxf_writer.cpp:218–231` | Jämförelse av utbredning, teckenvarningar |
| `src/dxf_writer_2000.cpp:296–318` | Brödtext före header |
| `src/dxf_writer_mleader.cpp:403–423` | Iterativ genomgång med besöksmängd |
| `src/dxf_reader.cpp:212–255` | Läsa råpostens huvud utan `groups()` |
| `src/typed_record.cpp:240` | `from_chars` med flyttal |
| `src/dxf_writer_extension_types.cpp:13` | Felet för 16 bitar |

## Risker och kantfall

- **3D-stoppets tolerans är eget val.** Får inte ge falskt `on_loop` eller ändra paritet vid söm och trimning.
- **Polfallet är inte återskapat i ett litet prov.** En kon av `LOFT` och samma kon genomskuren hänger inte idag. Ett handbyggt fall provas, annars redovisas det som ej återskapat.
- **Vridna träd:** en förgrening per nivå ger fortfarande antalet instanser, 2ⁿ, vilket är den verkliga mängden. Bara dubbelräkningen försvinner.
- **Vridna INSERT med array** (fler rader eller kolumner) i djupa kedjor har kvar upp till fyra hörn per nivå.
- **Cykler:** skyddet gäller aktiv väg, inte alla besök. Får inte tappa syskongrenar.
- **Blockinnehåll utan blocklåda:** `block_extents` kvar för 90°-grenen; ej mätt på solidtunga block.
- **Ursprunget saknar borttagna poster:** se B135.
- **Teckenvarningar:** en oförändrad läst 2000+-ritning ger inga längre vid sparning, som R12. Befintliga tester kan behöva ändras efter vad regeln säger.
- **Proxy-poster blir typade:** bevarade som råposter vid oförändrat, men nya typade vägar öppnas.
- **Kapade värden i ASCII:** en typad post med värde över 32767 skrivs nu kapad också i ASCII. Befintliga tester som väntar sig något annat rättas efter facit.
- **R12-buffring:** en extra kopia av brödtexten vid sparning, cirka 1× filstorleken.
- **Minnesväxten** från flyttade vektorer ska kontrolleras i mätningen före commit.

## Plattformar

MSVC, MinGW och Linux GCC ([release.yml](../../.github/workflows/release.yml)). Bara Windows med MinGW körs här; MSVC och Linux skrivs blint.
Flyttals-`to_chars` skyddas med `__cpp_lib_to_chars` och faller annars tillbaka på `snprintf`. Minneslapp: en rad i [TODO.md](../../TODO.md) om vad som ska provas på MSVC och Linux, och commit-meddelandet säger att bara Windows är testat.

## Uppdelning för implementationen

Upp till fem Sonnet-agenter samtidigt, `model: sonnet`, `effort: high`. De startas på Eriks ord. Inga underagenter, stubbar eller platshållare.

**Samordnaren har gjort före start:**
- Låst gränssnitten med riktig kod: `src/acis.hpp`, `src/extents.hpp`, `src/TypedRecordTypes.hpp`, `src/group_io.hpp`, `include/hdxf.hpp`, samt `src/number_parse.hpp` komplett.
- Skrivit facitskript och syntetiska indata och framställt faciten, som har avgjort reglerna i steg 3, 6 och 7.

**Delarna, alla samtidigt:**

| Del | Exklusiva filer | Steg |
|---|---|---|
| **A — ACIS** | `src/acis_extents.cpp`, `test/test_acis_extents_limits.cpp` | 1: 3D-stopp, förfiningsträd, höljeskontroll, `best`, `knot_lines`, passen i `BodyExtents` |
| **B — utbredningens vandring** | `src/extents.cpp`, `test/test_extents_performance.cpp`, `test/test_deep_blocks.cpp`, `test/test_extents_visibility.cpp` | 1: hörn, blocklåda, två pass, kroppscache; 2: blocklådor, `InsertFrame`, `busy_`, svar på tabellens begäran; 3 |
| **C — blockgrafik** | `src/mleader_graphics.cpp`, `src/table_graphics.cpp`, `src/table_layout.cpp`, `test/test_deep_block_graphics.cpp` | 2: `MLeaderDrawer`, `TableProxy`, `table_block_extents`, `block_box`, `stamp_loaded_tables` |
| **D — läsning, tal och proxy** | `src/group_io.cpp`, `src/dxf_reader.cpp`, `src/dxf_reader.hpp`, `src/raw_record.cpp`, `src/template_2000.cpp`, `src/proxy_record.cpp`, `src/mleader.cpp`, `src/object_reader.cpp`, `src/mtext_data.cpp`, `src/drawing_roundtrip.cpp`, `test/test_proxy_lengths.cpp`, `test/test_number_parse.cpp`, `test/test_binary_int16.cpp` | 5, 7 (utom tabellhuvudena), 9 (utom texten) |
| **E — skrivaren och ritningen** | `src/dxf_writer.hpp`, `src/dxf_writer.cpp`, `src/dxf_writer_2000.cpp`, `src/dxf_writer_template.cpp`, `src/drawing.cpp`, `src/drawing_polyline.cpp`, `test/test_extents_header.cpp`, `test/test_preserved_fast_path.cpp`, `test/test_polyline_properties.cpp` | 4, 6, 8, tabellhuvudena i 7, texten i 9 |

**Så samverkar de:**
- Alla arbetar i samma träd och ändrar bara sina egna filer. Behövs ändring i en låst header eller annans fil: fråga samordnaren, ändra inte själv.
- Var och en bygger bara sina egna objektfiler med 0 varningar, och kontrollerar sina testfiler med `-fsyntax-only`. Inga tester körs. Bygg med `-j 3`.
- Del B implementerar svaret på `TypedExtentsSink`-begäran, och del C använder den. Del D implementerar `write_table_count`, och del E använder den. Del A implementerar `acis_extents` med pass, och del B använder den.

**Samordnaren gör efter delarna:** lägger `src/drawing_polyline.cpp` och testfilerna i `src/CMakeLists.txt` och `test/CMakeLists.txt:42`, bygger hela biblioteket och testerna med 0 varningar, rättar sammanfogningsfel, och tar dokumenten.

## Tester

Tester och sabotage skrivs; inget körs förrän före commit. Syntetiska indata ligger i `test/data/synthetic/`. Räkningar sker med en intern, anropslokal `ExtentsCounters` i stället för klockor; djupa fall har en generös processgräns.

| Fil | Test | Sabotage som ska få det att falla |
|---|---|---|
| `test_acis_extents_limits` | `collapsed_spline_edge_stops_in_3d`, `closed_edge_is_not_a_point`, `pole_piece_does_not_claim_on_loop` | Inget stopp; bara ändpunkter; `last = true` |
| | `refine_tree_reuses_deviation` | Trädet töms per fråga |
| | `surface_hulls_skip_only_contained_faces`, `spline_best_uses_known_axis_bound`, `knot_candidate_in_box_skips_inside` | Ingen transform/viktkontroll; `off` utelämnad; filtret bort |
| `test_extents_performance` | `insert_counts_each_array_corner_once` (kedja med 30 vridna nivåer) | Dubbletter av hörn tillbaka |
| | `rotated_insert_computes_no_block_box` | `block_extents` före grenen |
| | `edge_and_full_block_boxes_are_separate`, `body_cache_skips_temporaries` | Gemensam cache; `rep` cachas |
| `test_deep_blocks` | `aligned_100000_load_save`, `rotated_100000_load_save` | Rekursion tillbaka |
| | `cycles_keep_sibling_geometry` | Globalt besöksskydd |
| `test_deep_block_graphics` | `mleader_graphics_iterative`, `table_graphics_iterative`, `table_extents_share_traversal`, `table_stamps_safe_on_load` | Någon rekursiv väg kvar |
| `test_extents_visibility` | `hidden_entities_do_not_count`, `hidden_attributes_do_not_count`, `entity_extents_hidden_has_none` | Kontrollen bort |
| `test_extents_header` | `unchanged_save_computes_nothing` | `compute_extents` ovillkorligt |
| | `moved_geometry_updates_header`, `removed_outer_entity_shrinks_box`, `explicit_header_value_wins`, `missing_active_has_view`, `header_number_format_per_version`, `r12_body_first_keeps_bytes` | Bara R12; jämförelse mot ofullständigt ursprung; alltid beräknat; beräkning för sent; samma format överallt; annan ordning |
| `test_proxy_lengths` | `bit_lengths_accept_extra_byte`, `byte_lengths_stay_exact`, `zero_and_partial_bits_checked` | Bara `(bits+7)/8`; 92/160 samma; alltid en extra |
| `test_polyline_properties` | `children_follow_owner`, `children_keep_overrides`, `child_edits_kept`, `new_children_use_owner`, `sync_rolled_back_on_failure` | Anrop bort; kopiera alltid; matcha på index; ingen återställning |
| `test_binary_int16` | `beyond_16_bits_written_as_autocad` (ASCII och binärt, jämförs med facit när det finns), `wrapped_value_gives_warning`, `read_ascii_text_kept`, `signed_flags_keep_bits`, `table_with_40000_layers_complete`, `table_count_wraps_silently` | En väg kapar inte; ingen varning; texten skrivs om; negativa ändras; tak på poster; varning för räknaren |
| `test_preserved_fast_path` | `matches_merge_for_unchanged`, `checks_descriptor_references`, `keeps_removal_effects`, `rejects_incomplete_mapping`, `checks_structure`, `converts_code_page` | Hoppar över fält; bara kodintervall; bara `n == o`; bara antal; råbytes direkt |
| `test_number_parse` | `matches_atof_forms`, `general17_keeps_text`, `group_code_alignment_kept`, `truncated_binary_rejected`, `handles_scan_heads_only`, `line_endings_and_eof_kept` | Enkel `from_chars`; annan precision; alltid tre kolumner; `sgetn` okontrollerad; `groups()` tillbaka; CRLF även binärt |

Vid förlustfria fel ska testet jämföra värden, inte bara räkna.

### Facit

Skript i `doc/facit/fas9_*.scr` körs av samordnaren med `tools/fas9_facit_run.py`, med indata från `test/data/synthetic/` via `--input`. Filerna blir `doc/facit/acad_*.dxf`, som `test_autocad_facit.cpp:340` tar upp själv. AutoCAD:s värden efter `ZOOM E` har brus upp till cirka 4e-7, så testerna jämför utbredning med toleransen 1e-5.

| Facit | Kärnan klarar det? |
|---|---|
| `extents_visibility`: `60 = 1` på toppnivå, i block, på INSERT; ATTRIB `60` och `70`; ATTDEF; paper space | Klart. Indata `test/data/synthetic/extents_visibility.dxf` |
| `extents_changed`: före, och efter flytt, ändrat block, borttagning, tillägg | Klart |
| `polyline_owner`: 2D, 3D, polygonnät, polyface, INSERT med ATTRIB; ändrad färg, linjetyp, lager, linjevikt med skala och transparens | Klart |
| `int16_polyface`, `int16_layers`: i `doc/facit/large/`, via `tools/gen_int16_limits.py` | Klart |
| `acis_hulls`: sfär, torus, `LOFT` till punkt, `SLICE`, block vid 0°, 30° och 90° | Klart |
| Proxy med jämnt bitantal | Nej, kärnan skapar inga. Regeln bygger på AutoCAD:s egna filer enligt analysen; små egna filer är **ej testat** mot AutoCAD |

**Facit som Erik skapar i AutoCAD:** inget krävs för att bli klar. Visuell kontroll av osynliga entiteter och attribut, samt polfall som kräver modellering i GUI, görs om Erik vill; annars är de **ej testat**. R13 och R14 kan AutoCAD 2025 inte spara: **ej testat**.

### Före commit (en gång)

1. Bygg med 0 varningar. Framställ facit.
2. `python -I tools/run_tests.py`, en gång. Faller något: rätta och kör om bara det som föll.
3. Sabotagen enligt tabellen.
4. Analysens mätningar upprepas på samma underlag, utanför repot: tider, minne, byte-identitet, jämförelse post för post, ändringar, stressfallen och `tools/fas9_verify_batch.py` (AutoCAD, ODA, ezdxf). Inget av det underlaget går in i repot.
5. Rapport: vad som körts och inte, och vad som är **ej testat**. Erik testar; commit och version på hans ord.

## Sista steget — dokumenten

- **`CLAUDE.md`:** utbredningens regler (synlighet, när headern skrivs, ingen blockdjupsgräns), R12:s skrivordning, VERTEX-regeln, felet för 16 bitar, proxyregeln. Versionsraderna på Eriks ord.
- **`README.md`, `README.sv.md`:** korrigeringarna; "Known differences" om något ändras.
- **`TODO.md`:** patchens rad och minneslappen för MSVC och Linux. MSVC-raden står kvar.
- **`doc/facit/fas9_register.md`:** kör om `tools/fas9_facit_register.py`.
- **`test/data/README.md`:** de nya syntetiska filerna.
- **Analysen:** länken till den här planen stämmer; utfallet skrivs i planen.
- **Version 1.9.2, på Eriks ord:** `CMakeLists.txt`, `include/hdxf.hpp` (`struct Version` och headerkommentaren), `CLAUDE.md`, `README.md`, `README.sv.md`, tagg.

## Utfall

Steg 1–9 är utförda. Bara Windows med MinGW är testat; MSVC och Linux är skrivna blint och står i `TODO.md`.

| Mätning | 1.9.1 | 1.9.2 |
|---|---|---|
| Öppna ritning A, R12 ASCII (207 MB) | 3,5 s | 2,0 s |
| Öppna ritning A, R12 binär (121 MB) | 5,7 s | 1,9 s |
| Spara ritning A oförändrad, 2018 | över 74 CPU-minuter, avbröts | 0,15 s |
| Spara ritning A oförändrad, R12 ASCII | 30 s | 5,9 s |
| Hela ritningens utbredning, ritning A | hängde | 0,44–0,54 s |
| Minnestopp vid öppning, ritning A, R12 | 810 MB | 809 MB |
| Minnestopp vid sparning, ritning A, R12 | 2,6 GB | 2,3 GB |
| 100 000 nästlade block | stack overflow | 4 s |

- **Körda:** hela sviten, 1428 godkända; 46 av 46 filer byte-identiska; utbredningen avviker högst 5e-9 från AutoCAD:s sparade header i filerna från 2000 och senare; AutoCAD:s kärna läste de ändrade filerna med samma AUDIT som originalen.
- **Inte körda:** sabotagen i testtabellen. Raden "matcha på index" ger samma resultat som handtag, eftersom ursprunget byggs ur posterna i nuvarande ordning.
- **Inte genomgånget:** FINDING-raderna från `tools/fas9_verify_batch.py`.
- **Ej testat:** R13 och R14 mot AutoCAD-facit, proxy med jämnt bitantal mot AutoCAD, polfallet i ett eget litet prov.
- **Tillägg i genomförandet:** block som alla infogar varandra har fler vägar än som går att vandra, så `kCutBoxesComputed`, `kCutWalks`, `kCutLeaders` och `kBlockMetLimit` avslutar sådana vandringar. En fil AutoCAD kan skapa har inga cykler.
