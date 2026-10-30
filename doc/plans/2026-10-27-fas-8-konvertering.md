# Fas 8 — Konvertering mellan versioner

**Status:** kompakt implementationsplan 2026-10-27; ingen implementation eller
programverifiering utförd i planuppdraget. Utgåva **1.10.0**, efter färdig fas 9 (**1.9.0**).

## Mål och omfattning

En **läst** ritning ska kunna sparas i varje annan version HDxf skriver:
**R12, R13, R14, 2000, 2004, 2007, 2010, 2013 och 2018**, ASCII och binärt.
Det är **9 × 8 = 72 riktade käll-/målpar**, 144 mål-/formatrutor. Även båda
källformaten provas: **288 kombinationer** före innehållsvariationerna.
De nio diagonalerna är fortsatt B12-rundtur, inte konvertering.

Utdata ska motsvara vad AutoCAD gör när det öppnar **samma källfil** och sparar som
målet med SAVEAS/DXFOUT: struktur, typade värden, äldre representationer, grafik,
relationer och belagd dataförlust. Ett annat versionsprov av ungefär samma ritning
är inte ensamt konverteringsfacit. Typade ändringar efter load ingår också.

- Uppgradering: R12 → R13…2018, R13/R14 → 2000…2018 och samtliga senare byten.
  Generera rätt handles, ägare, subclasser, tabellhuvuden, BLOCK_RECORD, OBJECTS,
  CLASSES och layouts; bevara användardata och återför äldre hjälprepresentationer.
- Nedgradering: samtliga äldre mål, inklusive R13 ↔ R14 och R12. Använd AutoCAD:s
  ersättningsgeometri, proxy-/round-trip-former, utelämnande eller fel per innehåll.
  LWPOLYLINE → POLYLINE och MTEXT → TEXT räcker inte som hela konverteringen.
- **Alla typer/fält från fas 9 ingår**, även dess A/B/C-underlag, F9.1–F9.27 och
  dokumenterade undantag. Fas 9 görs först; dess målcodecs återanvänds. Ingen typ,
  ACIS-riktning, äldre form eller okänd rest skjuts till en senare fas.
- B12:s postmall används **bara när källversion = målversion**. Vid konvertering
  skrivs posterna på nytt från typade värden med målversionens codecs.
  Att ändra ACADVER eller släppa den nuvarande spärren är inte konvertering.
- Felaktigt mål och innehåll som inte kan konverteras ger **namngivet fel före
  mutation** av struktur, handles, källdata, filename eller destinationsfil.
  AutoCAD-belagd förlust vid en lyckad konvertering redovisas i `warnings()`.
- Saknat och omöjligt AutoCAD-facit redovisas **ej testat** per typ/fält/gren,
  käll-/målversion och format. Det stoppar inte fasen men är inget godkänt test
  eller tillstånd att hitta på ett format. Verkliga kodfel rättas inom fasen.

Underlag: [CLAUDE.md](../../CLAUDE.md), [TODO.md](../../TODO.md),
[fas 9:s arbetsform](2026-10-27-fas-9-typer.md),
[fas 7](2026-10-25-fas-7-dxf-r13-r14.md) och
[R13/R14:s formatprofil](../reference/hdxf_r13_r14_format.md).
B1/B7/B12/B19, tillämpliga versionsbeslut B20–B99 och B100–B117 gäller;
fasens nya beslut börjar med **B118**. Konverteringsreglerna ersätter tidigare
avsiktliga konverteringsspärrar, inte samma-versionsavtalet eller filformaten.

## Avläst utgångsläge

- `src/drawing.cpp::write_dxf()` beräknar `from_file` för R13+ och separat
  `r12_from_file`; läst R13…2018 till annat mål ger `Writing DXF X as Y is not
  supported yet (the drawing was read from that version)` före förberedelserna.
- `source_version_ == R12` används också för nya ritningar. `loaded_r12_` skiljer
  vissa R12-vägar, men `set_version()` kan redan göra strukturändringar för R12.
  Ursprung måste därför göras explicit; den grenen är inte bevisat färdig konvertering.
- `target_version_error()` samlar äldre fel, GEODATA-versionsfel, SAT→SAB-kontroll
  och egenskapskontroller för 2004/2007/2010/2013/2018 före strukturskapande och handles.
  `older_checks.cpp` har undantag för oförändrad läst data i samma version. Dessa
  mallundantag får inte legitimera främmande data i ett konverterat mål.
- `drawing_r2000.cpp::ensure_r2000_structure()` lämnar normalt befintliga OBJECTS
  orörda; dess layoutkomplettering är främst för minnesbyggda ritningar.
  `drawing_r13_r14.cpp::ensure_older_structure()` är heller ingen generell
  layoutnedgraderare. Ett anrop till en nyritningskonstruktor räcker inte.
- `drawing_roundtrip.cpp` har 2013-egenskaper som äldre XRECORD/XDATA.
  `drawing_mtext.cpp`, `drawing_attribute.cpp` och `attribute_mirror.cpp` har
  MTEXT-kolumner/ramar och flerradiga attribut före 2018. Deras läsning viker ihop
  hjälpposter; konverteringen måste konsumera/regenerera dem utan dubblering.
- `dxf_writer_template.cpp` jämför mall/original/aktuellt typat värde, och använder
  mallen även för relationer, råsektioner, tabellhuvuden och klasslistor.
  Konvertering behöver relationsunderlaget men får inte skriva källmallen vidare.
- `acis_sab.hpp` erbjuder SAT→SAB, inte en färdig SAB→SAT-/SAT-nedgraderare.
  R13/R14-kontrollen i aktuell kod godtar nya SAT-versioner 100–106; profilens
  äldre tabelltext och dess senare utfall måste hållas isär. SAT 400 är inte
  automatiskt R14. En ändrad SAT-huvudrad är aldrig en kroppskonvertering.

Detta är en filbaserad läsning av nuläget, inte ett bygg-/testresultat. Efter fas 9
återkontrollerar 8.0 berörda gränser och dess faktiska typer; gamla spärrar kan då
redan ha ersatts med fas 9:s codecs.

## Arbetssätt

1. Ett grundsteg **8.0**, sedan **tio stora implementeringssteg** i tre vågor om
   högst fyra. Varje steg är ett sammanhängande agentuppdrag med implementation
   och testkod. **Start/återstart av underagenter kräver Eriks uttryckliga OK för
   implementationen.** Planuppdraget startar inga agenter och kör inga skal.
2. Implementera först; skriv riktade tester med koden. Varje agent bygger och kör
   **hela sviten en gång i slutet av steget**, redovisar och rättar. Därefter bara
   nödvändiga omkontroller. Befintlig CMake/Ninja/UCRT64-konfiguration används.
3. **Inga ODA-/accoreconsole-kontrollkörningar per agentsteg.** Samordnaren kör
   däremot AutoCAD-facitskripten **tidigt**, med grundens körhjälpare och senast
   före beroende implementationsvåg. Detta skapar AutoCAD-indata och SAVEAS-facit;
   det är ingen kontrollkedja för HDxf-utdata. Resultaten delas mellan stegen.
4. Separata arbetskopior från samma vågbas; disjunkta huvudfiler inom vågen.
   Gemensamma patchar sammanförs sekventiellt. Samordnaren integrerar varje våg,
   löser kopplingar och kör ett integrationsbygge, fullsviten och korsfamiljsfallen.
5. Efter våg 3: **en Codex-granskning**, inklusive att planens alla delar är gjorda;
   **ett samlat slutteststeg med AutoCAD 2025 + ODA**; samordnarens rättningssvep
   och nödvändiga omkontroller; dokumentation; utgåva **1.10.0**.
6. Endast AutoCAD-producerade filer är facit. Inga goldenfiler. Syntetiska små
   fel-/enhetstester får finnas men får inte bestämma AutoCAD:s serialisering.
   Ingen kod från ezdxf, ACadSharp, LibreDWG eller ODA kopieras. AutoCAD-original
   i deras samlingar får läsas med dokumenterad proveniens och distributionsrätt.
7. Skilj **implementerat**, **testkod körd**, **AutoCAD-verifierat**, **belagt fel**
   och **ej testat**. En lyckad process, oförändrad rå rundtur, klassregistrering
   eller ODA-konvertering bevisar inte korrekt SAVEAS-konvertering.
   Git Bash och filverktyg vid implementation, aldrig PowerShell.

### Gemensamma filer H/W/E/O/C/T

Huvudlogiken ligger i stegets egna `conversion_*.cpp`, inte i delade switchar.
Alla nya filnamn nedan är avsedda filgränser; befintliga codecs återanvänds.

| Kod | Gemensam fil | Tillåten ändring i familjesteg |
|---|---|---|
| H | `include/hdxf.hpp` | Grundens privata ursprungs-/exportkopplingar; familjens nödvändiga deklaration. Ingen ny stor publik konverteringsmodell. |
| W | `src/dxf_writer_2000.cpp` | Tunn mål-/familjedispatch till befintlig eller egen målcodec; ingen mall för annat mål. |
| E | `src/dxf_reader.cpp` | Endast nödvändig typad läskoppling för en äldre hjälprepresentation. Ingen konvertering under load. |
| O | `src/object_reader.cpp` | Motsvarande tunn objektkoppling för round-trip-/proxydata. |
| C | `src/CMakeLists.txt` | Lägg till just stegets källfiler. |
| T | `test/CMakeLists.txt` | Lägg till just stegets testfiler. |

Varje agent har sin namngivna H/W/E/O/C/T-patch för slutbygget i sin arbetskopia;
samordnaren applicerar dem sekventiellt vid integration. `—` betyder ingen ändring.
`drawing.cpp`, `dxf_writer.cpp/.hpp`, `dxf_writer_template.cpp`, strukturskapare,
`handles.hpp`, fas 9:s register och `extents.cpp` ändras i grunden/integrationen
eller med deklarerad tunn koppling. Ingen samtidig omskrivning av dessa filer.
Manifest, facitförteckning och matris sammanförs likadant. Ny delad hjälpare beslutas
och integreras mellan vågor; två agenter får inte samtidigt äga den.

## Vågöversikt

| Våg | Steg | Huvudfiler under `src/` |
|---|---|---|
| 0 | 8.0 Grund, ursprung och konverteringskontrakt | `ConversionTypes.hpp`, `conversion.cpp`, `conversion_registry.cpp` samt grundkopplingar i I/O |
| 1 | 8.1 Struktur, handles, ägare och layouts | `conversion_structure.cpp`, `conversion_spaces.cpp` |
| 1 | 8.2 Header, tabeller, namn och kodning | `conversion_header.cpp`, `conversion_tables.cpp`, `conversion_names.cpp` |
| 1 | 8.3 Text, attribut, mått och annotationskontexter | `conversion_text.cpp`, `conversion_annotation.cpp` |
| 1 | 8.4 Kurvor, fyllningar och övrig grundgeometri | `conversion_curves.cpp`, `conversion_hatch.cpp`, `conversion_geometry.cpp` |
| 2 | 8.5 ACIS, SAB/SAT, ytor och kroppsgrafik | `conversion_acis.cpp`, `acis_sat_target.cpp`, `acis_sab_to_sat.cpp` |
| 2 | 8.6 Färg, material, visuella stilar, render och ljus | `conversion_appearance.cpp`, `conversion_styles.cpp`, `conversion_render.cpp` |
| 2 | 8.7 Tabeller, dokumentdata, referenser, sektioner och geodata | `conversion_document.cpp`, `conversion_references.cpp`, `conversion_geodata.cpp` |
| 2 | 8.8 Dynamiska block, historik och associativa grafer | `conversion_dynamic.cpp`, `conversion_history.cpp`, `conversion_assoc.cpp` |
| 3 | 8.9 Proxyer, opaque data och alla rårester | `conversion_proxy.cpp`, `conversion_extensions.cpp`, `conversion_residual.cpp` |
| 3 | 8.10 Relationsslutning och full konverteringsmatris | `conversion_relations.cpp`, `conversion_finalize.cpp` |
| Slut | 8.S Granskning → sluttest → rättning → dokumentation → 1.10.0 | Samordnarens integrations-/leveransfiler |

Grunden har färdiga gränser för alla steg. Våg 1:s steg lämnar ändringsbeskrivningar
till grundens gemensamma graf; de anropar inte en vågkamrats ofärdiga implementation.
Våg 2 bygger på färdig struktur/text/geometri, men inte på andra steg i våg 2.
Våg 3:s slutning tar emot proxyresultat genom **8.0:s avtal**, inte genom väntan på
8.9:s kod. Gemensamma proxy-/relationsfall provas när våg 3 integreras.

## Beslut B118–B132

| Beslut | Bindande innebörd |
|---|---|
| **B118** | Alla 72 riktade par, båda käll-/målformaten och fas 9:s hela typ-/fältregister ingår. Samma version följer B12; alla andra versioner använder typad målutskrift. Ingen obligatorisk kedja genom R2000 eller senaste version. |
| **B119** | Behåll API-flödet `load → set_version(target) → save_as(path, format)` samt `save()`/`save_to_memory()`. För lästa ritningar väljer `set_version()` endast mål. Konvertera en isolerad exportkopia; ändra inte live-modellens källvärden, raw, handles eller ursprung, även efter lyckad export. `version()` anger valt mål; `save_as()` ändrar filename först efter lyckad skrivning. Återval av källversion återger dess kvarvarande B12-bas och typade ändringar. Exporterad målfil som öppnas på nytt har däremot målets faktiska förluster. |
| **B120** | Explicit läst/nytt ursprung, källversion/-format/-familj och målprofil. Förkontroll → semantisk konvertering på kopia → hjälpposter/grafik → relationsslutning → målvalidering → serialisering i minne → filskrivning. Ingen mallfallback över versionsgränsen. Opaque delar klassificeras före rensning av raw. |
| **B121** | AutoCAD 2025 öppnar exakt original och sparar oberoende mål: R12 och 2000–2018. Historiska original/referenser styr R13/R14; modern läsning är inget historiskt SAVEAS-facit. Proveniens, källa, sparningskedja, inställningar, hash och innehåll registreras. |
| **B122** | Struktur byggs från bevarad semantik och målprofil, inte från en tom ritnings godtyckliga defaults. N/A2000 hålls isär. Samma-version behåller familj; annan version får dokumenterad målprofil enligt historiskt SAVEAS-underlag, annars profilens N-form med SAVEAS-likheten uttryckligen ej testad. Inga automatiska A2000-ägare eller layout-hjälpgrafer enbart på grund av ett modernt källårtal. |
| **B123** | Varje källa ger en ändringsbeskrivning: behållen post, en-till-en, en-till-flera, sammanslagen, hjälppost eller borttagen. B19/fas 9:s pekarslag styr remappning; kodnummer ensamt är aldrig relationssemantik. Bevara externa/ursprungligen brutna länkar där målformen tillåter; skapa inga nya oavsiktligt brutna länkar. |
| **B124** | AutoCAD:s ersättningsgeometri och parametrar avgör R12 och andra äldre mål: block/INSERT, TEXT, POLYLINE, LINE, SOLID, 3DFACE etc. efter verkligt facit. Ingen generell EXPLODE, valfri tessellering eller klippning vid ritningsgräns som egen förenkling. |
| **B125** | Återanvänd 2013:s round-trip-XRECORD/XDATA och 2018:s äldre MTEXT-/attributformer i båda riktningar. Konsumera gamla kompanjoner före regenerering; native och äldre form får inte ge dubbla texter, block, ramar eller återuppväckta borttagna värden. |
| **B126** | ACIS-konvertering är typad omkodning av topologi, geometri och versionsfält: SAB 21800/22300 ↔ målets SAT/SAB, äldre SAT-schema och målkompatibla record-/subtypformer. Ingen bytekopia till inkompatibelt mål eller patch av versionshuvud. R12:s kroppsersättning är AutoCAD:s grafikform, inte SAT. |
| **B127** | True color/färgbok → AutoCAD:s närmaste ACI i mål utan stöd, med BYLAYER/BYBLOCK och lagrets av/på separat. Transparens, gradient, lineweight, plot style, material, visuella stilar och render får belagd native/äldre/proxy-/förlustform. Förlust av redigerbarhet eller egenskaper varnas. |
| **B128** | Varje fas 9-typ/fält har en käll-/målregel inklusive klassversioner, äldre proxyer, round-trip-grafer, cache, grafik och resurser. Introår är inget generellt förbud; målcodecs används, inte råpostkopior. Ingen extern evaluator eller modelleringsmotor krävs vid vanlig save. |
| **B129** | Unknown, okända fält/101-delar/102-grupper, råtabeller, råsektioner och opaque payloads inventeras. AutoCAD-belagd kompatibel proxy kan skapas, belagt utelämnande ger varning, omöjlig/obelagd säker konvertering ger namngivet fel. Bevara bytes bara där målgrammatik och interna referenser tillåter det. Ingen tyst förlust och ingen fabricerad klasspayload. |
| **B130** | Hela ritningen förkontrolleras: header, tabeller, block, modell/papper, attribut och objekt. Ogiltig enum/målprofil, intervall, kodning, namn och ej konverterbart innehåll får stabilt fel med typ/fält, källa, mål och handle/namn. Befintliga `requires DXF …` gäller kvarvarande verkliga målbegränsningar, inte sådant som nu kan konverteras. |
| **B131** | Testmatrisen skiljer faktisk konverteringsregel från bevisstatus. Native, äldre form, proxy, utelämnat med varning och belagt fel är olika utfall; ej testat är en separat axel. Fullsvit per agentsteg, en Codex-granskning och ett samlat externt sluttest enligt denna plan. |
| **B132** | Saknat facit söks och framställs tidigt; när det inte går anges exakt lucka/skäl. Känd grammatik implementeras och kodtestas ändå. Okänd grammatik får `format ej fastställt; konvertering ej verifierad` och säkert namngivet konverteringsfel, aldrig falskt introduktionsårsfel. Undantaget redovisas inom fasen; inget nytt senare fasuppdrag skapas. |

## Facit och tidig framställning

Sök först i `doc/acad_*_dxf.dxf`, `doc/facit/`, `build/test-data/`, fas 9:s nya
original och R13/R14-profilens pinnade historiska filer/`local-r13r14/PROVENANCE.txt`.
Läs innehåll och producent; andra biblioteks utdata är robusthetsindata, inte facit.
Registrera nya original i `test/data/manifest.cmake`/`test/data/README.md`, med
fast URL/commit + SHA256 där det är externt, respektive lokal AutoCAD-proveniens.
Ingen extern sökning eller AutoCAD-körning har utförts i detta planuppdrag.

`doc/acad_r12_dxf.dxf` visar grundformen, men innehåller inte alla nedgraderingar.
Befintliga R12-filer från `02_basic`, `03_text`, `06_hatch`, `10_solids`,
`11_surfaces`, `12_tables` och `08_layouts` visar redan ersättningsposter och block.
De ska läsas som faktisk AutoCAD-utdata: exempelvis får inte HATCH generellt antas
bli en konturpolyline eller MTEXT generellt antas bli en enda TEXT.
Säkra exakt källa → mål-kedja innan dessa används som konverteringsfacit.

Samordnaren kör **AutoCAD 2025:s kärna**, inte DWG TrueViews kärna:

```text
C:/Program Files/Autodesk/AutoCAD 2025/accoreconsole.exe /i <in> /s <scr> /l en-US
```

Varje orakel öppnar originalet på nytt före målvalet, med full DXFIN-/öppningssekvens
när indata kräver det. Spara som DXF med SAVEAS/DXFOUT i R12, 2000, 2004, 2007,
2010, 2013 och 2018, ASCII med fastställd precision samt binärt. Återanvänd befintliga
promptsekvenser, kontrollera slutet på prompt/logg och faktisk fil, inte bara `KLAR`.
Undvik att föregående nedgradering blir nästa provs källa. Avsiktliga flerstegskedjor
har egna namn och samma kedja i HDxf och AutoCAD. Ingen HDxf-fil blir facitindata.

Av de 72 paren har **56** mål som AutoCAD 2025 kan spara. De **16** paren till
R13/R14 kräver historiska AutoCAD-original/referenser. För R13↔R14 omfattar detta
båda källfamiljerna där de finns. Ingen R13/R14-sparning utlovas från AutoCAD 2025;
det som inte kan beläggas blir **ej testat**, även när modern AutoCAD/ODA kan läsa det.

### Facitskript F8.1–F8.10

Alla skript ligger i `doc/facit/`, ändrar/skapar endast AutoCAD-original och sparar
de möjliga versionerna i båda format. `fas8_saveas.scr` är gemensam sparningssekvens;
körhjälparen instansierar mål och källa för separata processer. Familjeskripten
skapar/öppnar isolerade fall, inte uppfunna råklassposter.

| Id | Skript | Innehåll och tidpunkt |
|---|---|---|
| F8.1 | `fas8_saveas.scr`, `fas8_structure.scr` | Säkra omöblerat källoriginal per version, tom/icke-tom ritning, R12 med/utan handles, modell/papper, flera layouts och aktuell/inaktiv layout. Kör i 8.0, före våg 1. |
| F8.2 | `fas8_header_tables.scr` | Icke-default header/DIMSTYLE, långa/Unicode-/kolliderande symbolnamn, kodsidor, komplex LTYPE och stil-XDATA, tabellposter tillagda/borttagna. Före våg 1. |
| F8.3 | `fas8_text_annotations.scr` | MTEXT-format/stacks/kolumner/ram, skalade/roterade/speglade flerradsattribut, måttblock och flera annotationsskalor; äldre → native också. Före våg 1. |
| F8.4 | `fas8_geometry_r12.scr` | LWPOLYLINE, ELLIPSE/båge, rationell/periodisk/3D-SPLINE, HATCH med öar/pattern/solid/gradient/splinekant, XLINE/RAY, LEADER/TOLERANCE/MLINE, MESH/HELIX. Isolera varje R12-ersättning och styrande inställningar. Före våg 1. |
| F8.5 | `fas8_acis.scr` | 3DSOLID/REGION/BODY och ytor med trim/hål/spline/transform/flera lumps, SAT/SAB och isolinjer; bevara AutoCAD:s egna äldre strömmar, mät kropparna. Före våg 2. |
| F8.6 | `fas8_appearance.scr` | RGB/färgbok/ACI, lager av, transparens, gradient, lineweight/plot style, material/visual style, ljus/sol/render och thumbnails. Före våg 2. |
| F8.7 | `fas8_document.scr` | ACAD_TABLE med kompanjoner/fält, index/filter/ritordning, IMAGE/WIPEOUT/underlägg/coordination/xref, GEODATA v1/v2/v3 med riktigt koordinatsystem, sektioner/ModelDoc och dokumentdata. Före våg 2. |
| F8.8 | `fas8_graphs.scr` | Dynamiska block/visibility/actions, constraints/arrays/assoc-mått och -ytor, solidhistorik och persistenta subentiteter; aktiva anonyma representationer före/efter ändring/borttagning. Före våg 2. |
| F8.9 | `fas8_proxy_extensions.scr` | OLE/proxyer, Map/TC/ZWM, VBA/OBJECT_PTR och okända restklasser: öppna riktiga original med tillgänglig applikation/plugin; native/proxy/utelämnande/fel och opaque bytes. Före våg 3. |
| F8.10 | `fas8_relations.scr` | Blandad helritning med delade resurser, en-till-flera-ersättning, borttaget mål och ursprungligen bruten/extern länk; samma-version efter målbyte och flerstegskedjor. Start i 8.0, komplettera före våg 3. |

Uppdatera `doc/facit/coverage.md` och `run_all.txt` vid implementation. Körningen
registrerar källa/format/hash, mål/format/hash, full kommando- och loggfil, ACADVER/
MAINTVER, post/handle, native-/proxy-/förlustutfall och resurser (font, bild, xref,
underlägg, NWD/NWC, HDR). Versionsfilernas antal ersätter inte postkontroll.

GUI/pluginbundna grenar, OLE-payload, Map, coordination och ModelDoc kan sakna
framställningsväg i tillgänglig kärna. Försök med befintliga original; ett hoppat
NWCOUT/VIEWBASE eller en CLASS utan instans bevisar inte innehållet. Ange exakt
F8-/F9-lucka och **ej testat**, utan krav på att Erik manuellt ska skapa den för
att fasen ska gå vidare. R13/R14-binärfacit och sluten äldre ACIS kan också saknas.

## Gemensamt testkontrakt T

T gäller alla steg och varje konkret typ/fält från fas 9; testkod skrivs med koden.

- **Riktiga lästa källor:** `load` och `load_from_memory`, båda källformaten.
  Prova alla 72 par och båda målformaten genom `save_as`, `save_to_memory` och
  relevant `save` efter målval. Ingen matris byggd enbart av nya målritningar.
- **Semantik och utdata:** jämför mot AutoCAD:s utdata av exakt samma källa.
  Typ-/postföljd, fält/närvaro, målsubclasser, kompanjoner, grafik och relationer
  jämförs efter explicit handle-isomorfi och dokumenterade volatila metadata.
  Maskera inte värden, postantal, förlust eller klassordning som behöver kontrolleras.
  Numerisk tolerans motiveras av facitprecision; samma-version/samma-format har B12.
- **Typad konvertering:** ändra en scalar, optional och lista där de finns före
  export; ta bort en post och återexportera. Tömd raw för kända typer ska ge samma
  målsemantik. Opaque/okända delar går genom B129, inte genom testets blinda rensning.
- **Ursprung och transaktion:** vald målversion får inte förstöra läst R12 heller.
  Prova källa → mål → återvald källa utan reload, upprepade exporter, kopiering/move
  och oberoende mål. Live-källdata/raw/handles bevaras; filename byts bara vid lyckad
  `save_as`. Samma filnamn som källan får användas först efter full förberedelse.
- **Fel:** ogiltig enum/profil, olagligt namn/intervall/UTF-8, avkortad payload och
  ej konverterbar rest. Sent upptäckt familjefel får inte ge handles, ändrade
  strukturer eller trunkerad destination. Kontrollera snapshot, destinationshash,
  `last_error()` och att varje I/O rensar gamla fel/varningar. Licensspärren gäller.
- **B19:** en-till-flera och sammanslagning, ägare/extension dictionaries/reactors,
  GROUP, SORTENTSTABLE, BLKREFS, IMAGE, HATCH, LEADER, LAYOUT och fas 9-grafer.
  Interna id/sorteringsnycklar/externa handles är skilda. Namnremappning följer
  alla referenser, inklusive header, overrides, attribut och dictionaries.
- **Geometri:** modell/papper/block, OCS/WCS, skalat/roterat/speglat INSERT,
  klippning, extents och textlayout; aktuell proxy/cache och resurser. Om AutoCAD
  ersätter en typ prövas geometrin och redigerbarhetsförlusten, inte bara load.
- **Kedjor:** återläs exporterat mål och konvertera igen; jämför samma AutoCAD-kedja.
  Kräv inte att data som AutoCAD förlorat i ett äldre mellanled återuppstår.
  Native återföring av verkligt bevarade round-trip-data får däremot inte utebli.

Matrisnyckel: **källfil/hash, källversion/familj/format, typ/fält/gren, målversion/
format, regel, testfall, AutoCAD-underlag, körstatus och ej testat-skäl**. Registrera
även nya/ändrade/borttagna fall. Saknas filfacit körs kodtester för känd grammatik;
de ger inte AutoCAD-status. B7 och externa läsare körs samlat i 8.S.

## Steg

### 8.0 — Grund, ursprung och exportkontrakt (våg 0, en agent)

- **Mål:** återkontrollera färdig fas 9 och ge varje typ/fält en ägare i denna plan.
  Skilj ny/läst R12 från läst R13+; behåll källfamilj/-format oberoende av valt mål.
  Inför intern konverteringskontext/register, exportkopia, preflight, målprofil,
  ändringsbeskrivning och diagnostik. Fast körordning och gräns mellan typad data,
  grafikkonsumenter, relationer och opaque rester. Behåll nyritningsflödets avtal.
- **Filer:** `src/ConversionTypes.hpp`, `conversion.cpp`, `conversion_registry.cpp`,
  grundkopplingar i `drawing.cpp`, `dxf_writer.cpp/.hpp`, `dxf_writer_template.cpp`;
  `tools/conversion_inventory.py`, `tools/fas8_facit.py`,
  `test/conversion_matrix.hpp`, `test/test_conversion_contract.cpp`.
  H/W/E/O/C/T: H ursprung/privata anrop; W målväg; E/O —; C/T registrering.
- **Beslut:** B118–B123, B129–B132. Ogiltigt `set_version()`-värde lämnar tidigare
  mål kvar och sätter namngivet fel; I/O gör egen målvalidering. Konverteringen
  använder exportkopia enligt B119, utan att omsätta källans raw till målmall.
  Inventera även okända delfält, råsektioner och typade opaque bytes före raw-rensning.
- **AutoCAD-underlag/facit:** F8.1/F8.10, doc/acad_*, historiska N/A2000 och fas 9:s
  register/matris/utfall. Skriv gemensamma facitskript/körhjälpare och familjernas
  beställningslista; samordnaren kör möjlig facitframställning före våg 1.
- **Testkod:** T; källa/valt mål/ny ritning, läst R12 utan och med OBJECTS-liknande
  rårester, mall spärrad vid annat mål, tillbaka till B12, tidigt/sent fel,
  oförändrad live-modell, upprepade exports och licens. Agentens slutbygge/fullsvit.
- **Ej testat:** faktisk SAVEAS-likhet för oåtkomliga käll-/måloriginal; en färdig
  konverteringsdispatch eller ett reserverat registerfält är inte en färdig familj.

### 8.1 — Struktur, handles, ägare och layouts (våg 1)

- **Mål:** alla upp-/nedgraderingsvägar för sektions-/ägargraf, rumsblock,
  BLOCK_RECORD, CLASSES, OBJECTS, dictionaries, layouts/plot settings och VIEWPORT.
  R12 får åtta tabeller och rätt entitets-/underentitetshandles; R13+ får rätt
  tabellhandles och ägarformer. Komplettera befintlig struktur, dubblera den inte.
  Vid äldre mål: belägg aktuell/inaktiv layouts geometri, ett paperspace,
  viewportklippning och layout-/plotdata som AutoCAD bevarar eller utelämnar.
- **Filer:** `src/conversion_structure.cpp`, `conversion_spaces.cpp`,
  `test/test_conversion_structure.cpp`, `test/test_conversion_layouts.cpp`.
  Återanvänd `drawing_r13_r14.cpp`, `drawing_r2000.cpp` och standardobjektskapare
  genom tunna anrop. H/W/C/T koppling; E/O —.
- **Beslut:** B120/B122/B123/B130. För varje källfamilj → R13/R14 dokumenteras
  målprofil, owner-330, CLASS utan 91, dictionary-/XRECORD-huvuden och layout-hjälpform.
  N får inga A2000-fält av misstag; A2000-data får inte raderas med en namnsvartlista.
  R13/R14 → 2000+ återför hjälplayouts där belagt, annars skapar rätt saknade struktur.
- **AutoCAD-underlag/facit:** F8.1/F8.10, `01_empty`, `08_layouts`, `04_blocks_attributes`,
  formatprofilens 1.5/4/6/9/12 och historiska AutoCAD-par. Nya ritningars extra
  default-layouts är inget bevis för vad SAVEAS av en läst R12-fil skapar.
- **Testkod:** T; tom/icke-tom källa, flera paper-block, aktiv layout varierad,
  grupper/delade objekt, INSERT/VERTEX/ATTRIB/SEQEND, inga dubbletter och HANDSEED
  över alla målhandles. Varje strukturriktning och N/A2000 separat.
- **Ej testat:** historiska layout-/plotstylekonverteringar utan parfacit samt
  oåtkomliga R13/R14-binärformer; modern läsning ger endast läsningsbevis.

### 8.2 — Header, tabeller, namn och textkodning (våg 1)

- **Mål:** målversionens ACADVER/MAINTVER/REQUIREDVERSIONS, variabelurval/ordning,
  korrekta värdetyper/defaults och bevarade användarvärden. Alla tabellfält,
  DIMSTYLE/overrides, DIMUNIT/DIMFIT ↔ moderna fält, pilnamn/handles, komplex LTYPE,
  STYLE/fontdata och BLOCK/ENDBLK-egenskaper. Kodning enligt B1/B55 i båda riktningar.
- **Filer:** `src/conversion_header.cpp`, `conversion_tables.cpp`,
  `conversion_names.cpp`, `test/test_conversion_header_tables.cpp`,
  `test/test_conversion_encoding.cpp`. H/W/E/O endast nödvändiga fältkopplingar;
  C/T registrering. Befintliga header-/dimstyle-/text_encoding-codecs återanvänds.
- **Beslut:** B122/B123/B125/B127/B130. Fastställ AutoCAD:s äldre namnkonvertering
  (31-teckens-/teckenregler, case, xref/anonyma/systemnamn), kollisionshantering
  och alla namnreferenser. Ingen egen tyst trunkering; obelagd/omöjlig mapping ger
  namngivet fel. Ursprunglig råtext får inte kringgå målets kodsida/escaping.
- **AutoCAD-underlag/facit:** F8.2, `07_layers_styles`, `15_unicode`, annotative
  tabelländringspar, doc/acad_r12 och R13/R14-profilens 3/5/8/11/13/14.
  Förlust av layer/style-XDATA till R12 avgörs per verklig AutoCAD-form.
- **Testkod:** T; långa/kolliderande namn, dos932/ANSI_1252/UTF-8, svenska/symboler/
  U+FFFF+ med surrogates, caret, TrueType/SHX, DIMSTYLE-pilar och overrides.
  Frånvaro kontra default och int16/int32/int64 i ASCII/binärt, räknare och extentsheader.
- **Ej testat:** historisk kodsida/namnmapping och sällsynta DIMSTYLE-kombinationer
  utan sparat original; profiltextens osäkerheter får inte döljas av en syntetisk fil.

### 8.3 — Text, attribut, mått och annotationskontexter (våg 1)

- **Mål:** MTEXT till AutoCAD:s TEXT-/blockform i R12, även stycken, formatrun,
  stacks, fontbyte, riktning, bakgrund, ram och kolumner. 2018 Embedded Object ↔
  äldre XDATA/MTEXT/ramar; multiline ATTRIB/ATTDEF ↔ ACAD_MLATT/ACAD_MATT_HEIGHT/
  spegelattribut och R12:s faktiska attributform. Vanliga/specialmått, ledartext,
  annotationsskalor/kontexter och äldre val av synlig representation ingår.
- **Filer:** `src/conversion_text.cpp`, `conversion_annotation.cpp`,
  `test/test_conversion_text.cpp`, `test/test_conversion_annotations.cpp`.
  Återanvänd `drawing_mtext.cpp`, `drawing_attribute.cpp`, `attribute_mirror.cpp`,
  textlayout och dimensionblock; H/W/E/O tunna kopplingar; C/T registrering.
- **Beslut:** B123–B125/B128/B130. Konsumera befintliga sekundärposter innan
  målformen skapas. Bevara taggar/låst läge, justering, OCS och INSERT-transform;
  använd facitregeln för rader kontra formatruns, inte en generell textsplit.
  Måttstil/specialsymboler och kontextens geometri ska följa med till ersättningsblock.
- **AutoCAD-underlag/facit:** F8.3, `03_text`, `04_blocks_attributes`, `05_dimensions`,
  `14_annotative`, `25_mtext_columns`, fas 9:s F9.19/F9.20 och 2013:s XRECORD-par.
- **Testkod:** T; kolumner statiska/dynamiska/auto-height/reversed, olika höjder,
  text i block/papper, attribut med olika skala/rotation/spegling och borttagen
  sekundärpost. Äldre → 2018 återför text exakt en gång; äldre mål bevarar de
  former AutoCAD faktiskt skriver. Kontrollera textinnehåll, taggar, grafik och lådor.
- **Ej testat:** attributkolumner, sällsynta kontext-/specialmåttsgrenar och
  fontberoende målutfall utan AutoCAD-fil/resurs anges separat enligt F8.3/F9-id.

### 8.4 — Kurvor, fyllningar och grundgeometri (våg 1)

- **Mål:** LWPOLYLINE → POLYLINE för R12/R13 med bulge, bredder, elevation,
  extrusion, stängning, vertex-id och XDATA. ELLIPSE, SPLINE, HATCH, XLINE/RAY,
  LEADER/TOLERANCE/MLINE samt HELIX/MESH får AutoCAD:s riktiga äldre målformer.
  Behåll native geometri där målet har den; uppgradering betyder inte att varje
  gammal POLYLINE godtyckligt blir LWPOLYLINE. R13:s HATCH/IMAGE-gräns får egen regel.
- **Filer:** `src/conversion_curves.cpp`, `conversion_hatch.cpp`,
  `conversion_geometry.cpp`, `test/test_conversion_curves.cpp`,
  `test/test_conversion_hatch_geometry.cpp`. H/W/C/T; E/O endast vid belagd äldre läsform.
- **Beslut:** B123/B124/B127/B128/B130. Fastställ AutoCAD:s segmentering,
  block/INSERT, mönsterlinjer, solida fyllningar, splineparametrar och styrvariabler.
  HATCH-öar, associativitet, gradientens äldre form och fitdata behandlas separat.
  Ingen egen splineprecision, viewportbaserad avkortning av XLINE/RAY eller tom proxy.
- **AutoCAD-underlag/facit:** F8.4, `02_basic`, `06_hatch`, `09_images`,
  `11_surfaces` och R13/R14-original/referens för native/ersättningsform.
  IMAGE/WIPEOUT-resursernas graf ligger i 8.7; här lämnas geometriskt resultat via avtalet.
- **Testkod:** T; rationella/periodiska kurvor, 3D, negativa bulges/bredder,
  hatch med hål/splinekant, ändrad/borttagen gräns, mesh crease/smoothing och helix.
  Kontrollera verkligt segment-/blockinnehåll, ordning, extents och källprecision.
- **Ej testat:** obelagda styrvariabler/segmenteringsgrenar och historiska
  R13-ersättningar anges per typ; en generisk polyline är inget AutoCAD-bevis.

### 8.5 — ACIS, SAB/SAT, ytor och kroppsgrafik (våg 2)

- **Mål:** samtliga riktningar för 3DSOLID/REGION/BODY, SURFACE och alla konkreta
  yttyper inklusive fas 9:s NURBSURFACE. SAB 22300 ↔ 21800, SAB → SAT före 2013,
  SAT → mål-SAB och äldre SAT-schema för R13/R14. R12 får AutoCAD:s egna
  ersättningsblock/kurvor/ytgrafik; 2004-ytor får sina riktiga isolineproxyer.
- **Filer:** `src/conversion_acis.cpp`, `acis_sat_target.cpp`,
  `acis_sab_to_sat.cpp`, `test/test_conversion_acis.cpp`,
  `test/test_conversion_surface_graphics.cpp`. Återanvänd `acis.cpp`, `acis_sab.cpp`,
  ACDSDATA och fas 9:s yt-/isolinekod via tunna anrop; H/W/E/O/C/T efter behov.
- **Beslut:** B123/B124/B126/B128–B130. Konvertera versionsberoende recordfält,
  subtyper, referenser, attribut, enheter, toleranser, produkt-/slutposter och
  pcurves med målgrammatik. R13/R14:s SAT 100–106 och äldre lästa undantag måste
  styrkas; välj inte SAT 400 för R14. ACDSDATA, GUID och thumbnails följer målformen.
  Historikgrafen hanteras av 8.8; ingen ospecificerad B-rep-regenerering.
- **AutoCAD-underlag/facit:** F8.5, `10_solids`, `11_surfaces`, `26_acis_referens`,
  fas 9:s F9.17/F9.18 samt historiska ACIS-filer/profilens 10 och faktiska utfall.
  Sök sluten R13/R14-kropp; referensimplementering utan sådant facit märks ej testat.
- **Testkod:** T; topologi/referensordning, trimmade/rationella/periodiska ytor,
  flera lumps/shells, degenererad kant, transform, toleranta poster och attribut.
  Kontrollera SAT/SAB:s innehåll och externa MASSPROP/AREA i 8.S. För mål där
  AutoCAD tappar kroppsemantik kontrolleras dess ersättningsgrafik och varning.
  Främmande rekord/subtyp eller avkortad SAB får precist fel, utan delvis kropp.
- **Ej testat:** varje ACIS-record/subtyp/schemaövergång och äldre kroppsmål utan
  filfacit; ange format ej fastställt där det gäller. Att SAT→SAB redan finns
  gör inte omvänd riktning eller äldre SAT-nedgradering verifierad.

### 8.6 — Färg, material, visuella stilar, render och ljus (våg 2)

- **Mål:** alla färgförekomster, DBCOLOR/färgbok, transparens, gradient,
  lineweight/plot style, material, visual style, bakgrund, ljus/sol, render-
  inställningar/MentalRay/RapidRT, LIGHTLIST/SUNSTUDY och previews/thumbnails.
  Native/äldre former gäller även header, lager, tabeller, vyer, viewport,
  block/attribut och fas 9:s cell-/stil-/sektionsfält.
- **Filer:** `src/conversion_appearance.cpp`, `conversion_styles.cpp`,
  `conversion_render.cpp`, `test/test_conversion_appearance.cpp`,
  `test/test_conversion_render.cpp`. H/W/E/O tunna codec-/visitoranrop; C/T registrering.
- **Beslut:** B123/B125/B127/B128/B130. En gemensam färg-/egenskapsvisitor ger
  reglerna till andra familjer, som använder samma färdiga grundgräns.
  ACI-val provas mot AutoCAD, inklusive svår RGB, BYLAYER/BYBLOCK och negativ
  lager-ACI. Rensa bara förbrukade material-/stilreferenser; delade resurser lever kvar.
  PROXYGRAPHICS och bortfall av ljus/renderinställningar följer målets facit.
- **AutoCAD-underlag/facit:** F8.6, `07_layers_styles`, `13_objects`,
  `18_render_modeldoc`, `21_native_records`, `24_rapidrt_preset`, F9.6/F9.7/F9.13–15.
- **Testkod:** T; färger i nested data, transparens med olika arv, gradient
  före 2004/R12, materialkanaler/matriser, stilversionernas fält, borttagen/delad
  resurs, ljuslistor/tider och thumbnaildata. Inga dubbla standardmaterial/stilar.
- **Ej testat:** äldre render-/procedurmaterial-/SUNSTUDY-grenar utan original,
  samt grafisk effekt som inte kan bedömas headless; serialiserat fält är inte renderprov.

### 8.7 — Dokument, referenser, sektioner och GEODATA (våg 2)

- **Mål:** ACAD_TABLE/TABLESTYLE/TABLECONTENT/CELLSTYLEMAP/TABLEGEOMETRY,
  FIELD/FIELDLIST, DATATABLE och övriga dokumentdata; SORTENTSTABLE, alla index/
  filter/IDBUFFER; IMAGE/WIPEOUT och variabler/definitioner/reactors; PDF/DWF/DGN-
  underlägg, coordination och xref-paneler; SECTION-familjen och ModelDoc-stilar;
  GEODATA v1/v2/v3 i båda riktningar. R12:s visuella ersättningar ingår.
- **Filer:** `src/conversion_document.cpp`, `conversion_references.cpp`,
  `conversion_geodata.cpp`, `test/test_conversion_document.cpp`,
  `test/test_conversion_references_geodata.cpp`. H/W/E/O tunna kopplingar; C/T registrering.
- **Beslut:** B123–B125/B128/B130. Återanvänd fas 9:s typade äldre tabellformer,
  checksum/layout/cache/*T-block och sektion-/ModelDoc-grafik. Kopiera inte nya
  kompanjoner till fel version. Fältvärde/cache/status och externa resurser hålls
  skilda från en evaluator. GEODATA:s koordinatsystem/WKT, mesh, norriktning och
  skalfaktorer konverteras semantiskt; ett ändrat versionsnummer räcker inte.
  IMAGE:s insideclip och hatch fitdata får målformen i samverkan med 8.4.
- **AutoCAD-underlag/facit:** F8.7, `09_images`, `12_tables`, `13_objects`,
  `19_coordination`–`23_coordination`, fas 9:s F9.3–5/F9.8/F9.16/F9.23/F9.26–27
  och riktiga GEODATA-original. Ingen påhittad WKT eller NWD/NWC-resurs.
- **Testkod:** T; tabellcell med fält/block/formel/merge, ändrad listlängd,
  klippning/transform och relativ/Unicode-resursväg, delad definition, borttaget
  mål, ritordningspar/id, koordinatkontrollpunkter och äldre GEODATA-förlust.
  Kontrollera målobjekt och ersättningsblock, inte bara bibehållen bildram.
- **Ej testat:** plugin-/GUI-/resursberoende underlägg/coordination/ModelDoc,
  GEODATA-system eller dokumentdatagren utan facit anges exakt; F9-luckor ärvs öppet.

### 8.8 — Dynamiska block, historik och associativa grafer (våg 2)

- **Mål:** alla fas 9:s BLOCK*-parametrar/actions/grips/constraints och
  ACAD_EVALUATION_GRAPH/EvalExpr; samtliga ACDBASSOC-baser/action/dependency/
  parameter/body/array-/mått-/ytoperationer, båda persistenta subentitetsmanagers
  och ACSH/HISTORY-graf. Äldre round-trip/proxy eller statisk anonymrepresentation
  ska bli vad AutoCAD sparar, med rätt koppling till faktiskt innehåll.
- **Filer:** `src/conversion_dynamic.cpp`, `conversion_history.cpp`,
  `conversion_assoc.cpp`, `test/test_conversion_dynamic.cpp`,
  `test/test_conversion_history_assoc.cpp`. H/W/E/O familjedispatch; C/T registrering.
- **Beslut:** B123/B125/B126/B128–B130. Bevara interna nod-/subentitets-id
  separat från handles. Hantera 2013:s flytt av assoc-parametrar och grafversioner.
  Äldre statiskt resultat tas från källans verkliga aktiva representation/cache
  enligt facit; ingen allmän dynamisk blocklösare eller ACIS-motor uppfinns.
  Målets förlust av constraints/historik/återredigerbarhet får explicit varning.
- **AutoCAD-underlag/facit:** F8.8, `10_solids`, `11_surfaces`,
  `16_dynamic_blocks`, `17_associative` och fas 9:s F9.9–12/F9.19–20.
  Samtliga nollprovade typer från fas 9 kvarstår som egna rader, inte som ett grupp-pass.
- **Testkod:** T; visibility/actions kombinerade, skalad/speglad instans,
  flera arrays/networks, delad geometri/material och borttaget mål. Kontrollera
  upp-/nedgradering av äldre hjälpgraf, räknare/bakåtlänkar och vald representation;
  ändrad historik får inte oavsiktligt ändra solidens B-rep eller extents.
- **Ej testat:** varje operation/action/parameter/constraint och cachegren utan
  AutoCAD-original; en registrerad manager eller en statisk bild bevisar inte hela grafen.

### 8.9 — Proxyer, opaque data och alla rårester (våg 3)

- **Mål:** OLEFRAME/OLE2FRAME, ACAD_PROXY_ENTITY/OBJECT, VBA_PROJECT,
  OBJECT_PTR, alla elva Map-typer, TCOPTIONALDATA/TCPOINTENTITY och samtliga 28
  ZWM-typer från fas 9. Slutbehandla Unknown och okända delfält/Embedded Object,
  102/XDATA, råtabeller, råsektioner och oanvända klasser i **hela** ritningen.
- **Filer:** `src/conversion_proxy.cpp`, `conversion_extensions.cpp`,
  `conversion_residual.cpp`, `test/test_conversion_proxy_extensions.cpp`,
  `test/test_conversion_residual.cpp`. Återanvänd fas 9:s proxy-/OLE-/utökningscodecs;
  H/W/E/O tunna registreringar; C/T registrering.
- **Beslut:** B120/B123/B128–B130/B132. Varje rest får explicit behåll-/konvertera-/
  proxy-/utelämna-/felregel med belägg, även vid uppgradering. Remappa
  proxyklassindex `500 + position` och typade referenser; bevara opaque nyttolast
  bara när intern versions-/referensstruktur är kompatibel. Om den inte säkert
  kan skrivas om ges precist fel. OLE-server/VBA körs inte för att läsa bytes.
- **AutoCAD-underlag/facit:** F8.9, F9.1/F9.2/F9.22/F9.24/F9.25/F9.27,
  historiska proxyoriginal och tillgängliga pluginoriginal sparade av AutoCAD.
  Ett redan råbevarat okänt fält är inte automatiskt portabelt till annat mål.
- **Testkod:** T; två proxyströmmar/bitlängder, klassbyte/klassordning,
  PROXYGRAPHICS=0, tom/icke-tom payload, interna/externa pekare, icke-ASCII
  råsträngar över kodsidesgräns, okänd delversion och ZWM:s intervallkonflikter.
  Kontrollera varning/fel och oförändrad källa även när felet hittas sent.
- **Ej testat:** AutoCAD:s utfall för okänd tredjepartsklass/opaque del som saknar
  möjligt original/plugin; säkert konverteringsfel redovisas som begränsning,
  aldrig som belagt AutoCAD-utelämnande eller färdig typad konvertering.

### 8.10 — Relationsslutning, målvalidering och hela matrisen (våg 3)

- **Mål:** slutför hela ritningens konverteringsgraf från familjernas färdiga
  ändringsbeskrivningar. Alla 72 par och båda käll-/målformat, korsfamiljsfall,
  återläsning, upprepade exporter och kedjor. Ingen typ, rårest eller gammal
  versionsspärr får falla mellan familjerna. Alla resultat går via samma I/O-väg.
- **Filer:** `src/conversion_relations.cpp`, `conversion_finalize.cpp`,
  `test/test_conversion_matrix.cpp`, `test/test_conversion_relations.cpp`,
  `test/test_conversion_failures.cpp`, `tools/conversion_batch.py`.
  Grundens `conversion.cpp`/I/O får sekventiell slutkoppling vid integration;
  H/W/E/O endast tunn koppling; C/T registrering.
- **Beslut:** B118–B123/B129–B132. Bestäm remappning för en-till-flera/sammanslagna
  mål per relationsslag, inte genom att låta alla peka på första ersättningen.
  Slut handles/ägare/CLASS/räknare och målbegränsningar valideras efter konvertering
  på kopian och före filöppning. B12-vägen får inte återinföras som reservlösning.
- **AutoCAD-underlag/facit:** F8.10 och alla F8.1–9, fas 9:s blandade relationsritning,
  samtliga doc/acad_*-original och belagda historiska källor. Förbered en extern
  slutbatch; kör inga externa kontrolläsare i detta agentsteg.
- **Testkod:** T; kombinerad tabell/fält, dynamiskt block, assoc-yta, solid/historik,
  context/scale, underlägg, hatch/image, stilar och okänd rest. Borttagning och
  delad resurs åt båda håll; kontrollera också helt legitimt utelämnat OBJECTS i R12.
  Matrisen måste upptäcka saknade källversioner/familjer/format och orimliga skips.
- **Ej testat:** kvarvarande saknade orakel markeras per konkret matrisruta;
  en körbar matrisgenerator får inte rapportera alla genererade filer som verifierade.

## Täckning av fas 9 och tidigare versionsrester

Alla konkreta typer/fält från fas 9:s register, gamla bilagor A–C och F9.1–27
förs över i 8.0; denna gruppering tar inte bort någon post. Delat ansvar betyder
disjunkta delar av modellen, sammanfogade genom grundens avtal.

| Underlag | Ägare i fas 8 |
|---|---|
| 9.0:s typ-/relations-/versionsregister | 8.0 och 8.10, alla matrisrader |
| 9.1/9.5 tabeller och kompanjoner | 8.7; deras färger 8.6 |
| 9.2 befintliga småtyper, BLOCK-färger och vertex-id | 8.2/8.4/8.6/8.7 enligt fältslag |
| 9.3 OLE/proxy | 8.9 |
| 9.4 ritordning/index/filter | 8.7; slutrelationer 8.10 |
| 9.6 underlägg/coordination/xref och 9.8 fält/dokumentdata | 8.7 |
| 9.7 material/stilar och 9.11 render/ljus/sol | 8.6 |
| 9.9/9.13 dynamiska block, 9.10/9.15 ACDBASSOC | 8.8 |
| 9.12 sektioner/ModelDoc | 8.7 |
| 9.14 ACSH/historik | Graf 8.8; kroppens målström/grafik 8.5 |
| 9.16 NURBSURFACE/2004-isolinjer | 8.5 |
| 9.17 kontexter/specialmått/DIMASSOC | Representation 8.3, assoc-graf 8.8 |
| 9.18 Map/TC/ZWM | 8.9 |
| B11/B19/B23/B35/B41/B46/B50/B54/B60–64/B74/B76/B78/B80–82/B94–98 | 8.0 inventerar kvarvarande spärrar; 8.1–9 ersätter med målregler, 8.10 kontrollerar full täckning |
| 2013-upright/MLeader/style/GUID, 2018-MTEXT/MLATT/viewstyle | 8.2/8.3/8.5/8.6/8.7 återanvänder typade äldre former; ingen parallell rå kopia |

## Slutsteg 8.S — Granskning, sluttest, rättning och utgåva

Ett sammanhållet avslut efter våg 3, i följande ordning:

1. **En Codex-granskning.** Granska koden och att **hela planen är genomförd**:
   B118–B132, F8.1–10, alla tio familjesteg, fas 9:s konkreta register/A/B/C/F9,
   72 par × båda käll-/målformat, B12-diagonalerna och återstående versionsspärrar.
   Kontrollera exportkopians värdesemantik, ursprung, typed-only målskrivning,
   inga smygmallar, opaque policy, ACIS-riktningarna, äldre ersättningsgeometri,
   N/A2000, namn/kodning, grafrelationer och minnesbudget. Samla fynd inför rättningssvepet.
2. **Ett samlat slutteststeg.** Bygg med befintlig konfiguration och kör hela sviten:

   ```bash
   cmake --build build
   ./build/test/hdxf_tests
   ```

   Kör befintlig ACIS-byggarsvit om den ingår i konfigurationen. Rapportera verkliga
   pass/fail/skips, inga gamla baslinjesiffror som aktuella. Kör inventeringen över
   hela tillgängliga korpusen med deduplicering/proveniens och kontrollera att alla
   typer/fält, rårester, familjer och matrisrutor har utfall.

   En samlad batch omfattar alla tillgängliga AutoCAD-original i `build/test-data`,
   `doc/acad_*_dxf.dxf`, `doc/facit`, fas 9:s original och nya F8-par. Generera HDxf:s
   konverteringar för alla riktade mål, käll-/målformat och relevanta ändrings-/
   borttagnings-/kedjefall, plus samma-version/B12-regressioner. Negativa fall ska
   ge förväntat namngivet fel; saknat AutoCAD-original står separat som ej testat.

   **Både AutoCAD 2025:s accoreconsole och ODA läser varje original och varje
   HDxf-resultat direkt**, utan mellanliggande ODA-konvertering. Full öppnings-/DXFIN-
   sekvens, REGEN, innehållsutfrågning och AUDIT/loggkontroll; faktisk ACADVER och
   målformat kontrolleras. ODA File Converter används som kontrolläsare och skriver
   målversionens DXF/DWG där verktyget stöder det. Dess utdata blir aldrig facit.
   Verktygsbegränsningar och fel redan i original skiljs från HDxf-introducerade fel.

   Jämför varje belagt par med AutoCAD:s SAVEAS-resultat av samma källa enligt T,
   inklusive bortfall/varningar, typersättning, objekt-/blockinnehåll, CLASS/proxyindex,
   ägare/reactors/dictionaries, räknare, texter och externa resurser. B7:s struktur-/
   relationskontroll samlas i batchen: inga oförklarade `.err`, auditfel/rättningar
   eller nya brutna länkar; utredda verktygsvarningar redovisas per fil.

   Mät isolerade extents/textlådor och MASSPROP/AREA för bibehållna kroppar mot samma
   källas AutoCAD-resultat; R12:s grafiska kroppsersättning bedöms som grafik,
   inte som fortsatt solid. Jämför proxykommandon, ersättningssegment, *D/*T/anonyma
   block, kontext och modell/papper. En gammal EXTMIN/MAX eller tom fellogg är inte
   innehållsbevis. Headless-kontroll beskrivs aldrig som visuell GUI-granskning.

   Spara kommandon, loggar, filhashar, körinställningar och per-filutfall i avtalad
   scratchpad. Rapportera särskilt de 16 historiska målparen och alla ej testat-rutor;
   288 genererade baskombinationer är inte 288 AutoCAD-belägg.
3. **Samordnarens rättningssvep.** Rätta granskningsfynd, kod-/integrationsfel,
   matrisluckor som kan fyllas och dokumentationsavvikelser. Kör nödvändiga
   riktade omtester/externa omkontroller efter rättning; ingen rutinmässig ny
   batterikedja. Verkliga fel får inte döljas som ej testat. Omöjligt facit får
   kvarstå enligt B132, med exakt gräns för implementerat och verifierat stöd.
4. **Dokumentation.** Uppdatera README.md, CLAUDE.md, TODO.md, API-kommentarer,
   test/data/README.md, manifest, facit coverage/run_all och denna plans utfall.
   Beskriv API-flöde/exportkopians semantik, alla målformer, förluster/varningar,
   namngivna fel, N/A2000-val, ACIS-schema och referensavvikelser. Varje undantag
   får typ/fält/gren/källa/mål/format, F8/F9-id, skäl och faktiskt sök-/körutfall.
   Inget flyttas till senare fas och inget obelagt stöd kallas AutoCAD-verifierat.
5. **Utgåva 1.10.0.** Uppdatera CMakeLists.txt, include/hdxf.hpp, CLAUDE.md och
   README.md till 1.10.0, gör nödvändigt versions-/paketbygge och följ projektets
   commit/push/taggrutin på main med **v1.10.0**. Ta bara med uppdragets filer.
   Leveransunderlaget innehåller verklig matris och alla kvarstående undantag.
   Ingen 2.0.0 här; planuppdraget utför ingen implementation, release eller commit.

## Klart när

1. Alla nio skrivversioner är mål från varje annan läst version genom fil-/minnes-
   I/O, med 72 riktade par, båda käll-/målformaten och redovisade undantag.
   Alla fas 9-typer/fält och rårester har regel och utfall; ingen senare restfas skapas.
2. Uppgradering bygger rätt struktur och återför bevarade äldre former.
   Nedgradering ger AutoCAD:s målrepresentation/förlust/fel enligt tillgängligt
   belägg, även R12-geometri, ACIS och R13/R14:s N/A2000. Inga smygmallar över versioner.
3. B12 i samma version, B19, källdata/exportkopian, namngivna fel före mutation,
   målvalidering och minnesbudget håller i körda tester. Live-källans innehåll
   förstörs inte när mål väljs eller exporteras.
4. Agentstegens slutkörningar, vågintegrationerna, en Codex-granskning inklusive
   plantäckning, ett samlat AutoCAD/ODA-sluttest och rättningssvepet har faktiska resultat.
5. Kvarvarande omöjliga facitluckor är **ej testat** i matris, stegutfall,
   dokumentation och leveransunderlag; okänd grammatik/ej verifierad generering
   anges uttryckligen. De räknas aldrig som passerade eller som belagda versionsförbud.
6. Dokumentation och **1.10.0** är levererade enligt projektets rutin. Denna
   planfil ensam innebär ingen implementation, byggning, testkörning, AutoCAD-
   verifiering, commit, push eller tagg.
