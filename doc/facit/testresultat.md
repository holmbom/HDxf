# Rundtur mot AutoCAD 2025:s facitfiler: grundorsaker

## Slutresultat efter F1–F22 (2026-10-25)

**476 av 476 filer är byte-identiska** efter läsning och skrivning i samma format (0 differenser, 0 läsfel,
0 skrivfel): 68 per version, 34 per format och version, R12 inklusive (R12: 34 ASCII + 34 binär). Hela
sviten: 616 tester, 616 godkända, 0 hoppade. Rundturen är `autocad_facit_all_local_files_round_trip`
(`r12_must_round_trip = true`), en körning, ca 48 s. Resultatet nedan (311 identiska) är utgångsläget före
rättningarna och står kvar som analysunderlag.

F9–F22 (R12) gjordes med template-läge även för R12: en läst R12-fil skrivs från sina bevarade poster
(`DxfWriter::emit_r12()`, jämförelse mellan ritningen nu och ritningen som den lästes) och bara ändrade
poster skrivs om i AutoCADs form. Vid genomförandet visade facit två saker som rapporten (avsnitt 4)
inte nämnde, båda i R12:
- AutoCAD exporterar en del grupper med fyra kolumner i kodraden (`  70`, `  66`, `  40`, `  62`, `  71`,
  `  72` i de exporterade POLYLINE/VERTEX/TEXT-posterna, 16 000 st): kodradens bredd bevaras nu per grupp
  (`DxfGroup::wide`).
- Två binära filer (`annotative_scale_removed_bin`, `hatch_boundary_moved_bin`) har NaN-doubles med
  bitmönster (oinitierat minne i POINT): bitarna bevaras exakt vid läsning och skrivning.

**Flerradigt ATTRIB före 2018 (F2, regeln):** AutoCAD gör ett ATTRIB per format-körning (en rad delas där
formatet byter, "Second ", "red", " line") i varje INSERT som är skalad eller roterad, och ett ATTRIB per
rad i en oskalad, oroterad INSERT (facit: INSERT 259 mot 25F, 26E, 265). Raden radbryts som MTEXT (25F:
"Second" / "red line" vid \W2). Regeln är införd (`attributes_by_runs()`, körningarna ur
`TextLayout::mtext(..., true)`) och kontrollerad mot alla tre transformerade referenser i
`acad_2013_dxf_blocks_attributes.dxf`: text och pennposition för varje fragment återskapas inom 1e-6
(`attrib_2013_regenerated_fragments_match_autocad`). Att just rotation eller skala (inte något annat som
skiljer INSERT 259) styr är det facit visar; ett dedikerat AutoCAD-facit som varierar en faktor i taget
har inte gjorts (se nedan).

**F10, TABLE 70 när en post läggs till eller tas bort (klart för R12 och 2000–2018, 2026-10-25):** AutoCAD-filerna finns
för alla sju versioner (`acad_<ver>_dxf_annotative_scale_removed_{view,style,ltype}_{added,removed}[_bin].dxf`, gjorda med
`14_annotative.scr`, en session, varje steg från det förra, ASCII precision 16 och binär; skriptet kördes om 2026-10-25
med alla versioner, så R12-filerna är från den körningen). Regeln, belagd av alla sex stegen i ASCII och binär i alla
versioner: (1) en post som tas bort sänker inte filens räknare (VIEW tom men räknat, STYLE 3 med 2 poster, LTYPE 2 med 1
exporterad post i R12 och 2 med 3 i 2000+); (2) varje post som läggs till höjer filens räknare med ett (STYLE 2 till 3,
LTYPE 1 till 2, VIEW + 1); (3) oförändrat medlemskap behåller filens räknare (476-rundturen). HDxf: R12
`DxfWriter::write_table_head_r12()` skriver filens 70 + antalet poster som inte lästs ur filen (`records_read()`); 2000+
`write_table_head_template(table, added)` gör samma för TABLE-posten (alla nio tabeller inklusive BLOCK_RECORD och
DIMSTYLE; `write_raw_record(record, added_to_count)`). Den nya VIEW-, STYLE- och LTYPE-posten som HDxf skriver i 2000+
stämmer med AutoCADs, grupp för grupp, med handtag, ägare och dictionary-relationer.
Tester: `autocad_facit_f10_{view,style,ltype}_{added,removed}` (R12) och `autocad_facit_f10_<ver>_{view,style,ltype}_
{added,removed}` för ver = 2000, 2004, 2007, 2010, 2013, 2018: 6 + 36 = 42 tester, vart och ett ASCII och binär (filen
läses, ändringen görs via API, resultatet jämförs grupp för grupp med AutoCADs fil). Skillnader som är AutoCADs och inte
medlemskapets, uteslutna i jämförelsen (i `test_autocad_facit.cpp`, `facit_differences()`):
- **Alla versioner:** handtag (grupp 5; 2000+ jämförs handtagen som namn: samma HDxf-handtag ska alltid möta samma
  AutoCAD-handtag och tvärtom i alla grupper 5, 105, 330–369, 390–399, 480, 481, 1005, och 0 ska möta 0, så en ägare,
  reaktor eller pekare till fel objekt är en skillnad), `$HANDSEED`, klockan (`$TDUPDATE`, `$TDUUPDATE`, `$TDINDWG`,
  `$TDUSRTIMER`), hatchens seedpunkt i XDATA (1021/1031, oinitierat minne).
- **VIEW-räknaren, alla versioner:** AutoCADs räknare är exakt 10 högre än filens räknare plus tillagda poster i *varje*
  steg i skriptet (R12 10, 21, 31, 41 … 71; 2018 ASCII 19, 30, 40 … 80), också när steget inte rör en vy. Det följer inte av
  någon post och HDxf kan inte göra det. Testet kräver att det är den enda skillnaden och att den är exakt +10. (Den
  första körningen, bara R12, hade +10 en gång och sedan konstant; den iakttagelsen gällde den sessionen.)
- **Bara 2000+:** `$FINGERPRINTGUID` och `$VERSIONGUID` samt XRECORD:en med versions-GUID två gånger (300, 302) och
  sökvägen filen sparades till (303); tiderna 40/41/42 i XRECORD `DWGPROPS COOKIE`; klassernas instansräknare (CLASS 91,
  2004+): AutoCADs antal objekt av klassen i sessionen, som varje spara höjer (14, 20, 32, 48, 64 per steg beroende på
  klass), testet kräver att AutoCADs värde är minst HDxfs; MLEADERSTYLE-reaktorer på handtag som inte finns i filen
  (8 fler i varje steg, objekt sessionen skapade och släppte; objekten som finns i filen jämförs); ordningen i
  `{BLKREFS`-listorna (331), de fem schema-XRECORD:arna (`AcDb_Thumbnail_Schema`, `AcDbDs::...`) som AutoCAD skriver i
  cyklisk ordning ibland (2004 och 2013 binär) och dictionaryn vars poster heter minnesadresser (kod 3, talet jämförs inte;
  posternas handtag jämförs i namnordning); proxygrafiken (310) i proxy-entiteterna MULTILEADER och
  LARGE_RADIAL_DIMENSION (BLOCKS och ENTITIES): AutoCAD gör om den vid varje spara och lämnar några paddingbytes som de
  låg i minnet (efter typsnittsnamnet, i doublarnas oanvända byte), kravet är lika lång och högst 8 bytes olika.
Övrigt är byte för byte lika: räknare, poster, `5`/`330`/`360`-relationer, reaktorer, XDATA och dictionaryn. Alla 560 facitfiler i
`doc/facit` rundtur byte för byte (`autocad_facit_all_local_files_round_trip`); hela sviten 658 av 658.

**Kvar:** Ett dedikerat F2-facit (en faktor i taget) gjordes inte; regeln ovan vilar på de fyra referenserna i facit.

Test: `load_from_memory` + `save_to_memory` i samma format (ASCII eller binärt) för alla 476 filer i `doc/facit/*.dxf`.
Kravet är byte-identiskt resultat. Analys enbart, inget är rättat i HDxf. Skriptet som körde testet ligger i
scratchpad (`roundtrip.cpp`). Alla skillnader har jämförts grupp för grupp (binära filer avkodade till kod/värde-listor).

## 1. Totalt

| Version | Filer | Identiska | Skiljer | Läsfel | Skrivfel | Fel ASCII | Fel binär |
|---|---|---|---|---|---|---|---|
| R12  | 68 | 1  | 67 | 0  | 0  | 34 | 33 |
| 2000 | 68 | 48 | 12 | 2  | 6  | 9  | 11 |
| 2004 | 68 | 48 | 12 | 2  | 6  | 9  | 11 |
| 2007 | 68 | 48 | 12 | 2  | 6  | 9  | 11 |
| 2010 | 68 | 52 | 8  | 2  | 6  | 7  | 9  |
| 2013 | 68 | 54 | 10 | 2  | 2  | 6  | 8  |
| 2018 | 68 | 60 | 4  | 2  | 2  | 3  | 5  |
| **Summa** | **476** | **311** | **125** | **12** | **28** | | |

Resultat: **22 olika grundorsaker**, 8 för 2000–2018 (98 felande filer) och 14 för R12 (67 felande filer).
Dessutom två följdskillnader som inte är egna orsaker (`$VERSIONGUID` i 58 filer, `$HANDSEED` i 46 filer, se 4).

Samband: alla 2000–2018-filer som skiljer har `$VERSIONGUID` ändrat. Det är en följd, inte en orsak, eftersom
`dxf_writer_template.cpp:802-823` ger ny GUID så fort något i utdata skiljer sig. `$HANDSEED` ändras när HDxf
skapar nya handtag (orsak A1 och A2).

Verifiering av de 40 hårda felen: jag byggde en kopia av biblioteket i scratchpad (`scratchpad/hcopy`, repot orört)
med tre minimala ändringar (A3, A4, A5 nedan). Därefter är **alla 40 filer byte-identiska**. Det finns alltså inga
dolda följdfel bakom dessa 40.

Sambandet R12 mot 2000+: 2000+ skrivs från bevarade poster (`RawRecord`, "template-läge"), så AutoCAD:s egenheter
följer med. R12 byggs om helt från den typade modellen, så varje avvikelse i ordning, precision och räknare
i AutoCAD:s R12-fil syns.

## 2. Orsaker, sorterade efter antal filer

Förkortningar: L = läsning, S = skrivning. "Filer" räknar ASCII + binär.

| # | Orsak | Version | Format | Filer | L/S |
|---|---|---|---|---|---|
| B1 | R12: `$EXTMIN/$EXTMAX` räknas om och blir annorlunda | R12 | båda | 56 | S |
| B2 | R12: tabellhuvudets räknare (70) skrivs som antal poster | R12 | båda | 42 | S |
| B3 | R12: extrusion 210/220/230 skrivs före geometrin | R12 | båda | 28 | S |
| B4 | R12: lager (8) före handtag (5) i AutoCAD:s POLYLINE/VERTEX/SEQEND | R12 | båda | 24 | S |
| A1 | DIMENSION utan block får ett nybyggt `*Dn` | 2000–2018 | båda | 24 | S |
| A2 | Flerradigt ATTRIB (före 2018) skrivs om som en ATTRIB per rad | 2000–2013 | båda | 20 | L+S |
| B6 | R12: POLYLINE:s 10/20/30 (x,y) förloras, och 66 står före 10 | R12 | båda | 18 | L+S |
| A3 | RAPIDRT-objekt nekas före 2013 | 2000–2010 | båda | 16 | S |
| B9 | R12: DIMENSION, gruppordning och utelämnade grupper | R12 | båda | 14 | S |
| A4 | MULTILEADER utan 341 ger "linetype "" does not exist" | 2000–2018 | båda | 12 | S |
| A5 | Binär: grupp 71 i DATATABLE är 32-bitars | 2000–2018 | binär | 12 | L (och S) |
| A6 | `{BLKREFS` läggs till på annotativa *U-blockposter | 2000–2007 | båda | 12 | S |
| A7 | Reaktorer från LEADER/HATCH läggs till på INSERT/LWPOLYLINE | 2000–2007 | båda | 12 | S |
| B5 | R12 ASCII: 6 värdesiffror ("%#.6g") i AutoCAD:s polylinjer | R12 | ASCII | 12 | S |
| B7 | R12: VERTEX/SEQEND:s 6/62 (BYBLOCK) förloras | R12 | båda | 10 | L+S |
| B10 | R12: VIEW-posten har 41 före 10/20 | R12 | båda | 8 | S |
| B11 | R12: `$PEXTMIN/$PEXTMAX` | R12 | mest ASCII | 5 | S |
| B12 | R12: XDATA på ATTRIB i INSERT skrivs två gånger | R12 | båda | 4 | S |
| A8 | MTEXT-ram: XDATA 80 mot grupp 90 ger dubblerad ram | 2013 | båda | 2 | L+S |
| B8 | R12: VERTEX:s 40/41 (nollbredder) och XDATA förloras | R12 | båda | 2 | L+S |
| B13 | R12: INSERT med skala 1.0 (41/42/43) skrivs utan skala | R12 | ASCII | 2 | S |
| B14 | R12: TEXT i dimensionsblock har AutoCAD:s egen ordning | R12 | båda | 2 | S |

Nästan alla R12-filer har flera orsaker samtidigt. Endast `EXT` ger fel i 11 filer, endast `TABCNT` i 3, endast
`B3` i 2. Resten har två eller fler (kombinationslista finns i 5).

## 3. Orsaker 2000–2018 (A)

### A1. DIMENSION utan block (24 filer)
Alla `associative` och `associative_endpoint_changed`, ASCII och binär, 2000–2018 (4 per version, 6 versioner).

- Post: ENTITIES DIMENSION `257` (lager ADSK_CONSTRAINTS, dynamisk begränsning "d1=80").
- AutoCAD: `... 100 AcDbDimension  10=40.0 20=-20.0 ... 70=128` utan grupp 2 (inget blocknamn).
- HDxf: `... 100 AcDbDimension  2=*D1  10=80.0 20=-20.0 ... 70=160`. Alltså nytt block (BLOCK_RECORD `3FF`, BLOCK `400`
  med ~160 grupper), definitionspunkten flyttad från 40 till 80, och 70 från 128 till 160 (bit 32).
- Följd: `$HANDSEED` 3FF till 40C, nya poster i BLOCK_RECORD-tabellen och BLOCKS.
- Källa: `src/dxf_writer.cpp:1885` `DxfWriter::generate_dim_blocks` (villkoret
  `!dim.block_name.empty() && dwg.find_block(...)` på ~rad 1900), anropad från `dxf_writer_2000.cpp:217`. R.7-regeln
  "en DIMENSION utan block får ett `*Dn`" gäller även när AutoCAD själv lämnat den utan block.

### A2. Flerradigt ATTRIB före 2018 (20 filer)
`blocks_attributes` och `blocks_attributes_attribute_changed` i 2000, 2004, 2007, 2010, 2013 (5 versioner x 4).
2018 är identisk (inbyggd, 101 Embedded Object).

- Post: INSERT `25F` (blocket HDXF_TAGGED) med attributen NOTES_001..005. AutoCAD har den flerradiga texten
  "First line / Second red line / Third line" som fem ATTRIB: `'First line'`, `'Second'`, `'red'` (egen färg),
  `' line'`, `'Third line'` (handtag 264, 387, 389, 38B, 38D), vardera med eget ACAD_XDICTIONARY (360=388 ...).
- HDxf skriver tre ATTRIB: `'First line'`, `'Second red line'` (nytt handtag `3F1`), `'Third line'`. Raderna är
  ihopslagna, de nya saknar `102 {ACAD_XDICTIONARY`, `2`-namnen blir NOTES_002..004 i stället för
  NOTES_002..005, positionerna får 1 ulp-skillnad (`97.97271937752201` mot `97.972719377522`), och
  DICTIONARY/XRECORD-objekten (`39E`, `3A0`, ... ACAD_MLATT) tas bort. XRECORD `391` får `70=3` (var 4) och
  färre 340.
- Källa: skrivarsidan `src/dxf_writer_2000.cpp:2181` och `:2203` (`attribute_mirror(...)`), `src/attribute_mirror.cpp:77`
  `attribute_mirror()` och `attribute_lines()` (en spegel per rad), läsarsidan `src/drawing_attribute.cpp` (rad ~127-302,
  ihopslagning av spegelposterna till `MultilineAttribute`). AutoCAD delar en rad vid varje formateringsbyte
  (färgen på "red"), HDxf bevarar inte den uppdelningen och inte posternas handtag/dictionaries.

### A3. RAPIDRT-objekt före 2013 (16 filer)
`render_modeldoc` och `render_modeldoc_ibl_background_off`, ASCII + binär, 2000, 2004, 2007, 2010. Skrivfel:
`A RAPIDRT render preset requires DXF 2013 or later`.

- AutoCAD 2025 skriver `RAPIDRTRENDERSETTINGS` och `RAPIDRTRENDERENVIRONMENT` även i 2000–2010-filer (finns i filerna).
- Källa: `src/drawing.cpp:269-270` i `r2013_property()` (`ObjectType::RapidRtRenderSettings/Environment` räknas som
  "2013-egenskap" oavsett läst version).
- Med de två raderna borttagna är alla 16 filer byte-identiska.

### A4. MULTILEADER utan 341 (12 filer)
`dimensions` och `dimensions_bin`, alla 6 versioner. Skrivfel: `MULTILEADER 3C9: linetype "" does not exist`.

- Post: MULTILEADER `3C9`, svansen `340=3C8 90=263168 170=1 91=-1073741824 171=-2 290 291 ...` har ingen grupp 341
  (linjetypen för ledarlinjen). Nästa MULTILEADER `3CF` har `341=14`.
- Läsaren lämnar `leader_linetype` tom, skrivaren skriver alltid 341 och slår upp namnet "".
- Källa: `src/dxf_writer_mleader.cpp:313` (`write_code(341, mleader_reference(... d.leader_linetype ...))`),
  felet kommer från `src/dxf_writer_2000.cpp:114` `linetype_handle()`; läsning `src/mleader.cpp:196`.
- Med 341 utelämnad när namnet är tomt är alla 12 filer byte-identiska.

### A5. Binär: grupp 71 i DATATABLE är 32-bitars (12 filer, läsfel)
`native_records_bin` och `native_records_material_changed_bin`, alla 6 versioner. Fel:
`Unknown group code 256 in binary DXF`.

- Objektet `ACDBDATATABLE` (2000) / `DATATABLE` (2004+), cellvärdet "Bool": AutoCAD skriver grupp 71 som 4 byte
  (`47 00 01 00 00 00` = 71, int32 1; sedan `47 00 00 00 00 00`). `group_value_type(71)` är Int16, så läsaren
  tappar synk (2 byte kvar, nästa kod blir 0 `"G"`, sedan 23552 = 0x5C00 ...) och fastnar på "256".
- Samma tabell används av skrivaren (`group_io.cpp: write_raw/write_typed`).
- Källa: `src/group_codes.hpp:32` `group_value_type()` (60-79 = Int16), `src/group_io.cpp:149-224`
  `GroupReader::read_binary()`. Kodens typ beror här på objekttypen, inte bara koden.
- Med 71 som Int32 inuti DATATABLE/ACDBDATATABLE (både läsning och skrivning) är alla 12 filer byte-identiska.

### A6. BLKREFS på *U-block (12 filer)
`annotative` och `annotative_scale_removed`, ASCII + binär, 2000, 2004, 2007. (2010+ är identiska.)

- Post: BLOCK_RECORD `EEA` (`*U30`) och 12 till (`F0D`, `F27`, `F40`, ... `1021`, blocken `*U31..*U42`).
  AutoCAD: `102 {ACAD_XDICTIONARY 360=10F4 102 } 330=1 ... 2=*U30 340=0`. HDxf lägger till
  `102 {BLKREFS 331=292 102 }` (INSERT `292`).
- AutoCAD listar bara INSERT:ar för `*U43` och `*U44` (`105E`, `3AF`), inte för `*U30..*U42`.
- Källa: `src/dxf_writer_2000.cpp:343-358` (`collect_inserts` fyller `block_refs_` för varje INSERT), skrivs vid `:1237`,
  sammanslås med läst post i `src/dxf_writer_template.cpp:582` (`flush`) och `:478-520` (`write_block`: "pekare som
  postens saknar läggs till").

### A7. Reaktorer från LEADER/HATCH (12 filer)
Samma 12 filer som A6.

- INSERT `39A` (`*U40`): AutoCAD `102 {ACAD_REACTORS 330=39B 102 }`, HDxf `330=39B 330=100B 330=1012 330=1019`
  (LEADER-posterna med `340=39A`).
- LWPOLYLINE `39D`: AutoCAD `330=3A0`, HDxf `330=3A0 330=103D 330=1048 330=1053` (HATCH-poster med `330=39D`
  som gränsobjekt). Scale_removed: `330=2645 330=264B` respektive `2665 266E`.
- Källa: `src/dxf_writer_2000.cpp:347-358` (`reactors_[annotation_handle]` för LEADER, `reactors_[source_handle]`
  för HATCH) och `write_reactors()` `:1378`. AutoCAD 2025:s 2000–2007-export har inte dessa bakåtlänkar för
  annotativa upplösta objekt.

### A8. MTEXT-ram i 2013 (2 filer)
`acad_2013_dxf_text` och `_bin`.

- MTEXT `261`: grupp `90=19` (16 ram + 3 fyllning), men XDATA `ACAD_MTEXT_TEXT_BORDERS` har `80=16`.
- `take_mtext_xdata` kräver `flags == d.background.flags` (16 != 19), så XDATA-blocket tas inte om hand utan ligger kvar
  rått; `mtext_xdata()` skriver dessutom ett nytt block eftersom `90 & 16` (`80=19`, ny LWPOLYLINE `792`). Resultat: dubbelt XDATA-block och en extra
  LWPOLYLINE, `$HANDSEED` 792 till 793.
- Källa: `src/mtext_data.cpp:227-247` (`take_mtext_xdata`) och `:267-276` (`mtext_xdata`).
- 2010 har samma XDATA (`80=16`) men `90=3` (utan ramens bit 16): då skriver `mtext_xdata()` inget nytt block och resultatet blir identiskt. Felet uppstår bara när 90 har bit 16 men XDATA-flaggan inte är exakt lika (2013: 19 mot 16).

## 4. Orsaker R12 (B)

### B1. `$EXTMIN/$EXTMAX` (56 filer)
R12-huvudet räknas om (`src/dxf_writer.cpp:139` `compute_extents`, `src/extents.cpp`) och bevaras inte som i 2000+.
Delorsaker (en fil kan ha flera):

| Del | Filer | Exempel (in = AutoCAD, ut = HDxf) |
|---|---|---|
| Textens storlek/typsnitt (TEXT, MTEXT, tabeller, attribut, dimensionstext) | ~30 | `text`: EXTMAX (567.9647, 30.18) mot (547.3625, 0.0); EXTMIN y -304.3997 mot -303.5116. `unicode`, `tables` (y -35.5942 mot -35.5376), `blocks_attributes` (x -5.2133 mot -5.4908), `dimensions` |
| Objekt som R12 inte har men AutoCAD räknar (IMAGE, HATCH, 3DSOLID, ytor, MATERIAL-testet) | ~26 | `images`: AutoCAD (-8.27e-12,0,0)..(280,203.5294,0), HDxf 1e20/-1e20. `hatch`: EXTMAX y 151.4665 mot 140.3815. `objects`: EXTMAX z 90.0664 mot 40.0. `solids`: y -8.9999 mot -7.6336. `surfaces`: z 50.1 mot 49.9994 |
| Z-brus ±1e-8 | 18 | AutoCAD EXTMIN z `-0.00000001`, EXTMAX z `0.00000001`; HDxf 0.0 (`basic`, `associative`, `annotative`, `dimensions`, `hatch`, `surfaces`) |
| 1-ulp/1e-15 | ~14 | `dynamic_blocks_rotation_changed` (ASCII): `157.042866522874192` mot `157.0428665228742204`, binären är lika; `layers_styles`: -1.2e-15 mot -1.3e-15 |
| 1e20-formatet | 1 ren (`empty`, ASCII) + images | AutoCAD `1.0000000000000000E+20` (16 decimaler), HDxf `1.000000000000000E+20` (15) |

Källor: `src/group_io.cpp:~365` `format_double(NumberStyle::R12)` använder `%.15E` (ska bli 16 decimaler), extents-beräkningen
i `src/extents.cpp`, `src/text_layout.cpp`, `src/mtext_layout.cpp`.

### B2. Tabellräknare (70), 42 filer
R12:s TABLE-huvud har `70` = ett antal AutoCAD håller (inte antalet poster i filen). HDxf skriver antalet poster.

| Tabell | Filer | Exempel (in mot ut) |
|---|---|---|
| VIEW | 38 | `70 10` mot `0` (annotative_scale_removed), `3` mot `0`, `20` mot `0`, `11` mot `1` |
| STYLE | 8 | `3` mot `2`, `4` mot `2` |
| DIMSTYLE | 8 | `3` mot `2`, `5` mot `2` |
| LTYPE | 4 | `5` mot `1` (dynamic_blocks) |

- Tabellen VIEW kan vara tom i filen men ha räknare 10 (AutoCAD räknar bland annat poster som inte skrivs till R12).
  Värdet går inte att räkna ut från filens innehåll. Räknaren läses och kastas.
- Källa: `src/dxf_writer.cpp:547-550` `write_view_table` (`write_code_int(70, views.size())`) och motsvarande
  `write_*_table` för STYLE/DIMSTYLE/LTYPE.

### B3. Extrusion före geometrin (28 filer)
LINE (22 filer), SOLID (6), CIRCLE (4), ARC (4).

- LINE `316` (layouts): AutoCAD `5 8 10 20 30 11 21 31 210 220 230`, HDxf `5 8 210 220 230 10 20 30 11 21 31`.
  ARC: AutoCAD `... 40 210 220 230 50 51`, HDxf `210 220 230 10 ...`. CIRCLE: `40 210 220 230`. SOLID: `... 13 23 33 210 220 230`.
- Källa: `src/dxf_writer.cpp:953-959` i `write_common_entity_codes()` (anropas före geometrin av `write_line`, `write_circle`,
  `write_arc`, `write_solid` ...). 2000+ är ok (annan skrivare).

### B4. Lager före handtag i AutoCAD:s polylinjer (24 filer)
AutoCAD:s R12-export skapar POLYLINE (70=8), VERTEX (70=32) och SEQEND för typer som saknas i R12 (SPLINE, ytor, nät):
15 980 VERTEX i ASCII-filerna har ordningen `0 VERTEX 8 0 5 2D1 10 ...`, medan vanliga (egna) polylinjer har `5, 8`.

- Exempel: VERTEX `2D1`, SEQEND `3E4`, POLYLINE `343`: in `8=0 5=343`, ut `5=343 8=0`.
- Källa: `src/dxf_writer.cpp:1160-1231` `write_polyline()` (handtag före lager för POLYLINE/VERTEX/SEQEND). Posterna
  har ingen markering som skiljer dem från egna polylinjer.

### B5. 6 värdesiffror i ASCII (12 filer, bara ASCII)
Samma polylinjer: ASCII-filens VERTEX/POLYLINE-koordinater har `%#.6g`-format (`170.000`, `73.4810`, `0.00000`, `169.813`).
Den binära filen har full precision, så HDxf läser `169.813` och skriver `169.8129999999999882`.

- Filer: annotative (2), associative (2), basic, dimensions, hatch (3), solids (2), surfaces.
- Källa: `src/group_io.cpp:~365` `format_double` + `write_polyline` (ingen hänsyn till AutoCAD:s 6-siffrors format för dessa poster).

### B6. POLYLINE:s 10/20/30 (18 filer)
- `basic` POLYLINE `272`: AutoCAD `10=170.0 20=73.0 30=0.0 66=1 70=8`; HDxf `66=1 10=0.0 20=0.0 30=0.0 70=8`.
  `solids` POLYLINE `40B`: in `10=112.412 20=73.0193 30=20.431`, ut `0.0 0.0 20.431`.
- Två skillnader: AutoCAD skriver 10/20/30 före 66, och x,y behålls (vid 3D-polylinje); HDxf skriver 66 först och x,y = 0.
- Källa: `src/dxf_writer.cpp:1167-1171` (`write_code_int(66,1); write_code_double(10,0.0); (20,0.0); (30, elevation)`).
  Modellen (`PolylineData`) har bara `elevation`, så x,y förloras vid läsning.

### B7. VERTEX/SEQEND:s 6 och 62 (10 filer)
- VERTEX `2DB` (layers_styles, i block): `6=BYBLOCK 62=0` borta; SEQEND `2DD` samma. `annotative`: VERTEX `8AA`, SEQEND `8AC` `6=BYBLOCK`.
- Källa: `write_polyline` skriver VERTEX/SEQEND med polylinjens lager, utan egen linjetyp/färg
  (`src/dxf_writer.cpp:1195-1230`), och läsaren sparar inte VERTEX:s egenskaper.

### B8. VERTEX:s 40/41 och XDATA (2 filer, `basic`)
- VERTEX `2CF`: `40=0.0 41=0.0` (explicita nollbredder) tappas; `src/dxf_writer.cpp:1206-1211` skriver bredder bara om != 0.
- VERTEX `258`: `1001 HDXF_FACIT 1000 vertex 1070 7` (XDATA på VERTEX) tappas. Det är dataförlust, inte bara formatering.
  `write_polyline` skriver XDATA bara för POLYLINE (rad ~1190), inte för VERTEX.

### B9. DIMENSION i R12 (14 filer)
- `3` (dimstil): AutoCAD utelämnar `3` när stilen är standardstilen (`251`, `297`, `2FC` ...: HDxf lägger till `3=Standard`).
  När den skrivs (annotative: `3=STANDARD`) står den efter 70/1 och före 13, HDxf skriver den direkt efter 2.
- `70`: AutoCAD utelämnar `70=0` (`604`), HDxf skriver alltid 70.
- `16/26/36`: HDxf skriver punkt 16 (=0) för vinkelmått (typ 5, `297`, `636`), AutoCAD inte.
- `40=0.0` på radie/diameter (`2FC`, `312`, `64F`): AutoCAD behåller, HDxf tappar.
- Källa: `src/dxf_writer.cpp:1581-1670` `write_dimension()` (kommentaren på raden `write_code(3, d.dim_style)` hänvisar till R12-referensens
  ordning; AutoCAD 2025 gör annorlunda).

### B10. VIEW-post (8 filer)
`layers_styles` (4), `render_modeldoc` (4). Post `HDXF_RENDER`: AutoCAD `70 40 10 20 41 11 ...`, HDxf `70 40 41 10 20 11 ...`.
Källa: `src/dxf_writer.cpp:547-566` `write_view_table()` (41 före 10/20).

### B11. `$PEXTMIN/$PEXTMAX` (5 filer)
- `layouts` (2): `$PEXTMAX` y `190.0586630286494` mot `185.0` (pappersrummets text/objekt räknas olika).
- `dimensions`, `solids`, `solids_solid_rotated` (alla ASCII): `$PEXTMIN` `1.1692913385826771 / 0.8267716535433073` mot
  `...778 / ...074`. Binärfilerna är lika.
- Källa: `src/dxf_writer.cpp:139-170` `compute_extents`.

### B12. XDATA på ATTRIB i INSERT dubbleras (4 filer: `annotative*`)
- ATTRIB `748`, `75C`, `76F` i blocket `*U44`: XDATA `1001 ACADMATTDEFINANNOBLOCKDECO ...{ 1070 1 1070 1 }` kommer ut två gånger.
  Läsaren har bara en lista.
- Källa: `src/dxf_writer.cpp:1386` (`write_insert`: `write_xdata(attr.props.xdata)` efter `write_attrib(...)`) och
  `write_attrib()` rad ~1468 (`write_xdata(attr.props.xdata)`). Samma lista skrivs i båda.

### B13. INSERT med skala 1.0 (2 filer, ASCII: `associative`, `associative_endpoint_changed`)
INSERT `2B3` (`*U5`, i block): AutoCAD `... 30=0.0 41=1.0 42=1.0 43=1.0 50=74.99`, HDxf utan 41/42/43 (standardvärden utelämnas).
Källa: `write_insert` i `src/dxf_writer.cpp:~1340`.

### B14. TEXT i dimensionsblock (2 filer, `dimensions`)
TEXT `594`/`595` i `*D`-block: AutoCAD `0 TEXT 8 0 40 0.18 5 594 62 0 72 0 71 0 10 134.719 20 19.5427 30 0.00000 1 ...`
(egen ordning, explicit 72/71=0, 6 siffror); HDxf `5 8 62 10 20 30 40 1`. Dimensionsblock som AutoCAD byggt vid exporten
läses som vanliga TEXT och skrivs i vanlig ordning. Källa: `write_text` i `src/dxf_writer.cpp`.

## 5. Filer per orsak, R12 (kombinationer)

| Kombination av orsaker | Filer |
|---|---|
| bara B1 | 11 |
| B1+B2 | 9 |
| B1+B3+B4/B6+B2 | 6 |
| B1+B3+B2 | 6 |
| B1+B3+B4/B6 | 4 |
| B3+B2+B10 | 4 |
| bara B2 | 3 |
| bara B3 | 2 |
| B1+B3 | 2 |
| B1+B4/B6 | 2 |
| B12+B9+B1+B4 | 2 (+2 med B2) |
| B9+B1+B4+B13+B2 | 2 |
| B9+B1+B4+B2 | 2 |
| B9+B1+B4+B10 | 2 (+2 med B2) |
| B3+B11+B2 | 2 |
| B1+B3+B11+B4+B2 | 2 |
| B9+B14+B1+B11+B4+B2 | 1 (+1 binär utan B11) |

(B4/B6 räknas här som "POLY" i klassificeringen, se kolumnen "Filer" i 2 för varje delorsak.)

## 6. Prioritet för rättning (vad tar bort flest fel)

| Orsak | Filer som blir identiska om bara den rättas |
|---|---|
| A3 + A4 + A5 | 40 (verifierat) |
| A1 | 24 (alla associative, följd: HANDSEED/GUID) |
| A2 | 20 |
| A6 + A7 | 12 |
| A8 | 2 |
| B1 (extents) | R12: 11 |
| B2 (tabellräknare) | R12: 3 |
| B3 (extrusion) | R12: 2 |

R12 kräver att nästan alla B-orsaker rättas innan något större antal filer blir identiska (B1 och B2 finns i 56 respektive 42 av 67).

## 7. Metod och filer i scratchpad

- Läsare/skrivare: `scratchpad/facit/rtdump.cpp` skriver utdata till `scratchpad/facit/out/`, jämförelse i Python
  (`dxfparse.py`, `mkdiffs.py`, `annot.py`, `cls.py`): båda filerna avkodas till (kod, värde)-listor, `diff` ger
  skillnaderna, och varje skillnad märks med sektion/posttyp/handtag.
- Kopian med de tre hjälpändringarna: `scratchpad/hcopy` (byggd med Ninja), körd mot de 40 felande filerna.
  Ändringarna: `group_io.cpp` (71 som Int32 i DATATABLE/ACDBDATATABLE, läsning + `write_raw`/`write_string`),
  `drawing.cpp` (RAPIDRT-raderna borta), `dxf_writer_mleader.cpp:313` (hoppa över 341 när namnet är tomt).
- Inga filer i repot utom denna är ändrade.
