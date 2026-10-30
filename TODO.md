# HDXF — TODO

## Att göra

- [ ] **1.9.2: MSVC-felet i materialkorpusen** — med MSVC faller
  `mvs_corpus_every_material_and_visual_style_is_typed_and_written_back_as_it_was`: `dxf_unicode.dxf` skrivs inte
  tillbaka som den lästes när dess poster skrivs ny ur de typade värdena. MinGW och Linux (g++) går igenom.
  Orsaken är inte utredd.

## Klart

- [x] **DXF R12 (AC1009)** — Komplett implementation, verifierad i AutoCAD.
  ASCII + Binary, alla 14 entitetstyper, 8 tabelltyper, header-variabler,
  blocks, attribut, dimensioner, viewports, XDATA, ECS.
- [x] **Öppen källkod (1.9.1)** — MIT ([LICENSE](LICENSE)); licensspärren och dess API (`LicenseData`,
  `SetLicense()`, `HasValidLicense()`) är borttagna. Install-regler, CMake-paketet `find_package(hdxf)` och
  färdigbyggda paket (MSVC, MinGW, Linux GCC) från `.github/workflows/release.yml`.
- [x] **UTF-8-sökvägar (1.0.2)** — `load()`/`save_as()` tar `std::filesystem::path`, så
  strömkonstruktorn väljer wide-överlagringen på Windows i stället för `fopen` i
  ANSI-kodsidan. `hdxf::path_from_utf8()` / `hdxf::utf8_from_path()` konverterar
  till/från UTF-8-`std::string` via `char8_t` (toolchain-oberoende, till skillnad från
  `fs::path(str)`). All I/O rensar numera `last_error()` vid ingången och sätter det på
  varje felväg — `save()`/`save_as()`/`save_to_memory()` returnerade tidigare tyst false.
  Tester i `test/test_paths.cpp`.
  ⚠️ Verifierad på Linux (icke-regression) — själva buggen kan bara reproduceras i
  Windows-bygget.
- [x] **Rättningsfas R.1–R.9 (1.3.1, 2026-10-24)** — inga förenklingar efter 1.3.0: heltal
  som AutoCAD, DIMSTYLE, layouter, `$LASTSAVEDBY`, alla kodsidor, exakta SPLINE-extents,
  måttblock som AutoCAD, typsnitt och text i extents, ACIS i extents — plan:
  [doc/plans/2026-10-23-fas-r-rattningar.md](doc/plans/2026-10-23-fas-r-rattningar.md)

---

## Kända avvikelser i R12 (upptäckta i fas 0)

Alla rättade (2026-10-23, före fas 1 steg 1.4) — som AutoCAD skriver R12 (ACadSharps
`sample_AC1009_ascii.dxf`). R12-golden ändrade och kontrollerade (TrueView, ODA, ezdxf).

- [x] **Block med samma namn slogs ihop vid inläsning** (funnen i 0.2, rättad i 0.8).
  Läsaren gick via `Drawing::add_block()`, som återanvänder ett block med samma namn.
  Enligt planens 0.8 lägger läsaren nu varje BLOCK (och LAYER) direkt i listan — båda
  bevaras, som i filen. `find_block()` ger det första. ezdxf:s
  `r12_blocks_with_no_names.dxf` ger nu 4 block, som facit.

- [x] **`$EXTMIN`/`$EXTMAX`** räknades på blockdefinitionernas entiteter i blockets egna
  koordinater, och INSERT transformerades inte. Nu (`src/extents.cpp`): modellrymden, en
  INSERT som blockets extents transformerade av insättningen, en DIMENSION genom sitt block,
  bågar exakt i OCS; `$PEXTMIN`/`$PEXTMAX` för paper space utan viewport id 1; tom ritning
  1E+20/−1E+20 (och standardvyn visar då gränserna). Stämmer med ezdxf:s bbox. Text räknas
  på insättnings- och justeringspunkt — glyfrutorna kräver typsnittet.
- [x] **`$HANDLING` var 0** men handles skrevs ändå. Nu som AutoCAD: `$HANDLING 1`, handles
  direkt efter `0` på entiteter, ATTRIB, VERTEX, SEQEND och ENDBLK (även i genererade
  dimensionsblock), inga på tabellposter eller BLOCK. På köpet: XDATA för POLYLINE och INSERT
  skrevs efter SEQEND, på SEQEND-posten, och försvann vid läsning — nu på sin egen post.
- [x] **Precision i ASCII:** flyttal skrevs med `%.10f` och `%.6E`. Nu som AutoCAD:
  `%.16f` utan avslutande nollor (DXFOUT:s 16 decimaler; `25.4` blir `25.3999999999999986`
  som i AutoCAD:s fil), `%.15E` från 1e16. Headervärden lagras med `%.17g`.

---

## Kommande DXF-versioner

Mål: läsa och skriva alla versioner R12 → 2018. Varje klar fas är en utgåva och höjer
minor-versionen (fas 0 → 1.1.0, fas 1 → 1.2.0 …); delstegen committas utan tagg,
rättningar efter en utgåva höjer patch-versionen (1.1.1 …); när alla
faser är klara blir det **2.0.0**. Alla strängar i API:t blir
UTF-8 i fas 0, steg 0.7 — brytande ändring (se fas 0-planen, B1 och B9). Ordningen följer formatets utveckling —
det stora språnget är R12 → R13/2000 (handles, ägare, subclass-markörer, OBJECTS).
Versionerna efter 2000 är tillägg ovanpå det. 2000 görs först eftersom det är den
vanligaste basnivån och den struktur alla senare versioner bygger på. R13/R14 tas sist:
de är sällsynta och i stort sett en delmängd av 2000.

Varje fas: läs + skriv + rundtur-tester + verifiering i AutoCAD, som för R12.
Okända entiteter/objekt bevaras som råa gruppkoder i alla versioner.

- [x] **Fas 0 — Grund för flera versioner** (1.1.0, 2026-10-23) — plan: [doc/plans/2026-10-22-fas-0-grund.md](doc/plans/2026-10-22-fas-0-grund.md)
  - [x] 0.1 Regressionsbas för R12 + sex R12-buggar
  - [x] 0.2 Testfiler (nedladdning + facit)
  - [x] 0.3 Gruppkodslagret
  - [x] 0.4 Bevarade poster
  - [x] 0.5 Handles och ägare
  - [x] 0.6 Läsa 2000+ med befintliga parsers
  - [x] 0.7 Textkodning (UTF-8)
  - [x] 0.8 Namnindex
  - [x] 0.9 Slå på nya versioner
  - `DxfVersion` med alla versioner; läsaren godtar alla `$ACADVER` från AC1009 till AC1032
  - Versionsstyrd läsare/skrivare (en gemensam väg för 2000+, egen för R12)
  - Handles obligatoriska, ägar-handles (`330`/`360`), reactors (`102 {ACAD_REACTORS}`)
  - Subclass-markörer (`100 AcDb…`) i läsning och skrivning
  - Binär DXF för R13+: gruppkoder alltid 2 byte (R12 har 1 byte + 255-escape)
  - Namnindex för block/lager (`find_block`/`add_block` är linjära i dag → O(n²) vid
    tiotusentals anonyma block)

- [x] **Fas 1 — DXF 2000 (AC1015)** — klar, 1.2.0 — plan: [doc/plans/2026-10-23-fas-1-dxf-2000.md](doc/plans/2026-10-23-fas-1-dxf-2000.md)
  - [x] 1.1 Objektmodell, BLOCK_RECORD och layouts (läsning)
  - [x] 1.2 Skrivare för 2000 — ritning i minnet
  - [x] 1.3 Gemensamma fält i 2000
  - [x] 1.4 Rundtur för filer lästa från 2000
  - [x] 1.5 LWPOLYLINE, ELLIPSE, XLINE, RAY
  - [x] 1.6 MTEXT, TOLERANCE, LEADER
  - [x] 1.7 SPLINE, HATCH
  - [x] 1.8 MLINE, IMAGE, 3DSOLID/REGION/BODY
  - [x] 1.9 Utgåva 1.2.0 (taggad `v1.2.0`)
  - Sektioner: `CLASSES`, `OBJECTS` (dictionaries, `ACAD_GROUP`, `ACAD_LAYOUT`, layouts)
  - Tabell `BLOCK_RECORD`, `*Model_Space`/`*Paper_Space`, flera layouts/paper spaces
  - Nya entiteter: `LWPOLYLINE`, `MTEXT`, `ELLIPSE`, `SPLINE`, `HATCH`, `XLINE`, `RAY`,
    `LEADER`, `TOLERANCE`, `MLINE`, `IMAGE`, `3DSOLID`/`REGION`/`BODY` (ACIS bevaras rått)
  - Lineweight (`370`), plot style (`390`), `$DWGCODEPAGE`-text
  - OBJECTS vid skrivning som AutoCAD skriver dem (inget eget minimum)

- [x] **Fas 2 — DXF 2004 (AC1018)** — klar, 1.3.0 — plan: [doc/plans/2026-10-23-fas-2-dxf-2004.md](doc/plans/2026-10-23-fas-2-dxf-2004.md)
  - [x] 2.1 Skrivare för 2004 (header, CLASSES `91`, VIEWPORT `170`, plot `76`–`78`)
  - [x] 2.2 True color och färgböcker (`420`, `430`, DBCOLOR)
  - [x] 2.3 Transparens (`440`, lagrets `AcCmTransparency`)
  - [x] 2.4 MTEXT-bakgrund och HATCH-gradient
  - [x] 2.5 `THUMBNAILIMAGE` (typad)
  - [x] 2.6 Utgåva 1.3.0
  - True color (`420`) och färgnamn (`430`) på entiteter och lager
  - `THUMBNAILIMAGE`-sektion
  - Transparens (`440`) — flyttad hit från fas 4: AutoCAD skriver den redan i 2004

- [x] **Fas 3 — DXF 2007 (AC1021)** — klar, 1.4.0 (2026-10-24) — plan: [doc/plans/2026-10-24-fas-3-dxf-2007.md](doc/plans/2026-10-24-fas-3-dxf-2007.md)
  - [x] 3.1 Planen
  - [x] 3.2 Skrivare för 2007 (header, tabellernas och entiteternas 2007-koder, MATERIAL och
    VISUALSTYLE i en ny ritning, UTF-8, attributens låsning)
  - [x] 3.3 MULTILEADER och MLEADERSTYLE — data
  - [x] 3.4 MULTILEADER — layout, proxygrafik och extents (`add_mleader()`, AutoCAD:s
    proxygrafik, extents med textens celler)
  - [x] 3.5 Ytor och HELIX (typade, skrivs i 2007; extents via ACIS och spiralens polylinje)
  - [x] 3.6 Fasens slut (README, CLAUDE.md, golden genom B7, TrueView-filer)
  - [x] Text i UTF-8 i stället för kodsida; `\U+XXXX`-escapes vid läsning/skrivning av äldre
  - [x] `MULTILEADER` och tillhörande objekt (`MLEADERSTYLE`)
  - [x] Ytorna (AcDbSurface: `SURFACE`, `EXTRUDEDSURFACE`, `LOFTEDSURFACE`, `REVOLVEDSURFACE`,
    `SWEPTSURFACE`, `PLANESURFACE`) och `HELIX` typade, med ACIS-data och exakta extents som R.9

- [x] **Fas 4 — DXF 2010 (AC1024)** — klar, 1.5.0 (2026-10-24) — plan: [doc/plans/2026-10-24-fas-4-dxf-2010.md](doc/plans/2026-10-24-fas-4-dxf-2010.md)
  - [x] 4.1 Planen
  - [x] 4.2 Skrivare för 2010 (header, 2010-koderna i tabeller och entiteter, proxystorlek `160`,
    VISUALSTYLE i 2010:s form, IMAGE:s proxygrafik, tecken över U+FFFF som surrogat-escapes)
  - [x] 4.3 MULTILEADER i 2010 (`270`, `271`–`273`, ledarlinjernas egenskaper, vertikalt fäst text)
  - [x] 4.4 MESH typad (`add_mesh()`, proxygrafik, 2000 … 2010)
  - [x] 4.5 MESH:s utjämning och extents (AutoCAD:s regel, B49)
  - [x] 4.6 GEODATA typad (version 1, 2 och 3)
  - [x] 4.7 Ytor och HELIX i 2010
  - [x] 4.8 Fasens slut (README, CLAUDE.md, golden genom B7)
  - [x] `MESH`, `GEODATA` (transparens gjordes i fas 2)
  - [x] MULTILEADER i 2010: `270`, ledarlinjernas egna grupper (`170`, `92`, `340` … efter `91`, läses
    redan), `271`–`273`, och vertikalt fäst text (`271 1`: ingen landning, fäste 9/10 —
    proxygrafiken för det); MLEADERSTYLE `179`, `271`–`273`
  - [x] Ytor och HELIX skrivna i 2010 (proxygrafiken i `160`)
  - [x] `MESH` i extents (hörnpunkterna vid nivå 0, den utjämnade ytan över nivå 0 som
    AutoCAD) — ACadSharps hela ritning får då AutoCAD:s `$EXTMIN` z (−5.0917, MESH 343)

- [x] **Fas 5 — DXF 2013 (AC1027)** — klar, 1.6.0 (2026-10-25) — plan: [doc/plans/2026-10-24-fas-5-dxf-2013.md](doc/plans/2026-10-24-fas-5-dxf-2013.md)
  - [x] 5.1 Planen
  - [x] 5.2 Skrivare för 2013 (header med `$REQUIREDVERSIONS`, LAYER `348`, linjetypens upright,
    MLEADERSTYLE `298`, AcDbModelerGeometry `290`/`2`, VISUALSTYLE i 2013-formen, ny ritning som
    AutoCAD:s, ACDSDATA-schemana)
  - [x] 5.3 2013:s egenskaper i 2000 … 2010 (round-trip-XRECORD för upright, MULTILEADER `295` och
    MLEADERSTYLE `298`, GUID som XDATA `ACAD_OBJECT_CHANGE_GUID`, entitetens utökningsdictionary)
  - [x] 5.4 ACDSDATA typad (kropparnas SAB, layouternas och vyernas förhandsbilder, relationerna)
  - [x] 5.5 SAT → SAB (1): kodaren, topologin och den analytiska geometrin (21200/21500 → 21800)
  - [x] 5.6 SAT → SAB (2): spline-geometrin och resten av posttyperna; SAT-radernas 1/3 som AutoCAD:s
  - [x] 5.7 SAT → SAB (3): äldre ACIS-versioner (106, 400, 600, 700, 20800); toleranta poster;
    SAT-rader i referensens form räknade som de står i filen
  - [x] 5.8 Ytor, HELIX, MESH och GEODATA (version 3) i 2013; varje typ från 2010 skriven på nytt
    i 2013 som i 2010
  - [x] 5.9 Fasens slut (README, CLAUDE.md, golden genom B7); en post skriven på nytt behåller det
    AutoCAD:s har (reactors, utökningsdictionaries, BLOCK:s egenskaper, LTYPE `340`, måttens `42`,
    MTEXT-delarna, små tal, ARC `210`, splinevikter … — 2000 … 2013)
  - `ACDSDATA`-sektionen typad; ACIS skrivs i 2013 som SAB 21800 som AutoCAD (en SAT-kropp
    konverteras; AutoCAD:s läsning underkänner SAT och äldre SAB i 2013)

- [x] **Fas 6 — DXF 2018 (AC1032)** — klar, 1.7.0 (2026-10-25) — plan: [doc/plans/2026-10-25-fas-6-dxf-2018.md](doc/plans/2026-10-25-fas-6-dxf-2018.md)
  - [x] 6.1 Planen
  - [x] 6.2 Skrivvägen, headern (`$ACADMAINTVER` som `90`), standardstrukturen och rundturen
    (ACadSharps båda 2018-filer byte för byte)
  - [x] 6.3 Inbäddade delar (`101 Embedded Object`) och typad läsmodell för text
  - [x] 6.4 MTEXT:s skrivning, kolumner, ram och layout
  - [x] 6.5 Flerradiga attribut, äldre hjälpobjekt och extents
  - [x] 6.6 Objektens nya basfält (vystilarna) och versionsdata (DBCOLOR, LIGHT, SUN, VISUALSTYLE)
  - [x] 6.7 Koordinationsmodell och RapidRT enligt 2018-referensen
  - [x] 6.8 SAT → SAB 22300 och ACDSDATA för alla kroppstyper
  - [x] 6.9 Övriga entiteter, redigering och hela regressionsmatrisen
  - [x] 6.10 Fasens slut, dokumentation och B7

- [x] **Fas 7 — DXF R14 (AC1014) och R13 (AC1012)** — klar, 1.8.0 (2026-10-27) — plan: [doc/plans/2026-10-25-fas-7-dxf-r13-r14.md](doc/plans/2026-10-25-fas-7-dxf-r13-r14.md), format: [doc/reference/hdxf_r13_r14_format.md](doc/reference/hdxf_r13_r14_format.md)
  - R14: som 2000 utan lineweight, plot style och layouts
  - R13: som R14 utan `LWPOLYLINE` (polylinjer som `POLYLINE`/`VERTEX`)

- [x] **Fas 9 — Typer som bevaras men inte är typade** — klar, 1.9.0 (2026-10-30) — plan: [doc/plans/2026-10-27-fas-9-typer.md](doc/plans/2026-10-27-fas-9-typer.md), register: [doc/facit/fas9_register.md](doc/facit/fas9_register.md)
  - [x] 9.0 Grund och gemensamt integrationsavtal (`TypedRegistry`, codec, `VersionMatrix`, facitregister)
  - [x] 9.1 Tabeller: ACAD_TABLE, TABLESTYLE, TABLECONTENT, CELLSTYLEMAP
  - [x] 9.2 Befintliga småtyper, BLOCK/ENDBLK:s färger, hörn-id
  - [x] 9.3 OLE och proxyklasser med verklig nyttolast
  - [x] 9.4 Ritordning, klippning och index
  - [x] 9.5 Tabellayout, `*T`-block, grafik och extents
  - [x] 9.6 Underlägg, coordination model och xref-paneler
  - [x] 9.7 MATERIAL, alla VISUALSTYLE-former och entitetens stilfält
  - [x] 9.8 FIELD, värdetabeller och övriga dokumentdata
  - [x] 9.9 Evalueringsgraf och dynamiska blockbaser
  - [x] 9.10 ACDBASSOC-baser, persistenta subentiteter och observerade kroppar
  - [x] 9.11 Render, LIGHT/SUN, LIGHTLIST och SUNSTUDY
  - [x] 9.12 SECTION-familjen och DETAIL/SECTION view styles
  - [x] 9.13 Dynamiska block, parametrar, actions och constraints
  - [x] 9.14 ACSH, historik och källgeometri
  - [x] 9.15 Övriga ACDBASSOC: constraints, parametrar, operationer och arrays
  - [x] 9.16 NURBSURFACE och 2000/2004-ytornas isolinjer (proxygrafik)
  - [x] 9.17 Annotationskontexter, DIMASSOC och specialmått
  - [x] 9.18 AutoCAD Map samt TurboCAD- och ZWM-utökningarna
  - [x] 9.S Granskning (Codex, 18 fynd åtgärdade), rättningssvep, verifieringsbatch (3 851 original, 15 051 HDxf-utdata
    i AutoCAD 2025:s kärna, ODA och ezdxf), dokumentation
  - Brytande API-ändringar i 1.9.0: `ObjectData` är en struct som ärver `std::variant` (`data.get<T>()` i stället för
    `std::get`/`std::visit`), `Block::color` → `Block::block_color` + `endblk_color`,
    `AssocPersSubentManagerData::trailing` → `third_list`, nya `EntityType`/`ObjectType`-värden
  - Det AutoCAD-facit saknar är **ej testat**, per familj och gren med skäl i planens Utfall-avsnitt; R13/R14 följer
    1.8.x-grunden, SECTION-typerna där är "inte fastställt"
  - Kända avvikelser mot AutoCAD, inte rättade: vriden TEXT (extents efter glyferna mot textlådans hörn), MTEXT i Arial
    (annan marginal över/under raden), DIMENSION 42 ur geometrin när posten skrivs ny (vinkel-constraint), text skriven
    ny i 2000/2004 som `\U+XXXX` där AutoCAD:s ZWM-fil har kodsidans byte, R12-poster ur typade värden med grupper vid
    noll/standardvärde som AutoCAD utelämnar, VPORT ur typade värden med omgivningsljusets färg (63/421) där en fil
    AutoCAD sparat ur en annan producents ritning saknar den

- [ ] **Fas 8 — Konvertering mellan versioner** — senare (Erik 2026-10-24: hoppas över i svepet;
  läsa går i alla versioner, och en ny ritning skrivs i vald version)
  - `save_as()` till annan version än den lästa: nedgradering (t.ex. `LWPOLYLINE` →
    `POLYLINE`, `MTEXT` → `TEXT`, true color → närmaste ACI vid R12) och uppgradering
    (handles, ägare och OBJECTS genereras)
