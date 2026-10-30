# Facittäckning och kvarvarande postkontroll

Detta är en karta över skriptens avsedda provgeometri, inte en implementationstest.
Skript 01–21 är körda till slut i AutoCAD 2025 (2026-10-25). Skript 22–25 provades
2026-10-26; utfall och begränsningar står sist under "Kan inte skriptas".
Skripten begär sju versioner i två format. Native form i ett
äldre format, proxy, förlust och avvisad sparning är olika utfall.

## Fas 6: Facit som Erik skapar i AutoCAD

| Planpost | Skript | Postkontroll efter AutoCAD-körningen |
|---|---|---|
| 1 Koordinationsmodell | 19, 23 | 23 kontrollerar NWCOUT och hoppar över när exportören saknas. Inga koordinationsfiler skapades 2026-10-26. 19 kräver riktig NWD/NWC. Kontrollera faktisk klass, definition, delning, matris, enhetsfaktor och proxygrafik när en resurs finns. |
| 2 IBL | 18 | Native IBL-objekt, medföljande HDR, 27,5° rotation och bakgrund på/av. Kontrollera båda 290 och vyns länk. |
| 3 MTEXT-kolumner | 03, 25 | 25 verifierat: två statiska kolumner och två roterade dynamiska prov med fyra kolumner och auto-höjd. 2018: Embedded Object 71/72/73/74 = 1/2/0/0, 2/4/1/0, 2/4/1/0; 2000–2013: motsvarande XDATA 75/76/79/78. Omvänd ordning, olika höjder per kolumn och ram kvarstår. Tidigare beskrivning av kommandots dynamiska prov som manuell höjd var fel. |
| 4 Flerradiga attribut | 04, 14 | Instanser, rotation/skala, låst position och annotationsskalor. Kolumner inne i attributet och sekundärattribut är särskilda kvarvarande grenar; att vanlig MTEXT har kolumner bevisar dem inte. |
| 5 Vystilar | 18, 22 | 22 verifierat: mallens Imperial24-stilar med egna visningsnamn och flaggor 3/5; native 300/90 i 2018, ACAD_XREC_ROUNDTRIP med DISPLAYNAME/FLAGS i 2000–2013. Nya stilobjekt och härledda vyer skapades inte. |
| 6 Tom 2018 | 01 | acad.dwt, båda format. Inte acadiso.dwt; mallarna får inte blandas som samma grundritning. |
| 7 Ytor/HELIX/MESH/GEODATA/ACIS | 08, 10, 11; GUI för GEODATA | Yttyper, smoothing, block/papper, SAT/SAB. BODY rapporteras bara om ACISIN faktiskt skapat BODY. GEODATA:s verkliga koordinatsystem och mesh kräver komplettering. |
| 8 Flera toppkroppar | 10 | Disconnected-lump-solid. Undersök SAB:s body-poster; ett objekt med flera lumps är inte samma sak som flera topp-body. |

## Fas 5: kontrollpunkter utan orakel

10–11 skapar analytiska och splinebaserade kroppar med extrude/revolve/sweep/loft,
boolean och öppna ytor. AutoCAD själv skapar SAT före 2013 och SAB i 2013/2018.
Inga SAT-huvuden, token eller modellerversioner ändras i skripten.

Planens `subshell`, `rot_spl_sur`, `surf_int_cur`, `par_int_cur`, `closed`,
`summary`/`none`, toleranta poster, ytterligare ATTRIB_CUSTOM-definitioner och
pcurve-typer kan inte väljas som namngivna DXF-/ACIS-deltyper i ett ritkommando.
Provgeometri kan framställa en gren, men den måste hittas i AutoCAD:s sparade data.
Ett modernt splineprov är inte automatiskt orakel för alla gamla SAT-deltyper.
De återstående specifika grenarna behöver original med sådana poster eller
modellkärnans API; ingen av dem färdigmarkeras här.

## Fas 7: F7.1–F7.9

AutoCAD 2025 sparar inte R13/R14 DXF. Nedanstående beskriver ritningsinnehållet
som skripten täcker i de sju beställda versionerna. AC1012/AC1014-rutorna behöver
en AutoCAD-utgåva som faktiskt kan spara dem; nedsparning via ODA räknas inte.

| Facit-id | Skript |
|---|---|
| F7.1 Grund/header | 01, 02, 08 |
| F7.2 Tabeller/DIMSTYLE | 07; kontrollera skillnaden mellan nyare DIMLUNIT/DIMATFIT och äldre DIMUNIT/DIMFIT i riktiga R13/R14-filer. |
| F7.3 Block/papper/mått | 04, 05, 08, 14 |
| F7.4 Entitetsgrammatik | 02, 03, 05, 13 |
| F7.5 HATCH | 06, inklusive gränsändring/borttagning |
| F7.6 IMAGE | 09; absoluta ASCII-/Unicode-sökvägar ingår. Relativ sökväg är en separat komplettering. |
| F7.7 ACIS | 10–11; BODY och särskilda äldre SAT-versioner måste konstateras. |
| F7.8 Objekt/round-trip | 04, 07, 08, 13, 18, 21 |
| F7.9 Text/namn | 03, 04, 15; native dos932 behöver äldre/lokaliserad källa. |

## Fas 9: F9.1–F9.27

| Facit-id | Skript eller faktisk begränsning | Vad skripten inte ensamma bevisar |
|---|---|---|
| F9.1 OLE | Handarbete/originalpayload, se README | Länkad/inbäddad/statisk ram, giltiga bytes och gammal OLEFRAME. |
| F9.2 Proxy | Rätt plugin/original krävs | Giltig klasspayload och pointer-/grafikgrenar; tomma proxyer skapas inte. |
| F9.3 Underlägg | 09, 20 | PDF/DWF/DGN är riktiga underlägg. Alla insideclip-/block-/papper-/normalvariationer är inte samma fall. |
| F9.4 Index/filter | 13, 21 | XCLIP ger native SPATIAL_FILTER; LAYER_FILTER och IDBUFFER skapas med ägare. INDEXCTL + DWG-save måste följas av kontroll av de index AutoCAD faktiskt skriver. |
| F9.5 TABLE | 12 | Text, int/double, formel, block, merge, färg, kant, flera cellinnehåll och custom-data. Alla AcValue-grenar kräver fortfarande egna typvärden. |
| F9.6 MATERIAL | 13, 21 | Sex mappar och matriser, applicerat material och opacitetsändring. Alla procedurala plugin-/moderna tillgångsgrenar följer inte automatiskt av legacy-mapparna. |
| F9.7 VISUALSTYLE | 13, 18 | Native stilar och vylänkar; full/face/edge-referenser 348/349/350 kräver särskild postkontroll. |
| F9.8 FIELD/DIMASSOC | 12, 17 | Objektfält, uttryck och endpoint-association. Alla barn/AcValue/status/cache-grenar kräver separata prov. |
| F9.9 Dynamiska block | 16; lookup-/propertytabeller i GUI | Linear/stretch, rotate, flip, point/move, visibility, alignment/base och ändrad instans. Planens fulla kombinations-/constraint-/lookup-matris täcks inte av ett enda dynamiskt block. |
| F9.10 Solidhistorik | 10 | SOLIDHIST är på; primitiv-/boolean-/extrude/revolve/sweep/loft-grenar samt FILLETEDGE/CHAMFEREDGE/BREP ingår. Kontrollera de faktiska ACSH-posterna, inte bara inställningen. |
| F9.11 Persistenta subentiteter | 10, 11, 17 | Riktiga modelloperationer/associationer kan skapa managers; båda klasserna och tabellgrenarna måste konstateras. |
| F9.12 ACDBASSOC | 11, 17 | Ytors källkurvor, patch/network/offset/trim/blend/extend, constraints och tre arraytyper. Varje nollprovad action/parameterklass i bilaga C kräver faktiskt prov; förekomst av en network bevisar inte alla. |
| F9.13 Render | 18, 24 | 24 verifierat: egen RapidRT-preset, AutoCAD-namn High - Copy 0, predefined=0 och rendernivå 9. Äldre MentalRay, RENDERENTRY och samtliga destination/save-to-file-grenar behöver original/API-prov. |
| F9.14 LIGHTLIST | 13, 08 | Två LIGHT, SUN och viewport; kontrollera verklig LIGHTLIST och dess 5-referenser. |
| F9.15 SUNSTUDY | Original/API-prov | Ingen fastställd dialogfri skapandekedja för kalender/tim/sheet-set-fallen. `290`-motsägelsen löses inte genom påhittad indata. |
| F9.16 Sektioner | 13 | Två sektioner, jogg, höjder, ändring; samtliga nested geometry-/backvertex-/source-destination-grenar behöver kompletterande modellfall. |
| F9.17 NURBSURFACE | 11 | CONVTONURBS skapar native yta; faktisk 0-post och geometri måste kontrolleras. |
| F9.18 Ytornas isolinjer | 11 | ISOLINES=8, yttyper och trimmad yta med hål. Periodiska dubbelsömmar och rationella/periodiska flerknutytor är skilda geometrifall som grundytorna inte bevisar. |
| F9.19 Annotationskontext | 14 | Flera skalor och borttagning. Räkna faktiska kontextposter för varje typ; blockets kontext är inte automatiskt varje ATTRIB:s egen kontext. |
| F9.20 Arc/large radial dimension | 05, 14 | Native typer i modell, block och papper. Kontrollera symbolplacering och dimensionsblock i varje rum. |
| F9.21 Vertex-id | 17 | Geometriska constraints ger AutoCAD möjlighet att skriva 91. Flagga 1024 sätts inte godtyckligt. |
| F9.22 OBJECT_PTR/VBA | ASE-/VBA-original/plugin | Giltig extern struktur/projektpayload kan inte ersättas av ett XRECORD med liknande namn. |
| F9.23 Coordination/xref | 19, 23; resurs/exportör saknas | Ingen ny koordinationsfacit. Riktig modell och delad definition; XREFPANELOBJECT/EXACXREFPANELOBJECT och host/load-varianter måste konstateras/kompletteras från rätt miljö. |
| F9.24 Map | AutoCAD Map/original krävs | Icke-tomma manager/library/style/resource-grenar. |
| F9.25 TC/ZWM | Ursprungligt plugin/original krävs | Alla 28 ZWM-typer, TC-payload och deras faktiska listgrenar. |
| F9.26 ModelDoc-stilar | 18, 22 | Skilda displaynamn/flaggor verifierade i 22. Härledda pilreferenser, färger och hatch-vinkellistor kräver ytterligare stilvariationer. |
| F9.27 DATATABLE | 21 | Två rader, sju typer inkl. objektref. Alla ägar-/pekarslag och de övriga producentberoende kodformerna är separata grenar. |

## Källor för kommandon och native indata

- [Autodesk: MTEXT DXF, kolumner och masker](https://help.autodesk.com/cloudhelp/2020/ENU/AutoCAD-DXF/files/GUID-5E5DB93B-F8D3-4433-ADF7-E92E250D2BAB.htm)
- [Autodesk: MTextBoundaryWidth/UpdateMTextAttribute](https://help.autodesk.com/cloudhelp/2024/ENU/AutoCAD-ActiveX-Reference/files/GUID-2B799069-AAC8-4D8F-B30A-805814DE47D4.htm)
- [Autodesk: AddSection](https://help.autodesk.com/cloudhelp/2017/RUS/AutoCAD-ActiveX/files/GUID-7349D699-A696-48DF-83D1-315FF944649E.htm)
- [Autodesk: MATERIAL DXF, sex mappar och matriser](https://help.autodesk.com/cloudhelp/2024/ENU/AutoCAD-DXF/files/GUID-E540C5BB-E166-44FA-B36C-5C739878B272.htm)
- [Autodesk: RAPIDRTRENDERENVIRONMENT](https://help.autodesk.com/cloudhelp/2020/ENU/AutoCAD-DXF/files/GUID-E0D55EE1-54C2-4C0E-A458-919E2E677595.htm)
- [Autodesk: DATATABLE](https://help.autodesk.com/cloudhelp/2020/ENU/AutoCAD-DXF/files/GUID-D09D0650-B926-40DD-A2F2-4FD5BDDFC330.htm)
- [Autodesk: AcDbDataCell CellType](https://help.autodesk.com/cloudhelp/2018/ENU/OARXMAC-RefGuide/files/OREFMAC-AcDbDataCell__CellType.html)
- [Autodesk: LAYER_FILTER](https://help.autodesk.com/cloudhelp/2021/CHS/AutoCAD-DXF/files/GUID-3B44DCFD-FA96-482B-8468-37B3C5B5F289.htm)
- [Autodesk: IDBUFFER](https://help.autodesk.com/cloudhelp/2020/ENU/AutoCAD-DXF/files/GUID-7A243F2B-72D8-4C48-A29A-3F251B86D03F.htm)
- [Autodesk: -COORDINATIONMODELATTACH](https://help.autodesk.com/cloudhelp/2026/ENU/AutoCAD-Core/files/GUID-FE727B97-4918-480C-9C90-DF148371BE47.htm)

Där 2025-sökningen inte exponerade en läsbar sida har närliggande Autodesk-
utgåvors API-/DXF-sidor använts. Det är underlag för skripten, inte en utförd
kompatibilitetskontroll i Eriks AutoCAD 2025-installation.

## Kan inte skriptas (konstaterat vid körning i AutoCAD 2025)

Här skiljs dialogberoende grenar från sådant som bara är blockerat i den lokala
installationen. Auto-höjd och en egen RapidRT-preset gick att skripta vid omprovet
2026-10-26; de är inte längre begränsningar. Kvarvarande grenar listas nedan.

| Skript | Borttaget / ändrat | Varför |
|---|---|---|
| 03, 25 | Omvänd kolumnordning, olika höjd per kolumn och ram (90 bit 16) kvarstår. Dynamisk auto-höjd är verifierad i 25. | -MTEXT → Columns → Dynamic → bredd 35 → mellanrum 5 → höjd 30 skriver auto-höjd=1, både med MTEXTCOLUMN=1 och =2. Inget kommandoradsalternativ för omvänt flöde eller skilda kolumnhöjder finns i den dokumenterade prompten. Höjd 0 avvisades med "Value must be positive and nonzero"; det provet är inte med i skriptet. |
| 06, 08, 17 | Kommandona använder nu punktplock, SPLMETHOD och `_Endpoint` i stället för `_Method`, `_Fit` och entitetsnamn. | `SPLINE _Method` finns inte i 2025:s promptsekvens; GCHORIZONTAL/DCLINEAR tar inte ett objekt via ename (Invalid selection). |
| 10 | Primitiver, booleska operationer, FILLETEDGE och CHAMFEREDGE görs nu med kommandon (inte COM), annars får de ingen solidhistorik. | COM `AddBox`/`Boolean` ger ingen ACSH-historik; FILLETEDGE kräver punktplock på kanten i en 3D-vy. |
| 16 | Parametrar och åtgärder väljs med punktplock i Block Editor; uppslagstabeller/egenskapstabeller och constraint-kombinationer ingår inte. | `ssget` hittar parameterobjekten men `entget` ger nil, och ename går inte att ge till BACTION. |
| 18 | Länken vy → IBL-bakgrund (VIEW 332) och nya DETAIL-/SECTION-stilobjekt kvarstår. Egen RapidRT-preset finns nu i 24; den tidigare begränsningen var för bred. | RENDERPRESETS/RENDERENVIRONMENT/IBL är paletter; -VIEW kan inte sätta bakgrund. 24 behöver ingen ny ActiveX-klass: ändring av en standardpresets systemvariabler skapar AutoCAD:s egen anpassade kopia. |
| 22 | Nya detalj-/snittstilobjekt och VIEWBASE → VIEWSECTION/VIEWDETAIL-kedjan skapades inte. 22 använder mallens befintliga stilar enligt 18:s fungerande entmod-mönster. | VIEWBASE gav "Inventor Server failed to load" efter BOX i en ny acad.dwt-ritning. Det är en lokal installationsbegränsning, inte bevis att VIEWBASE generellt saknar skriptstöd. De dokumenterade stilkommandona heter VIEWDETAILSTYLE och VIEWSECTIONSTYLE och öppnar stilhanterarnas dialoger. Inga interna bibliotek eller okända ActiveX-objekt undersöktes. |
| 23 | Inga NWC- eller DXF-filer skapades. Skriptet når KLAR med uttryckligt HOPPAR OVER. Export/attach-grenen är skriven men inte körverifierad. | getcname NWCOUT gav nil och NWCOUT gav "Unknown command". Ingen coordination.nwc/.nwd finns i facitmappen. Navisworks-exportören måste finnas för att skapa en egen NWC med den beställda metoden; inga komponenter installerades. |
| 24 | Egen RapidRT-preset är skapad och verifierad. Ett eget valt namn, exempelvis HDXF_RAPIDRT, ingår inte. | -RENDERPRESETS väljer en preset. RENDERTARGET=1, RENDERLEVEL=9, RENDERTIME=3 och RENDERLIGHTCALC=1 skapar den anpassade kopian High - Copy 0. Namnet tilldelas av AutoCAD; namnfältet finns i Render Presets Manager. |
| 19, 20 | Inga filer skrivs utan `coordination.nwd`/`.nwc` respektive `source.dwf` + `source.dgn` (skriptet skriver HOPPAR OVER och avslutar via skip.scr). | Externa resurser saknas; Erik lägger dem i doc/facit och kör om. |
| 09 | R12-filerna är tomma på bilder, underlägg och wipeout. | R12 har inte typerna; AutoCAD tappar dem vid DXF-sparning. |
| 11 | BODY-provet: ACISIN gav ytor, inte BODY. | AutoCAD:s importer; ingen BODY-facit hävdas. |
| 13 | LAYER_FILTER, IDBUFFER, LIGHTLIST: se 21 för de två första; LIGHTLIST skapas inte av skriptet. | Måste konstateras i AutoCAD:s egna utdata. |
| 05, 12, 14, 15 | Inget borttaget. | |

### Körning och filkontroll 2026-10-26, AutoCAD 2025 (25.0s)

Varje skript kördes med SCRIPT i en separat ny ritning skapad från den installerade
`acad.dwt`. 22, 24 och 25 nådde sina respektive KLAR och skrev vardera 14 filer,
totalt **42 nya DXF**. 23 nådde sitt överhoppningsslut och skrev **0 filer**.
Kommandologgarna för dessa filkörningar saknar LISP-fel. De tidigare interaktiva
promptproven innehöll avbrutna kommandon och räknas inte som lyckade filkörningar.

Alla 42 filer kontrollerades med en tillfällig fristående läsare: `$ACADVER` är
AC1009/AC1015/AC1018/AC1021/AC1024/AC1027/AC1032 enligt filnamnet, och samtliga
21 binära filer börjar med `AutoCAD Binary DXF`. Gruppkoder lästes i båda formaten;
kontrollen omfattade faktiska objektposter och följande fält, inte bara klassnamn:

- **22, modeldoc_views:** en ACDBDETAILVIEWSTYLE och en ACDBSECTIONVIEWSTYLE i
  2000–2018. Visningsnamnen är `HDxf detail display name` och
  `HDxf section display name`, flaggor 3 respektive 5. I 2000–2013 följer kontrollen
  stilens 360-länk till ACAD_XREC_ROUNDTRIP och dess DISPLAYNAME/FLAGS.
- **24, rapidrt_preset:** RAPIDRTRENDERSETTINGS med namnet `High - Copy 0`,
  predefined=0 och rendernivå 9 i 2000–2018. I 2000–2010 finns en sådan post;
  2013/2018 har även den namngivna kopian i presetordboken.
- **25, mtext_columns:** tre MTEXT i 2018: statisk typ 1 med två kolumner samt två
  dynamiska av typ 2 med fyra kolumner, auto-höjd=1, omvänd ordning=0. I 2000–2013
  skriver AutoCAD tio länkade MTEXT med motsvarande kolumn-XDATA. R12 skriver 66 TEXT.
- **R12:** AutoCAD tappar ModelDoc-stilarna och RapidRT-objekten och spränger MTEXT.
  R12-filerna är facit för det utfallet och bevisar inte native stöd för de typerna.

Källor för de fungerande kommandovägarna:
[Autodesk 2025: -MTEXT](https://help.autodesk.com/cloudhelp/2025/ENG/AutoCAD-Core/files/GUID-F94BE932-DA31-437E-9610-27F46ACD5711.htm),
[MTEXTCOLUMN](https://help.autodesk.com/cloudhelp/2025/ENU/AutoCAD-Core/files/GUID-188C7F13-7046-436E-9F4E-6E05984456E8.htm),
[Render Presets Manager](https://help.autodesk.com/cloudhelp/2025/ENU/AutoCAD-Core/files/GUID-036EEDB2-C196-4967-961B-415E232EAAF5.htm),
[VIEWBASE](https://help.autodesk.com/cloudhelp/2025/ENU/AutoCAD-Core/files/GUID-E0F79E2F-8838-4470-B8D0-8626343A22D2.htm).

Inget HDxf-bygge, inget HDxf-rundturstest och ingen commit kördes i detta uppdrag.
