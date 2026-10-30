## AutoCAD-facit (skript)

Skripten i [facit/](facit/) körs i **AutoCAD 2025 för Windows**, med `SCRIPT`
eller genom att dra en `.scr` till ritningsfönstret. **Öppna en ny tom ritning
från `acad.dwt` med Ctrl+N före varje skript.** Skripten byter inte dokument.
[run_all.txt](facit/run_all.txt) innehåller exakt körordning, resurser och felhantering.

Varje tillstånd sparas som R12, 2000, 2004, 2007, 2010, 2013 och 2018, både ASCII
med precision 16 och binärt, med det provade `SAVEAS DXF _V`-mönstret. Namnen är
`acad_<ver>_dxf_<innehåll>.dxf` och `acad_<ver>_dxf_<innehåll>_bin.dxf`.
Tillstånd efter en ändring har ett beskrivande suffix. Det finns **21 nya skript
med 518 unika DXF-utdatavägar**, varav 42 hör till de resursberoende skripten 19–20.
Den befintliga `probe.scr` är orörd och ingår inte i antalet.

| Skript | Innehåll | DXF-filer inklusive före/efter |
|---|---|---:|
| [01_empty.scr](facit/01_empty.scr) | Tom `acad.dwt`, header, tabeller, rumsblock och standardobjekt. | 14 |
| [02_basic.scr](facit/02_basic.scr) | LINE, CIRCLE, ARC, POINT, TRACE, SOLID, 3DFACE; POLYLINE 2D/3D/mesh/polyface med bulge, bredd och vertex-XDATA; LWPOLYLINE, ELLIPSE, SPLINE fit/control med vikter/knutar, XLINE och RAY. SHAPE/BAT om den levererade `ltypeshp.shx` finns. | 14 |
| [03_text.scr](facit/03_text.scr) | TEXT:s 15 justeringar, SHX/TrueType, formaterad och lång MTEXT, mask/ram; statiska två/tre kolumner, dynamisk auto/manuell höjd, omvänd ordning och rotation; TOLERANCE. | 14 |
| [04_blocks_attributes.scr](facit/04_blocks_attributes.scr) | BLOCK, INSERT, MINSERT, nästling, ATTDEF/ATTRIB, konstant/osynlig/preset/låst position, flerradiga attribut, skilda instanser och transformeringar, XDATA och extension dictionary; ändrat attributvärde. | 28 |
| [05_dimensions.scr](facit/05_dimensions.scr) | Linjära, aligned, tvålinje-/trepunktsvinklar, radie, diameter, ordinata, båglängd och joggad radie; båg-/joggmått även i block/papper; LEADER/QLEADER, text-/block-MLEADER och egen MLEADERSTYLE. | 14 |
| [06_hatch.scr](facit/06_hatch.scr) | Mönster, solid, tvåfärgsgradient, öar, ellips-/splinegräns, associativ/oassocierad; flyttad och borttagen gräns. | 42 |
| [07_layers_styles.scr](facit/07_layers_styles.scr) | ACI/RGB, lineweight, lager-/entitetstransparens, plotflagga; komplex linjetyp, SHX/TrueType, DIMSTYLE-varianter, UCS, VIEW, VPORT och XDATA. Installerad färgbok används om den går att läsa. | 28 |
| [08_layouts.scr](facit/08_layouts.scr) | Två papperslayouter, geometri i modell/papper, viewports, polygonklippt viewport, viewportfryst lager, plotinställningar och ACIS i papper. | 14 |
| [09_images.scr](facit/09_images.scr) | IMAGE med delad definition, svensk resurssökväg, rektangel-/polygonklippning, WIPEOUT och PDF-underlägg från `doc/facit/underlay.pdf` (en egen PDF); borttagen bildinstans. | 28 |
| [10_solids.scr](facit/10_solids.scr) | BOX, SPHERE, CYLINDER, CONE, TORUS, WEDGE, PYRAMID, EXTRUDE, REVOLVE, SWEEP, LOFT, union/subtract, FILLETEDGE, CHAMFEREDGE, BREP, REGION, solid med separata lumps, ACIS i block/papper och solidhistorik; roterad kropp. | 28 |
| [11_surfaces.scr](facit/11_surfaces.scr) | Plan/extruderad/roterad/svept/loftad/NURBS-yta, källkurvor, patch/network/offset/trim med hål/blend/extend, HELIX och meshprimitiver med smoothing. Öppen yta exporteras/importeras med ACISOUT/ACISIN som BODY-prov. | 14 |
| [12_tables.scr](facit/12_tables.scr) | TABLE/TABLESTYLE, typade celler, blockcell, flera innehåll per cell, custom-data, sammanslagning, kant/färg, formel, FIELD i MTEXT/TABLE; ändrad cell och ändrat fältmål. | 42 |
| [13_objects.scr](facit/13_objects.scr) | GROUP, MLINE/MLINESTYLE, dictionary/XRECORD, extension dictionary, DRAWORDER, block-XCLIP, två ljus/SUN, tilldelat MATERIAL, VISUALSTYLE och två SECTIONOBJECT; ändrat medlemskap och sektion. | 42 |
| [14_annotative.scr](facit/14_annotative.scr) | TEXT, MTEXT, dimensionstyper, LEADER, MLEADER, TOLERANCE, HATCH och block med flerradigt attribut, i flera skalor; borttagen skala. | 28 |
| [15_unicode.scr](facit/15_unicode.scr) | Svenska, japanska, grekiska och kyrilliska tecken; Unicode-lager/block/attribut, lång MTEXT och supplementary Unicode med UTF-16-surrogatescapes. ASCII-skript; kräver `LISPSYS=1` eller `2` och omstart om värdet ändras. | 14 |
| [16_dynamic_blocks.scr](facit/16_dynamic_blocks.scr) | Block Editor, base/alignment/linear/rotation/flip/point/visibility, stretch/rotate/flip/move-actions och instanser med olika egenskaper; ändrad rotation. | 28 |
| [17_associative.scr](facit/17_associative.scr) | Geometriskt/dimensionellt tvång, DIMASSOC med riktiga endpoint-snaps, rektangulär/polär/path-array, constrained polyline för vertex-id; ändrad måttgeometri. | 28 |
| [18_render_modeldoc.scr](facit/18_render_modeldoc.scr) | RapidRT-preset, IBL/HDR med rotation och bakgrund på/av, namngiven vy; DETAIL/SECTION-vystilar med skilda displaynamn och flaggor. | 28 |
| [19_coordination.scr](facit/19_coordination.scr) | Riktig NWD/NWC, delad definition, placering/skala/rotation; kräver `coordination.nwd` eller `.nwc` i mappen. | 28 |
| [20_underlays.scr](facit/20_underlays.scr) | DWF/DGN, delad DWF-definition, transformering/klippning; kräver `source.dwf` med blad `Model` och `source.dgn` med modell `Default`. | 14 |
| [21_native_records.scr](facit/21_native_records.scr) | Sex materialmappar med icke-identiska mappermatriser, LAYER_FILTER, IDBUFFER, DATATABLE med flera värdetyper/referenser; `INDEXCTL=3` och native DWG-sparning för index; ändrad materialopacitet. | 28 |

[outputs.txt](facit/outputs.txt) listar de 518 namnen.
[coverage.md](facit/coverage.md) kopplar provfallen till fas 5/6/7/9 och skiljer
ritningsinnehåll från krav som måste konstateras i AutoCAD:s sparade poster.
De medföljande `raster.bmp`, `raster_å.bmp` och `environment.hdr` är små genererade
bildresurser. Bara AutoCAD:s sparade DXF-filer är facit. Inga DXF-filer skapas av
författarverktygen, HDxf, ODA eller ezdxf här.

Kommandokontrollerna maskar `CMDACTIVE` med `3`: skriptets egen bit `4` ska inte
tolkas som en obesvarad kommandoprompt.
[Autodesk: CMDACTIVE](https://help.autodesk.com/cloudhelp/2025/ENG/AutoCAD-Core/files/GUID-18B2E090-6C94-4C24-A70A-F2D6BDD5569C.htm).

**Verifieringsstatus:** ASCII utan BOM, CRLF, ett balanserat AutoLISP-uttryck per
exekverbar rad, utdataunikhet och skriptens start/slut har kontrollerats statiskt.
Ingen AutoCAD-körning, HDxf-byggning eller commit har gjorts. `KLAR` betyder att
skriptet nått slutet; det ersätter inte kontroll av de sparade filernas typer,
relationer och aktuella tidsstämplar. Äldre nedsparning kan spränga, utelämna eller
proxybevara nyare innehåll. BODY, flera toppkroppar, index och särskilda interna
ACIS-/associationsklasser får inte räknas som belagda bara för att geometrin finns.

### Kan inte skriptas

Följande kräver handarbete, originaldata eller ett kompletterande API/plugin
utöver de fristående `.scr`/AutoLISP-filerna. Detta är inte ett påstående att de är
omöjliga med all programkod som kan köras i AutoCAD.

- **GEODATA via GEOGRAPHICLOCATION:** placera markören och välj koordinatsystem i
  GUI; för planens nät och långa koordinatsystem behövs dessutom ett verkligt
  georefererat underlag/API. Det dokumenterade kommandot använder dialogen.
  [Autodesk: GEOGRAPHICLOCATION](https://help.autodesk.com/cloudhelp/2025/ENU/AutoCAD-Core/files/GUID-10A3B776-A0FA-4438-B29B-EA22C070A27E.htm).
- **OLE med giltig nyttolast:** infoga en länkad/inbäddad/statisk OLE-ram via
  INSERT/PASTESPEC från en verklig OLE-källa och spara. En äldre OLEFRAME behöver
  originalritning. AutoLISP/ActiveX har ingen motsvarande fristående skapandemetod.
  [Autodesk: OLE Object](https://help.autodesk.com/cloudhelp/2017/DEU/AutoCAD-ActiveX/files/GUID-F22B8915-FE51-4650-A738-95C81019064E.htm).
- **Dynamiska lookup-/property-tabeller:** fyll BLOOKUPTABLE/BTABLE i Block Editor
  för planens tabellgrenar och kombinationer. Grundparametrar/actions skriptas i 16;
  lookup-action visar en dialog.
  [Autodesk: BACTION](https://help.autodesk.com/cloudhelp/2022/ENG/AutoCAD-Core/files/GUID-98CCB318-60F6-46C9-8F90-C2B8614553C4.htm).
- **SUNSTUDY:s kalender-, tim- och sheet-set-grenar samt äldre MentalRay/renderposter:**
  behövs som verkliga original/API-prov; ingen dokumenterad kommandokedja för att
  skapa hela dessa äldre objekt i AutoCAD 2025 har fastställts. En konstruerad
  `290=12` ersätter inte ett native timprov. Moderna ljus/SUN/RapidRT/IBL skriptas.
- **Plugin-/applikationsdata:** riktiga custom-proxyer med klasspayload, Map-objekt,
  TC/ZWM, ASE/OBJECT_PTR och VBA_PROJECT kräver rätt produkt/plugin/originalpayload.
  Öppna ursprungsritningen före sparning och skapa de begärda variationerna med
  dess verktyg. Skapa inte tomma låtsasposter för att få ett typnamn i DXF.
- **Historiska format och modellkärnans interna grenar:** AutoCAD 2025 erbjuder inte
  R13/R14 DXF i SAVEAS. De rutorna, ursprungliga dos932-/gamla SAT-versioner och
  äldre renderer behöver en lämplig AutoCAD-utgåva/originalfil. Sällsynta ACIS-deltyper,
  en viss tolerant topologi, flera topp-body i samma SAB och en viss intern
  associationsklass kan inte beställas med namn via ritkommandon; bevara ett verkligt
  prov när AutoCAD faktiskt framställer det. Separata lumps i 10 är ett försök,
  inte bevis för flera topp-body.

NWD/NWC/DWF/DGN är **resurskrav, inte skriptförbud**: 19–20 har kommandokedjorna
och avbryter med ett tydligt resursfel om originalfilerna saknas. Färg-/font-/shape-
resurser hämtas från installationen; de distribueras inte med biblioteket.
