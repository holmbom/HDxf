[English](README.md) · **Svenska**

# HDxf

C++-bibliotek för att läsa, skriva och bearbeta DXF-filer.
Version 1.9.1 — läser DXF R12 … 2018, skriver R12, R13, R14, 2000, 2004, 2007, 2010, 2013 och 2018 (R12 verifierat
i AutoCAD, 2000 och 2004 i DWG TrueView och ODA, 2007 … 2018 i ODA och AutoCAD:s kärna).
Alla strängar är UTF-8 (sedan 1.1.0, se
[Teckenkodning](#teckenkodning)). Filsökvägar är UTF-8-säkra i Windows.
1.9.1: öppen källkod under MIT — licensspärren är borta (brytande: `LicenseData`, `SetLicense()` och `HasValidLicense()` finns inte
längre, och fil-I/O kräver ingen licens); färdigbyggda bibliotek för Windows och Linux på varje release; en R12-fil som
lästs utan handtag skrivs tillbaka utan dem ($HANDLING 0, ingen $HANDSEED), annars kastar AutoCAD ritningen.
1.9.0: fas 9 — posterna som 1.8.1 behöll som råa gruppkoder är typade (se [Typade poster](#typade-poster-fas-9)); brytande ändringar: `ObjectData` är en struct som ärver från `std::variant`, `Block::color` är `block_color` + `endblk_color`, `AssocPersSubentManagerData::trailing` heter `third_list`, nya `EntityType`-/`ObjectType`-värden.
1.8.1: R13/R14 som AutoCAD 2025 läser dem: HATCH:s 47 med bara en härledd gränsbana, en formfils STYLE utan
namn; varje ny R13/R14-testritning och tvingad omskrivning läses av AutoCAD 2025:s kärna.
1.8.0: skrivning av DXF R13 och R14 (AC1012/AC1014, ASCII och binärt): nya ritningar i den form AutoCAD R13/R14 skrev,
R13/R14-filer läses tillbaka byte för byte; R14 har LWPOLYLINE, HATCH och IMAGE (se `doc/reference/hdxf_r13_r14_format.md`).
1.7.2: ACIS-kroppsbyggaren `hdxf_acis` (se [ACIS-kroppsbyggaren](#acis-kroppsbyggaren-hdxf_acis)); kärnan är oförändrad.
1.7.1: rundturen mot AutoCAD 2025:s facit (560/560 filer byte för byte), R12-mallläge och CRLF-filer;
fynden i Codex-granskningen av 1.7.0 är rättade (kodsidsbyte, $HANDSEED, MLINE-stil, remove_layer).

## Funktioner

- **Läsa och skriva** DXF R12-, R13-, R14-, 2000-, 2004-, 2007-, 2010-, 2013- och 2018-filer (ASCII och binärt). R13
  och R14 skrivs som AutoCAD R13/R14 skrev dem; en fil som lästs från dem skrivs tillbaka byte för byte. En ritning som
  lästs från en version skrivs bara som just den versionen (konvertering mellan versioner är en senare fas)
- **Typade poster** — tabeller, OLE- och proxyobjekt, underlägg, material och visuella stilar, fält, dynamiska block,
  associativitet, rendering och ljus, sektioner, annoteringskontexter samt poster från AutoCAD Map, TurboCAD och ZWM är
  typade i det publika API:t (se [Typade poster](#typade-poster-fas-9))
- **Skriva tillbaka det som lästs** — en ritning som lästs från DXF 2000, 2004, 2007, 2010, 2013 eller 2018 skrivs ur
  sina bevarade poster, och bara det som ändrats skrivs på nytt (se
  [Skriva DXF 2000](#skriva-dxf-2000))
- **Ett enda header-bibliotek** — inkludera bara `hdxf.hpp`
- **Inga beroenden** — bara C++:s standardbibliotek (C++20)
- **Rundturssäkert** — okända entiteter och objekt, och en post som en kodek inte kan läsa, bevaras som råa gruppkoder
- **UTF-8 överallt** — varje `$DWGCODEPAGE` som AutoCAD använder (ANSI, DOS, ISO 8859, Macintosh
  och de tvåbytes ANSI_932/936/949/950/1361) samt UTF-8 från 2007 avkodas, liksom `\U+XXXX`
  och `\M+nXXXX`

## DXF R12-entiteter som stöds

| Entitet | Beskrivning |
|--------|-------------|
| LINE | Linje mellan två punkter (2D/3D) |
| CIRCLE | Cirkel med medelpunkt och radie |
| ARC | Cirkelbåge med start- och slutvinkel |
| POINT | Enskild punkt |
| TEXT | Enradig text |
| POLYLINE | Linje/polygon med flera segment, med stöd för bulge |
| TRACE | Fylld fyrhörning (äldre) |
| SOLID | Fylld triangel/fyrhörning |
| 3DFACE | 3D-yta med flaggor för osynliga kanter |
| SHAPE | Formreferens |
| INSERT | Blockinfogning med skala, rotation, matriser |
| ATTDEF | Attributdefinition (i block) |
| ATTRIB | Attributvärde (på infogningar) |
| DIMENSION | Alla 7 undertyper: roterad, justerad, vinkel, diameter, radie, vinkel med 3 punkter, ordinat |
| VIEWPORT | Vyfönster i pappersrymden |

## DXF 2000-entiteter

Skrivs bara som DXF 2000 — att spara en ritning som har en av dem som R12 misslyckas med
`"<TYPE> requires DXF 2000 or later"`.

| Entitet | Beskrivning | Lägg till |
|--------|-------------|-----|
| LWPOLYLINE | Lätt 2D-polylinje: bredder, bulges, höjd | `add_lwpolyline()` |
| ELLIPSE | Ellips eller elliptisk båge | `add_ellipse()` |
| XLINE, RAY | Obegränsade linjer (ingår inte i utbredningen) | `add_xline()`, `add_ray()` |
| MTEXT | Flerradig text, formateringskoder som en enda sträng | `add_mtext()` |
| TOLERANCE | Toleransram | `add_tolerance()` |
| LEADER | Hänvisningslinje med pilspets, hakline och annotering | `add_leader()` |
| SPLINE | NURBS-kurva: knutar, kontrollpunkter, vikter, passpunkter | `add_spline()` |
| HATCH | Solid eller mönsterfyllning; polylinje- eller kantgränser; associativ | `add_hatch()` |
| MLINE | Multilinje i en MLINESTYLE (geometrin beräknas som AutoCAD gör) | `add_mline()` |
| IMAGE | Rasterbild med sin IMAGEDEF, reaktor och RASTERVARIABLES | `add_image()` |
| 3DSOLID, REGION, BODY | ACIS-data bevarad som den skrevs; dess B-rep läses för exakt utbredning | `add_acis()` |
| MULTILEADER | AutoCAD 2008:s multihänvisning (MTEXT- eller blockinnehåll, hänvisningslinjer, landningar) med sin MLEADERSTYLE; skrivs i 2000, 2004 och 2007 som AutoCAD skriver den, med sin proxygrafik | `add_mleader()`, `add_mleader_block()`, `add_mleader_style()` |

De entitetstyper som DXF 2000 har utöver dessa (OLE2FRAME, ACAD_TABLE …) var `EntityType::Unknown` till och med 1.8.1;
sedan 1.9.0 är de typade (se [Typade poster](#typade-poster-fas-9)).

## DXF 2007-entiteter

| Entitet | Beskrivning | Lägg till |
|--------|-------------|-----|
| SURFACE, EXTRUDEDSURFACE, LOFTEDSURFACE, REVOLVEDSURFACE, SWEPTSURFACE, PLANESURFACE | AutoCAD 2007:s ytor: ACIS-kroppen (exakt utbredning), isolinjerna och det ytan skapades av (`SurfaceData`) | `add_surface()` |
| HELIX | Helix: dess spline och dess definition (`HelixData`); ritas som AutoCAD ritar den (proxygrafik, utbredning) | `add_entity()` |

En ny yta kräver DXF 2007 (`"EXTRUDEDSURFACE requires DXF 2007 or later"`; AutoCAD skriver en
yta i 2004 med sina isolinjer som proxygrafik, vilket HDxf inte gör). En yta som lästs från
en 2004-fil skrivs tillbaka med dem.

## DXF 2010-entiteter och -objekt

| Typ | Beskrivning | Lägg till |
|------|-------------|-----|
| MESH | AutoCAD 2010:s underindelade nät (`MeshData`): dess bur av hörn, ytor, kanter och veck, dess utjämningsnivå och de egna egenskaper som ytor, kanter eller hörn har. AutoCAD skriver den också i 2000 … 2010-filer, och det gör HDxf också | `add_mesh()` |
| GEODATA (objekt) | Var ritningen ligger på jorden (`GeoDataData`), i de tre former AutoCAD skriver: version 1 (2007-filer), 2 (2010), 3 (2013+) | `set_geodata()` |

Ett utjämnat MESH mäts och ritas som AutoCAD jämnar ut det — Catmull-Clark med AutoCAD:s
regler för veck, bråkdelsveck och randen, framtagna med AutoCAD:s egen motor (plan
`doc/plans/2026-10-24-fas-4-dxf-2010.md`, B49): dess utbredning är den som dess utjämnade kanter har, precis som
AutoCAD:s.

**Brytande ändring i 1.2.0:** dessa typer var `EntityType::Unknown` i 1.1.0. OBJECTS finns i
`objects()` i stället för `raw_sections()`, och ett blocks BLOCK_RECORD i `Block::record`
i stället för `raw_tables()`. Kod som letade efter dem där måste ändras.

## DXF 2013

DXF 2013 lägger inte till någon entitetstyp; det den lägger till är typat (plan `doc/plans/2026-10-24-fas-5-dxf-2013.md`):

| Vad | Var |
|------|-------|
| `$REQUIREDVERSIONS` (160 0) | headern, efter `$LASTSAVEDBY` |
| Ett lagers 348 (0 på varje lager i AutoCAD:s 2013-filer; finns i ingen referens) | `Layer::pointer_348` |
| Ett linjetypselements "upright" (74 bit 8, `U=` i en .lin-fil) | `LinetypeElement::type` |
| MULTILEADER 295 och MLEADERSTYLE 298 | `MLeaderData::text_extended`, `MLeaderStyleData::text_extended` |
| En kropps ändrings-GUID (AcDbModelerGeometry 290/2) | `AcisData::guid` |
| ACIS-data för 3DSOLID, REGION, BODY och ytorna som SAB i sektionen ACDSDATA | `AcisData::sab` |
| Förhandsbilderna för layouter och vyer (PNG i ACDSDATA) | `LayoutData::thumbnail`, `View::thumbnail` |

ACIS i 2013 är binärt (SAB 21800): en kropp som lästs som SAB skrivs tillbaka med sina byte; en kropp
med SAT-rader — `add_acis()`, eller ACIS kopierat från en äldre fil, version 1.0 … 21800 — skrivs som SAB
som AutoCAD skriver det, post för post AutoCAD:s egen SAB av samma kropp, och
AutoCAD:s kärna läser den med samma volym och area. 2013:s egenskaper skrivs i 2000 …
2010 som AutoCAD skriver dem där: upright och `text_extended` som rundturs-XRECORD i
postens extension dictionary, ändrings-GUID som XDATA `ACAD_OBJECT_CHANGE_GUID` — och läses tillbaka
därifrån.

## Tabeller som stöds

VPORT, LTYPE, LAYER, STYLE, VIEW, UCS, APPID, DIMSTYLE — och i 2000 BLOCK_RECORD (i
`Block::record`), linjetjocklekar, plottstilar, sammansatta linjetyper och 2000:s
måttvariabler

## Ytterligare funktioner

- Fullt stöd för ACI-färger (0-256), inklusive BYBLOCK och BYLAYER
- Lageregenskaper: på/av, fryst, låst, färg, linjetyp
- Egna linjetyper med streckmönster
- Textstilar med typsnitt, breddfaktor, lutningsvinkel och den typsnittsfamilj som AutoCAD hittar ett
  TrueType-typsnitt med (`TextStyle::font_family`)
- Typsnitt lästa som AutoCAD läser dem — SHX (former, unifont, bigfont), TrueType, OpenType/CFF —
  från `set_font_paths()` (standard `%WINDIR%\Fonts`); text, MTEXT och TOLERANCE mäts
  och läggs ut som AutoCAD gör (`Drawing::entity_extents()`, `Drawing::mtext_size()`)
- Måttstilar med hela R12:s måttvariabelmängd, 2000:s tillägg och de nyare
  variablerna (DIMTFILL, DIMLTYPE, DIMFXL, DIMJOGANG …)
- Måttblock (`*D`) byggda som AutoCAD bygger dem, för varje måtttyp och
  måttvariabel
- Blockdefinitioner med nästlade entiteter
- INSERT med stöd för ATTRIB (sekvensen attribut-följer)
- XDATA (utökad entitetsdata) per applikation
- Entitetsegenskaper: tjocklek, extruderingsriktning, linjetypsskala, pappersrymd
- ECS (entitetskoordinatsystem) med Arbitrary Axis Algorithm
- Headervariabler (R12 och AutoCAD:s 2000-standardvärden, användarkonfigurerbara)
- Utbredning beräknad som AutoCAD håller den (`$EXTMIN`/`$EXTMAX`, `$PEXTMIN`/`$PEXTMAX`):
  exakt för splines, text i sina typsnitt och ACIS-solider efter sin B-rep

## Snabbstart

```cpp
#include "hdxf.hpp"

int main() {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);

    dwg.add_layer("MyLayer", hdxf::Color::red());
    dwg.add_line({0, 0}, {100, 50}, "MyLayer");
    dwg.add_circle({50, 25}, 20.0, "MyLayer");
    dwg.add_text({10, 60}, "Hello DXF", 5.0, "MyLayer");

    dwg.save_as("output.dxf", hdxf::DxfFormat::Ascii);
}
```

## Skriva DXF 2000

```cpp
hdxf::Drawing dwg(hdxf::DxfVersion::R2000);   // eller set_version(R2000) på valfri ritning
dwg.add_lwpolyline({{0, 0}, {100, 0}, {100, 50}}, true, "Walls");
dwg.save_as("plan.dxf");
```

`Drawing()` startar fortfarande som R12, så befintlig kod skriver det den skrev förut; DXF 2000 väljs med
`Drawing(DxfVersion::R2000)` eller `set_version()`. HDxf följer det AutoCAD skriver och Autodesks DXF-referens.

**Vad som kan sparas:**

| Ritningen | Sparad som R12 | Sparad som 2000 | Sparad som 2004 |
|---|---|---|---|
| Byggd i minnet, eller läst från R12 | Ja (inga 2000-typer, inga 2004-egenskaper) | Ja (inga 2004-egenskaper) | Ja, genererad |
| Läst från 2000 | Nej (konvertering kommer senare) | Ja, ur sina bevarade poster | Nej (konvertering) |
| Läst från 2004 | Nej | Nej | Ja, ur sina bevarade poster |
| Läst från R13, R14 eller 2007+ | Nej | Nej | Nej (konvertering kommer senare) |

**En ny 2000-ritning** får det AutoCAD skriver för en sådan: `*Model_Space` och `*Paper_Space`
med sina BLOCK_RECORD, layouterna Model och Layout1, de namngivna lexikonen
(ACAD_GROUP, ACAD_LAYOUT, ACAD_MLINESTYLE med Standard, ACAD_PLOTSETTINGS,
ACAD_PLOTSTYLENAME), AutoCAD:s 2000-headervariabler och nya `$FINGERPRINTGUID` /
`$VERSIONGUID`. Tal skrivs med högst 16 signifikanta siffror, text utanför ASCII
som `\U+XXXX`, som AutoCAD skriver dem.

**En ritning som lästs från 2000** skrivs tillbaka ur sina bevarade poster: varje post
jämförs med hur den var när den lästes, och bara det som ändrats skrivs på nytt — resten,
inklusive grupper som HDxf inte tolkar, behåller filens text. Sparad i samma format
utan ändringar kommer filen tillbaka post för post. En lista som ändrats (polylinjehörn,
linjetypselement, hatch-gränser, MTEXT-text …) skrivs i sin helhet; en grupp som
posten saknade hamnar där AutoCAD placerar den. Ett ändrat mått får ny geometri i sitt
`*D`-block.

**Objekt, blockposter och layouter:** `objects()` / `find_object()` håller OBJECTS (typade:
DICTIONARY, XRECORD, GROUP, LAYOUT, MLINESTYLE, IMAGEDEF …, och sedan 1.9.0 de flesta av de övriga; resten som
`UnknownObjectData`),
`Block::record` BLOCK_RECORD, `layouts()` / `add_layout()` layouterna och
`add_group()` grupperna. En posts bevarade form kan ersättas eller rensas (`RawRecord`
från en lista av grupper); en rensad genereras ur de tolkade fälten.

**Relationer behålls** när något tas bort eller läggs till: det en post äger försvinner med den
(ett extension dictionary, ATTRIB/VERTEX/SEQEND, en bilds IMAGEDEF_REACTOR); gruppmedlemmar,
reaktorer, lexikonposter, ritordningen och blockreferenser följer med;
en hänvisningslinjes annotering och en hatchs gränsobjekt länkas åt båda hållen (en hatch vars
gräns är borta är inte längre associativ). En pekare som HDxf inte tolkar till en
borttagen post blir `0`, som AutoCAD skriver en nollpekare, och räknas i
`warnings()`. Klasser som ritningen behöver läggs till i CLASSES.

## Skriva DXF 2004

```cpp
hdxf::Drawing dwg(hdxf::DxfVersion::R2004);   // eller set_version(R2004)
hdxf::Entity& wall = dwg.add_line({0, 0, 0}, {100, 0, 0});
wall.props.color = hdxf::Color::from_rgb(200, 30, 30);          // äkta färg (420)
wall.props.transparency = hdxf::Transparency::from_alpha(127);  // 440
dwg.save_as("plan.dxf");
```

DXF 2004 skrivs som 2000 skrivs (samma struktur, samma hantering av en ritning som lästs från en
fil — allt ovan gäller), med det AutoCAD skriver i 2004 ovanpå (plan
`doc/plans/2026-10-23-fas-2-dxf-2004.md`):

- **Header:** AutoCAD:s 211 variabler, `$ACADVER AC1018`, de tolv 2004-tilläggen
  (`$SORTENTS`, `$DIMASSOC`, `$PROJECTNAME` …). `$LASTSAVEDBY` är tom i en ny ritning.
- **Äkta färg och färgböcker:** `Color` har `aci` (62), `rgb` (420) och `name` (430,
  `"BOOK$COLOR"`). `Color::from_rgb()` sätter ACI till den närmaste i AutoCAD:s palett
  (`hdxf::nearest_aci()`, som AutoCAD väljer den); `Color::from_book()` lägger till namnet. Ändra
  en färg med dessa (eller `from_aci()`), inte genom att sätta enbart `aci`. En entitets färgbokfärg
  får sitt DBCOLOR-objekt i ACAD_COLOR när ritningen sparas (som AutoCAD; ett
  lagers får inget); ett namn med två RGB-värden är ett fel.
- **Transparens:** `EntityProperties::transparency` (440) och `Layer::transparency` (XDATA
  `AcCmTransparency`, som AutoCAD lagrar den — också i 2000). Resten av ett lagers XDATA finns i
  `Layer::xdata`.
- **MTEXT-bakgrund** (`MTextData::background`), **gradientfyllningar**
  (`HatchData::gradient`, `add_hatch(boundary, gradient)`), skuggplottinställningarna för
  vyfönster och layouter.
- **Förhandsbild:** `thumbnail()` / `set_thumbnail()` — THUMBNAILIMAGE-byten (en DIB),
  skrivna i 2000 och 2004.
- **Klasser** har sitt instansantal (91); LAYOUT och ACDBPLACEHOLDER är inga klasser i
  2004. En ritning som lästs från 2004 behåller AutoCAD:s CLASSES som de var.

Att spara en ritning med något som bara 2004 har som R12 eller 2000 misslyckas, innan något
ändrats — `"True color requires DXF 2004 or later"`, och likadant för transparens, MTEXT-
bakgrunder, gradienter och skuggplottinställningar. Konvertering (äkta färg till närmaste ACI
…) kommer i en senare fas.

**Ändringar i 1.3.1:** `DimStyle` och `TextStyle` har nya fält (kompilera om).
Heltal högerjusteras som AutoCAD skriver dem, DIMSTYLE i 2000/2004 har bara det som
skiljer sig från AutoCAD:s värden, en ny 2000/2004-ritning har Layout1 och Layout2, och
`$LASTSAVEDBY` är användarens namn. Måttblock, textutbredning och utbredningen av
splines och solider beräknas exakt, så `$EXTMIN`/`$EXTMAX` och byggda `*D`-block
ändras från 1.3.0.

**Brytande ändringar i 1.3.0:** DBCOLOR är `ObjectType::DbColor` (var `Unknown`);
THUMBNAILIMAGE finns i `thumbnail()` i stället för `raw_sections()`; ett lagers
`AcCmTransparency`-XDATA är `Layer::transparency`. `Color`, `EntityProperties` och `Layer`
är större — kompilera om. I 2000 har Model-layouten nu sitt sista vyfönster (331, den
`*Active` VPORT) och `60` kommer före `370`, som AutoCAD skriver dem.

## Skriva DXF 2007

```cpp
hdxf::Drawing dwg(hdxf::DxfVersion::R2007);   // eller set_version(R2007)
dwg.add_text({0, 0, 0}, "Grön äng ∅ 25", 2.5);            // UTF-8 i filen
dwg.add_mleader({{0, 0, 0}, {30, 20, 0}}, "Pump P1");     // en multihänvisning i Standard-stilen
dwg.save_as("plan.dxf");
```

DXF 2007 skrivs som 2004 skrivs (samma struktur, samma hantering av en ritning som lästs från en
fil), med det AutoCAD skriver i 2007 ovanpå (plan `doc/plans/2026-10-24-fas-3-dxf-2007.md`):

- **Text i UTF-8:** filen är UTF-8 vad `$DWGCODEPAGE` än säger, som AutoCAD skriver den;
  ett tecken över U+FFFF skrivs som sina UTF-16-surrogat i två `\U+`-escapes (den enda
  form AutoCAD läser det i, se "Teckenkodning"). Ett måtts gradtecken är `\U+00B0` i sitt
  blocks MTEXT, som AutoCAD skriver det.
- **Header:** AutoCAD:s 251 variabler, `$ACADVER AC1021`, 2007-tilläggen (`$CMATERIAL`,
  `$INTERFEREOBJVS`, `$DGNFRAME`, `$REALWORLDSCALE` …).
- **En ny ritning** har AutoCAD 2017:s tre material (ByLayer, ByBlock, Global) och 26 visuella
  stilar i ACAD_MATERIAL och ACAD_VISUALSTYLE (som AutoCAD:s nya 2007-ritning), med headern och
  vyfönstren pekande på dem.
- **Nya fält:** en entitets och ett lagers material (`material_handle`, 347), MTEXT:s definierade
  höjd (46), ett attributs låsta position (280; till och med 2004 XDATA `AcDbAttr`),
  vyernas och vyfönstrens visuella stil, bakgrund, sol och belysning, en 3DSOLID:s
  historik (350), ett blocks enheter, sprängbarhet och skalning.
- **MULTILEADER:** `add_mleader(leader, text, style)` placerar texten efter landningen som
  AutoCAD:s MLEADER-kommando gör (fästpunkterna, landningsgapet, vänster- eller högerjusterad
  efter hänvisningslinjens sida); `add_mleader_block(leader, block)` med ett block. Skrivs med sin proxygrafik
  — den ritning AutoCAD skriver för program som inte känner till multihänvisningar (texten,
  pilspetsarna, hänvisningslinjerna med sina avbrott, blocket med varje entitetstyp det innehåller,
  ramen) — om inte `$PROXYGRAPHICS` är 0. En multihänvisning som lästs från en fil behåller AutoCAD:s grafik
  tills den eller det den ritas med (dess block, dess stilar) ändras. Dess utbredning är det den ritar.
  Ett block, en stil eller en linjetyp som den namngav och som tagits bort sedan den lästes blir en nollpekare
  (0), som AutoCAD skriver en.
- **Ytor och HELIX** (se tabellen ovan).

Att spara en ritning med något som bara 2007 har som en äldre version misslyckas, innan något
ändrats — `"A material requires DXF 2007 or later"`, och likadant för visuella stilar,
bakgrunder, MTEXT:s definierade höjd och nya ytor. En ritning som lästs från 2000, 2004 eller 2007 skrivs
bara som den versionen.

**Brytande ändring i 1.4.0:** MULTILEADER, MLEADERSTYLE, ytorna och HELIX var
`EntityType::Unknown` / `ObjectType::Unknown` i 1.3.x; nu är de typade
(`EntityType::MLeader`, `ObjectType::MLeaderStyle` …). Kod som läste deras råa grupper där
måste ändras. `Entity` och `Object` är större — kompilera om.

## Skriva DXF 2010

```cpp
hdxf::Drawing dwg(hdxf::DxfVersion::R2010);   // eller set_version(R2010)
dwg.add_mesh({{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}, {2, 2, 3}},
             {{0, 3, 2, 1}, {0, 1, 4}, {1, 2, 4}, {2, 3, 4}, {3, 0, 4}}, 2);   // utjämnat två gånger
hdxf::GeoDataData geo;
geo.coordinate_system = "SWEREF99-TM";
dwg.set_geodata(geo);
dwg.save_as("plan.dxf");
```

DXF 2010 skrivs som 2007 skrivs, med det AutoCAD skriver i 2010 ovanpå (plan
`doc/plans/2026-10-24-fas-4-dxf-2010.md`):

- **Header:** AutoCAD:s 252 variabler, `$ACADVER AC1024`, `$DIMTXTDIRECTION` och
  `$XCLIPFRAME` som 280 (0, 1 eller 2; i 2004/2007 är den 290, och en 2:a bevaras i DICTIONARYVAR
  `XCLIPFRAME` som AutoCAD gör).
- **Nya fält:** DIMENSION:s, ATTRIB:s och ATTDEF:s version (280), IMAGE:s klippläge
  (`clip_inside`, 290), DIMSTYLE:s `dimtxtdirection` (295), en hatch-splinekants passpunkter
  och tangenter (97 …, krävs av AutoCAD i 2010).
- **Proxygrafikens** storlekar är 160 (64 bitar) i stället för 92.
- **MULTILEADER:** 270, 271–273 och MLEADERSTYLE:s 179 i stället för XDATA `ACAD_MLEADERVER`;
  en hänvisningslinjes egna egenskaper (`MLeaderLine::color`, `linetype` …); text fäst vertikalt
  (en stil med `text_attachment_direction` 1: texten centrerad över hänvisningslinjen, som AutoCAD
  placerar den).
- **En ny ritning** har AutoCAD 2017:s visuella stilar i 2010:s form; en ritning som gjorts som 2007 och
  satts till 2010 (eller tillbaka) får dem i formen för den version den sparas som.
- **IMAGE** skrivs med sin kontur som proxygrafik, som AutoCAD skriver den i 2000 … 2010.

Att spara en ritning med något som bara 2010 har som en äldre version misslyckas, innan något
ändrats — `"An image's clip mode inside requires DXF 2010 or later"`, och likadant för
DIMTXTDIRECTION, en multihänvisnings vertikala fäste eller en hänvisningslinjes egna egenskaper, en hatch-
splinekants passpunkter och en GEODATA av version 2 eller 3. En ritning som lästs från 2000 … 2010 skrivs
bara som den versionen.

**Brytande ändringar i 1.5.0:** MESH och GEODATA var `EntityType::Unknown` /
`ObjectType::Unknown` i 1.4.x; nu är de typade (`EntityType::Mesh`, `ObjectType::GeoData`).
`MLeaderLine::newer_groups` är borta — en hänvisningslinjes 2010-egenskaper är typade fält. Ett
tecken över U+FFFF skrivs inte längre som `?` med en varning: det skrivs som sina UTF-16-
surrogat i två `\U+`-escapes i varje version, den enda form AutoCAD läser det i (2007:s
UTF-8-fil inräknad). IMAGE får sin proxygrafik. `Entity`, `Object`, `DimStyle`,
`ImageData` och `BlockRecord` är större — kompilera om.

## Skriva DXF 2013

```cpp
hdxf::Drawing dwg(hdxf::DxfVersion::R2013);   // eller set_version(R2013)
hdxf::AcisData body;
body.lines = sat_lines;                        // en kropps SAT, valfri version upp till 21800
dwg.add_acis(hdxf::EntityType::Solid3D, body); // skrivs som SAB i ACDSDATA
dwg.save_as("solid.dxf");
```

DXF 2013 skrivs som 2010 skrivs, med det AutoCAD skriver i 2013 ovanpå: headerns 253
variabler (`$ACADVER AC1027`, `$ACADMAINTVER 125`, `$REQUIREDVERSIONS`), LAYER 348, LTYPE:s
upright i 74, MULTILEADER 295 och MLEADERSTYLE 298, AcDbModelerGeometry med bara 290 och 2 (ändrings-
GUID), ACIS-data och förhandsbilderna i ACDSDATA (sist, efter OBJECTS), VISUALSTYLE i
2013:s form, och en ny ritning som AutoCAD 2017:s (dess Standard-multihänvisningsstil med 295 1 och
298 1, VPORT-tabellens XDATA `DbSaveVer`, ACDSDATA med miniatyrbilden och systemscheman). En
GEODATA är version 3 i 2013 (`set_geodata()` med `version = 3`; 1 eller 2 misslyckas — AutoCAD gör
om dem till 3, vilket är konvertering). En ritning som lästs från 2013 skrivs bara som 2013, ur sina
bevarade poster; ACadSharps 2013-exempel går runt byte för byte.

Att spara som en äldre version misslyckas, innan något ändrats, för det som bara 2013 har: en kropp med
enbart SAB-data (`"A 3DSOLID with only SAB data requires DXF 2013 or later"`; SAB → SAT är
konvertering), ett lagers 348, en GEODATA av version 3. Förhandsbilder skrivs inte före 2013
(AutoCAD:s 2000 … 2010-filer har inga): en rad i `warnings()`.

**Brytande ändringar i 1.6.0:**
- ACDSDATA är typad: den finns inte längre i `raw_sections()` (kropparnas SAB i `AcisData::sab`,
  förhandsbilderna i `LayoutData::thumbnail` / `View::thumbnail`).
- Nya fält: `AcisData::guid` och `sab`; `EntityProperties::xdictionary`, `reactors` (som lästa —
  en entitet som kopierats till en annan ritning har dem rensade, liksom sitt extension dictionary) och
  `plot_style_type` (380); `Vertex::reactors`; `Linetype::xdictionary` och `xdata`;
  `Layer::xdictionary` och `pointer_348`; `TextStyle`, `View` och `Vport` `xdictionary`,
  `Vport::xdata`; `DimStyle::reactors` och `dimarcsym` (90); `Block::description` och dess entitets-
  egenskaper (`color`, `linetype`, `lineweight`, `linetype_scale`, `visible`, `xdata`);
  `BlockRecord::preview`; `ViewportData::visual_style_in_entity`; `MLeaderStyleData::text_extended`.
  `Entity` och dessa structer är större — kompilera om.
- Ett lagers `AcCmTransparency 1071 0` (ingen transparens, som AutoCAD skriver det på vissa lager)
  stannar i `Layer::xdata`; en transparens som sätts ersätter det.
- Ett linjetypselements stil från en formfil, som saknar namn, är dess fil
  (`LinetypeElement::style = "ltypeshp.shx"`), inte Standard.
- Det som skrivs som AutoCAD skriver det, också i 2000 … 2010: MLEADERSTYLE 295 är 0 före 2013;
  ett tal under 1e-15 har 16 decimaler (`-0.0000000000000004`, en MLINE:s riktningar behåller sin
  exponent); ARC:s 210 finns i AcDbCircle; en rationell splines vikt följer varje kontrollpunkt;
  MTEXT:s delar är 250 tecken från 2004 (`^J` ett; UTF-16-enheter), 250 som skrivet i 2000;
  ett måtts 42 mäts i dess plan, en vinkel genom bågens punkt, och skalas med
  DIMLFAC; en VPORT, VIEW eller Model-layouten som lästs utan visuell stil eller sista vyfönster behåller
  ingen.

## Skriva DXF 2018

```cpp
hdxf::Drawing dwg(hdxf::DxfVersion::R2018);   // eller set_version(R2018)
dwg.add_mtext({0, 0, 0}, "Text in columns", 2.5);
dwg.save_as("drawing.dxf");                    // AC1032, ASCII eller binärt
```

DXF 2018 (AC1032) skrivs som 2013 skrivs, med det AutoCAD skriver i 2018 ovanpå (plan
`doc/plans/2026-10-25-fas-6-dxf-2018.md`):

- Headern: `$ACADVER AC1032` och `$ACADMAINTVER` som grupp 90 (70 före 2018, vilken kod programmet än
  gav); `$REQUIREDVERSIONS` är ett 64-bitars heltal i binära filer. Ingen `DbSaveVer` i VPORT-tabellen.
- **MTEXT-kolumner och ram** (`MTextData::columns`, `MTextColumns`, `MTextEmbedded`): i 2018 är
  kolumnerna det inbäddade objektet efter `101 Embedded Object`; i 2000 … 2013 är de XDATA
  `ACAD_MTEXT_COLUMN_INFO` och, för flera kolumner, en MTEXT-entitet per kolumn länkad med
  `ACAD_MTEXT_COLUMNS` (HDxf behåller en text i modellen); textramen är en flagga i 2018 och XDATA
  plus en LWPOLYLINE per kolumn tidigare. En definierad höjd är XDATA i 2000 … 2013.
- **Flerradig ATTRIB/ATTDEF** (`AttribData::multiline`, `MultilineAttribute`): den inbäddade MTEXT:en i
  2018; före 2018 XRECORD:en `ACAD_MLATT` och `ACAD_MATT_HEIGHT` i attributets extension
  dictionary, attributets egen TEXT som spegel och de övriga raderna som ATTRIB. Misslyckas som
  äldre, innan något ändrats, för det som bara 2018 har (`"An attribute's type (71) requires DXF 2018 or later"`).
- **Detalj- och sektionsvystilar** (`ObjectType::DetailViewStyle`, `SectionViewStyle`,
  `ModelDocViewStyleData`): version, namn, flagga, visningsnamn (300) och visningsflaggor (90); en XRECORD
  `DISPLAYNAME`/`FLAGS` före 2018. DBCOLOR utan `DBColXD`; LIGHT, SUN och VISUALSTYLE utan
  SavedBy-XDATA.
- **Koordinationsmodell och RapidRT** (`add_coordination_model()`, `CoordinationModelData`,
  `NavisworksModelDefData`, `add_rapidrt_render_settings()`, `set_rapidrt_render_environment()`): som
  Autodesks 2018-referens har dem (2013 och senare). Modellens gränser räknas i utbredningen.
- **ACIS i 2018 är SAB 22300** (`ASM BinaryFile4`, huvud `(22300, 0, 2, 4)`): en kropp med SAT-rader
  kodas ur dem; en kropp som lästs som SAB (21800 eller 22300, till och med 21800 i en AC1032-fil) skrivs
  med sina byte.

En ritning som lästs från 2018 skrivs bara som 2018, ur sina bevarade poster; ACadSharps två 2018-
exempel går runt byte för byte. Att skriva en ritning som lästs från en annan version som 2018 (och åt
andra hållet) misslyckas med `"Writing DXF X as Y is not supported yet (the drawing was read from that
version)"` — konvertering är fas 8.

## Typade poster (fas 9)

Till och med 1.8.1 behöll HDxf det mesta av det AutoCAD skriver utöver de grundläggande entiteterna, tabellerna och objekten som råa gruppkoder
(`EntityType::Unknown`, `UnknownObjectData`, `RawRecord`). Sedan 1.9.0 har dessa poster typad data i det publika API:t
(plan `doc/plans/2026-10-27-fas-9-typer.md`, stegen 9.0–9.18 och 9.S):

| Familj | Vad som är typat |
|---|---|
| Tabeller | ACAD_TABLE, TABLESTYLE, TABLECONTENT, CELLSTYLEMAP, `*T`-blocken; tabellen ritad som AutoCAD ritar den |
| Block | BLOCK- och ENDBLK-färger (`Block::block_color`, `Block::endblk_color`) |
| OLE och proxy | OLEFRAME, OLE2FRAME; ACAD_PROXY_ENTITY och ACAD_PROXY_OBJECT med klassens egen data läst och skriven genom klassens kodek (`ProxyEntityData::class_data`, `ProxyObjectData::class_data`) |
| Ritordning, klippning, index | SORTENTSTABLE, SPATIAL_FILTER, LAYER_INDEX, SPATIAL_INDEX, IDBUFFER och liknande |
| Underlägg, koordination | PDF-/DWF-/DGN-underlägg och deras definitioner, koordinationsmodell |
| Material och stilar | MATERIAL och varje form av VISUALSTYLE |
| Fält och dokumentdata | FIELD, FIELDLIST och de övriga dokumentobjekten |
| Dynamiska block | utvärderingsgrafen, blockobjekten (parametrar, åtgärder, grepp, begränsningar) |
| Associativitet | ACDBASSOC* (baser, bestående delobjekt, begränsningar, parametrar, åtgärder, matriser, hanterare) och ACSH-historiken för solider och ytor |
| Rendering och ljus | renderinställningar, LIGHT, SUN, LIGHTLIST, SUNSTUDY |
| Sektioner och vystilar | SECTION-familjen, DETAIL- och SECTION-vystilar |
| Ytor | NURBSURFACE, och isolinjerna för 2000/2004-ytorna som proxygrafik |
| Annotering | annoteringskontexter, DIMASSOC, särskilda mått (ARC_DIMENSION, LARGE_RADIAL_DIMENSION) |
| Tillägg | poster från AutoCAD Map, TurboCAD och ZWM |

Varje typad posttyp är en kodek i typregistret (`src/typed_registry_data.inc`, rapporten
`doc/facit/fas9_register.md`). En kodek läser posten, skriver en ny, skriver en post vars bevarade form
rensats och slår ihop en ändrad post med den bevarade, så att det som inte ändrats behåller filens text (som för
varje post sedan 1.2.0). Ägare, reaktorer, lexikon och de övriga relationerna följer det som tas bort eller läggs till;
att ta bort en post tar också bort det som bara den äger, och en pekare till en borttagen post utelämnas (i en ritning byggd i
minnet liksom i en som lästs från en fil).

- **En post som en kodek inte kan läsa förblir otypad**: den bevaras som läst och `Drawing::warnings()` säger det. En grupp
  som kodeken inte känner till blir aldrig en typad post där data går förlorad.
- **Versionsmatris.** Varje typ har en matris med nio versioner (R12 … 2018) gånger två format (ASCII, binärt). En cell anger
  hur posten genereras (den egna formen, en äldre proxy- eller rundtursform som AutoCAD skriver den, eller ett namngivet
  fel som `"<TYPE> requires DXF 2013 or later"`) och, som ett separat faktum, om AutoCAD har setts läsa
  den. Sparandet misslyckas med det namngivna felet innan något ändrats.
- **Ej testat.** En gren för vilken ingen AutoCAD-fil finns, och som AutoCAD 2025:s kärna inte kan göra (dess kommandon
  är interaktiva eller ett tilläggs), är **ej testad**: den listas med sin orsak i styckena "Ej testat" i
  planens utfallsavsnitt och i `doc/facit/fas9_register.md`. Den kallas inte fullt typad eller AutoCAD-verifierad. R13 och
  R14 följer 1.8.x-basen och formatprofilen för de flesta typerna, eftersom AutoCAD 2025 inte kan spara dem; SECTION-
  typerna i R13/R14 är "ej fastställda".
- **Verifiering.** `tools/fas9_verify_batch.py` lät AutoCAD 2025:s kärna, ODA File Converter och ezdxf läsa 3 851
  original (3 798 sparade av AutoCAD, 53 av andra program, använda som robusthetsindata) och 15 051 filer som HDxf skrev av dem
  (oförändrad rundtur och omskrivning enbart ur de typade värdena, ASCII och binärt). Där originalet öppnas och HDxf:s
  utdata inte, är orsaken AutoCAD:s egen: dess binära filer med material- och visuella stilritningar öppnas inte
  heller ("Unknown object type"), AutoCAD läser inte den `-NAN` som det skriver i en R12-hatchs XDATA, och bildfallen
  kraschar kärnan på originalen också. En körning utan gränssnitt är ingen visuell kontroll.

**Kända skillnader mot AutoCAD** (funna, inte ändrade):

- En roterad TEXT: AutoCAD räknar utbredningen efter glyferna, HDxf efter textrutans hörn.
- MTEXT i Arial: AutoCAD lämnar en annan marginal ovanför och under raden.
- Ett måtts 42 beräknas alltid ur geometrin när posten skrivs på nytt; för en vinkelbegränsning
  ("ang1=33") ger HDxf 33,25°, som måttets text säger, där AutoCAD:s fil har 20,56°.
- Text som skrivs på nytt i 2000/2004 utanför ASCII är `\U+XXXX`, där AutoCAD:s fil från en ZWM-ritning har tecknet
  som en byte i kodsidan.
- En R12-post som skrivs ur typade värden skriver vissa grupper med noll eller standardvärdet (DIMENSION 70, TEXT 72/71) som
  AutoCAD utelämnar; AutoCAD läser dem.
- En VPORT som skrivs ur typade värden (2007+) har alltid omgivningsljusets färg (63/421), som AutoCAD:s nya filer; en
  fil som AutoCAD sparat från en annan producents ritning har ingen. En oförändrad eller ändrad VPORT skrivs ur sin
  bevarade post, så detta syns bara när posten skrivs ur sina värden.

**Brytande ändringar i 1.9.0:**

- `ObjectData` är en `struct` som ärver från `std::variant<…>` (`ObjectDataVariant`). Använd `data.get<T>()`,
  `data.get_if<T>()` och `data.index()`; `std::get` och `std::visit` direkt på `Object::data` kompilerar inte längre.
  Konstruktion från ett värde, `std::in_place_type`, `std::variant_size` och `std::variant_alternative` fungerar fortfarande.
- `Block::color` är ersatt av `Block::block_color` och `Block::endblk_color`.
- `AssocPersSubentManagerData::trailing` är ersatt av `third_list`.
- Många nya `EntityType`- och `ObjectType`-värden och datatyper: ett program med en uttömmande `switch` över dem måste lägga till
  fall. Poster som var `Unknown` är nu typade, så kod som läste deras råa grupper måste ändras.

## Filsökvägar

`load()`, `save_as()` och den ihågkomna sökvägen i `save()` tar `std::filesystem::path`.
Stränglitteraler och `std::string` konverteras implicit, så
befintliga anropsställen fortsätter fungera.

När anroparen håller en sökväg som UTF-8 i en `std::string`, konvertera den med
`hdxf::path_from_utf8()` i stället för att lita på den implicita konverteringen — den
konverteringen är bara UTF-8 på libstdc++ och ANSI på MSVC. `hdxf::utf8_from_path()`
konverterar tillbaka; båda går via `char8_t` och beter sig likadant på varje verktygskedja.
Sökvägar i `last_error()` är UTF-8. Varje I/O-anrop rensar `last_error()` vid ingång, så den är tom efter ett lyckat anrop.

```cpp
std::string utf8 = "C:/Projekt/Sträcka Öst/ritning.dxf";  // UTF-8-byte
dwg.load(hdxf::path_from_utf8(utf8));
```

## Teckenkodning

Varje sträng i API:t är UTF-8 — text, attributvärden, namn på lager, block och stilar,
XDATA-strängar. **Brytande ändring i 1.1.0:** tidigare var strängarna filens råa
byte (Windows-1252 för de flesta R12-filer). Kod som skickade eller förväntade kodsidesbyte
måste byta till UTF-8.

- Läsning: till och med 2004 avkodas byten med `$DWGCODEPAGE` (ANSI_1252 när den
  saknas): ANSI_874 och ANSI_1250 … ANSI_1258, DOS437 … DOS869 (även som CP437 …),
  ISO8859-1 … ISO8859-10, MACINTOSH, och tvåbytes ANSI_932 (Shift-JIS, även DOS932),
  ANSI_936 (GB, GB2312), ANSI_949 (KSC5601), ANSI_950 (BIG5) och ANSI_1361 (JOHAB) — råa
  byte, och AutoCAD:s `\M+nXXXX`-escape för ett tvåbytestecken. Från 2007 är filen
  UTF-8. `\U+XXXX` avkodas. Ett namn som HDxf inte känner till fungerar så länge filen är
  ASCII; byte ≥ 0x80 gör då att `load()` misslyckas med `"Unsupported code page: <name>"`.
  Ogiltig UTF-8 i en 2007+-fil misslyckas också.
- Skriva R12: strängar skrivs i ritningens `$DWGCODEPAGE` (standard ANSI_1252); ett
  tecken som kodsidan saknar skrivs som `\U+XXXX`, så en fil som hade `\U+` för ett
  tecken som kodsidan har skrivs tillbaka med själva tecknet. Ett tecken över
  U+FFFF skrivs som sina UTF-16-surrogat, `\U+D83D\U+DE00` för U+1F600 — i varje version,
  den enda form AutoCAD läser det i (AutoCAD 2027:s egen läsare vägrar det som UTF-8 och som CESU-8
  i en 2007/2010-fil; plan fas 4, B55). En sträng som inte är giltig UTF-8 gör att sparandet misslyckas,
  och ingen fil skrivs.
- Skriva 2000 och 2004: allt utanför ASCII är `\U+XXXX`, vad `$DWGCODEPAGE` än säger, som
  AutoCAD skriver det (tecken över U+FFFF som sina surrogat, som i R12). MTEXT-text delas i
  grupper om högst 250 tecken — i 2000 som skrivet (`^J` två), från 2004 som tecken (`^J`
  ett, ett tecken över U+FFFF två: UTF-16-enheter) — aldrig inuti ett tecken.
- Skriva 2007, 2010 och 2013: UTF-8, varje tecken som det är utom de över U+FFFF (deras
  surrogat, som ovan); MTEXT:s grupper om 250 tecken som från 2004. Ett `\U+00B0` som AutoCAD:s
  måttmotor lämnat kvar i en text läses som `°`, som AutoCAD:s egen läsning gör det.
- Styrtecken: ASCII-DXF skriver dem som `^` + bokstav (`^J` = radmatning) och `^`
  som `^ `, som DXF-referensen anger; läsning vänder på det. Binärt DXF behöver ingetdera.

## Bygga

Kräver CMake 3.20+ och en C++20-kompilator.

```bash
cmake -B build -G Ninja
cmake --build build
python -I tools/run_tests.py
```

Utan byggtyp blir bygget Release. `tools/run_tests.py` kör testerna (`build/test/hdxf_tests`) i parallella
delar som fördelas efter varje tests tid vid förra körningen; det tar filter som `hdxf_tests` gör och `-j N` för
antalet delar.

## ACIS-kroppsbyggaren (hdxf_acis)

`libs/hdxf_acis/` (target `hdxf_acis`, alias `hdxf::acis`, header `hdxf_acis.hpp`, alternativ `HDXF_BUILD_ACIS`, på som
standard) bygger ACIS-kroppen för en 3DSOLID, REGION eller BODY ur ett programs exakta randrepresentation (B-rep) och
returnerar den som `hdxf::AcisData`. HDxf har aldrig gjort ACIS-data förut; byggaren behöver bara HDxf:s publika header och
ändrar inte kärnan. Allt är millimeter och radianer.

```cpp
#include <hdxf_acis.hpp>

hdxf::acis::SphereParameters sphere;
sphere.frame.origin = {50, 10, 10};
sphere.radius = 12;

auto sat = hdxf::acis::make_sphere(sphere);
if (!sat) { for (auto& e : sat.errors()) std::cerr << e.text() << "\n"; return; }
auto acis = hdxf::acis::to_acis_data(sat.value());
if (!acis) return;

hdxf::Drawing drawing(hdxf::DxfVersion::R2010);      // eller R2013, R2018
drawing.add_acis(hdxf::EntityType::Solid3D, std::move(acis.value()));
```

* **Allmänna B-reps.** `BodyBuilder` tar kroppar, lumps, skal (yttre och hålrum), ytor, slingor (yttre, inre),
  coedges, kanter, hörn, kurvor, ytor och pcurves som typade handtag och kontrollerar dem innan något
  skrivs: slutna slingor, exakt två coedges på varje kant i ett slutet skal, partners, motsatta riktningar på
  delade kanter, plana slingor moturs (yttre) eller medurs (inre), kurvändar på hörnen, kurvor på
  sina ytor, pcurves på sina kanter, B-spline-grader, knutar och vikter. Ett avslag är en `Diagnostic` med en
  kod, handtaget, en sökväg (`body[0]/lump[0]/shell[0]/edge[12]`) och ett meddelande; för felaktig indata finns inga undantag.
* **Geometri, exakt.** Plan, kon och cylinder, sfär, torus, B-spline- och NURBS-ytor, linjer, ellipser och
  B-spline-kurvor, som SAT 21500 i AutoCAD 2025:s former. Ingenting trianguleras eller approximeras, och inga hörn
  slås ihop. En hel sfär eller torus är en yta utan slingor, en kons spets en degenererad kant, en söm en kant som används
  två gånger av en yta. `make_box`, `make_cylinder`, `make_cone`, `make_sphere` och `make_torus` går genom samma
  modell, placerade av en ram som AutoCAD placerar sina primitiver.
* **In i HDxf.** SAT 21500 går in i DXF 2010 som entitetens rader; DXF 2013 och 2018 får den SAB som HDxf:s SAT→SAB
  gör (21800 och 22300). Texten, chiffret (ett stort `A` är `^` i `AcisData::lines`; `^ ` i ett ASCII-
  DXF är DXF:s eget cirkumflex) och formatet finns i [doc/reference/hdxf_acis_sat_21500.md](doc/reference/hdxf_acis_sat_21500.md).
* **Verifiering.** 98 tester i `hdxf_acis_tests`; mot AutoCAD 2025:s facit (utbredning, topologi, geometri,
  transformationer); och AutoCAD:s kärna (DWG TrueView 2027) mätte volym, area och tyngdpunkt för byggarens kroppar
  i DXF 2010 och 2018 (17 byggda fall och 8 av AutoCAD:s egna kroppar) — låda, sfär, cylinder, kon, torus, regioner, en roterad låda, ett hål, två lumps, en hålighet, en
  NURBS-cylinder, en cylinder med söm; slutna och öppna BODY läses som BODY — lika med de analytiska värdena och med AutoCAD:s egna kroppar av samma
  former. Former som AutoCAD:s egna filer inte innehåller (sömmar, hålrumsskal, NURBS-solider i 21500) är mätta men
  saknar facit; referensen listar dem.
* **Begränsningar.** En kropp per SAT, inga trådar eller toleranta kanter, inga attribut (AutoCAD:s historik hittas inte på),
  bara rotation och likformig skala, cirkulära koner och ringtorusar, degenererade kanter bara vid en kons spets.

```bash
cmake -B build -G Ninja                     # bygger biblioteket och dess tester; -DHDXF_BUILD_ACIS=OFF utelämnar dem
./build/libs/hdxf_acis/test/hdxf_acis_tests
python libs/hdxf_acis/tools/acad_measure.py <dir>   # efter hdxf_acis_export_cases <dir>; kräver DWG TrueView
```

## Använda i ditt projekt (CMake FetchContent)

```cmake
include(FetchContent)
FetchContent_Declare(
    hdxf
    GIT_REPOSITORY https://github.com/holmbom/HDxf.git
    GIT_TAG v1.9.1
)
FetchContent_MakeAvailable(hdxf)

target_link_libraries(your_target PRIVATE hdxf::hdxf)   # hdxf::acis för kroppsbyggaren
```

### Färdigbyggda bibliotek

Varje release har färdigbyggda Release-bibliotek för Windows (MSVC och MinGW) och Linux (GCC), så att biblioteket
inte behöver kompileras i ditt projekt. Packa upp ett och peka CMake dit:

```cmake
find_package(hdxf 1 CONFIG REQUIRED)   # -DCMAKE_PREFIX_PATH=sökväg/till/HDxf-vX.Y.Z-…
target_link_libraries(your_target PRIVATE hdxf::hdxf)
```

Välj paketet för din kompilator: statiska C++-bibliotek går inte att blanda mellan MSVC, MinGW och GCC. Alla paket
är Release-byggen; ett Debug-bygge av ditt program med MSVC behöver källkoden i stället.

## Kända fel

- **MSVC:** ett prov faller — `dxf_unicode.dxf` i korpusen för material och visuella stilar skrivs inte tillbaka som
  den lästes när dess poster skrivs ur de typade värdena. GCC (MinGW och Linux) går igenom. Rättas i 1.9.2.

## Författare

Erik Holmbom. Byggt med hjälp av Claude (Anthropic).

## Licens

[MIT](LICENSE) © 2026 Erik Holmbom
