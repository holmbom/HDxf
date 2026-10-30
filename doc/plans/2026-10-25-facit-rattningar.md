# Faciträttningar — oförändrad rundtur R12–2018

**Status:** implementationsplan, undersökt 2026-10-25. Ingen implementation, byggning eller
testkörning ingår i planleveransen. Endast denna planfil ändras.
**Underlag:** [testresultat.md](../facit/testresultat.md), läst i sin helhet,
[CLAUDE.md](../../CLAUDE.md), berörda läsar-/skrivarfunktioner och stickprov direkt i
AutoCAD 2025:s lokala ASCII-facit. Rapportens binäranalys och provkörning i scratchpad
är tidigare resultat, inte körningar utförda vid denna planering.

## Mål

1. Alla **476/476** AutoCAD-filer i `doc/facit/*.dxf` ska kunna läsas och skrivas i samma
   version och format med **byte-identiskt resultat**: inga läsfel, skrivfel eller differenser.
   Nuläge enligt rapporten: 311 identiska, 125 differenser, 12 läsfel, 28 skrivfel.
   Varje version har 68 filer; R12 har 67 fel av 68. Antalet filer har kontrollerats lokalt.
2. En läst, oförändrad post skrivs som AutoCAD skrev den. Ordning, explicita standardvärden,
   taltext, tabellräknare, extents, attributfragment, dictionaries och relationer bevaras.
   HDxf ska inte reparera eller normalisera sådant som AutoCAD avsiktligt lämnade så.
3. Nya ritningar och nya/ändrade poster använder den belagda AutoCAD-formen. Bevarande
   får inte dölja redigeringar, återinföra borttagna poster eller stänga av dimensionsblock,
   speglar, extentsberäkning och relationsunderhåll där de faktiskt behövs.
4. Ett permanent test i `test/test_autocad_facit.cpp` hittar alla närvarande lokala facit,
   testar även binärfiler och hoppar över när katalogen saknas eller saknar DXF-filer.
   Det kräver ingen ändring av `.gitignore`, där dessa filer redan är ignorerade.
5. **En implementationsagent, ett svep:** alla kodrättningar och enhetstester först;
   därefter ett gemensamt slutsteg med ett bygge, hela sviten och en full facitrundtur.
   Inga delbyggen, delsviter, commits, versionsändringar eller releaseåtgärder ingår.

Facit är endast filer som AutoCAD självt sparat. Inga goldenfiler skapas eller uppdateras.
Små tekniska indata i enhetstester är tillåtna som stimuli, men får inte upphöjas till
facit för DXF-formen. Förväntade grupper nedan kommer från AutoCAD-filerna. Ingen kod
kopieras från andra DXF-bibliotek. Inga externa DXF-program behövs för denna rättningsfas.

### Filnotation och numrering

`P(V,N)` betyder exakt `doc/facit/acad_<V>_dxf_<N>.dxf` och motsvarande
`doc/facit/acad_<V>_dxf_<N>_bin.dxf`. `V=r12` respektive `2000`, `2004`, `2007`,
`2010`, `2013`, `2018`. När endast ASCII eller binär anges avses bara den av de två.
En uppräkning av versioner och namn betyder alla deras kombinationer, inte andra filer.
Exempel: `P(2013,text)` är `acad_2013_dxf_text.dxf` och `acad_2013_dxf_text_bin.dxf`.
Grupper skrivs som `kod=värde`; ordning i kodlistor är betydelsefull.

| Plan | Rapport | Plan | Rapport |
|---|---|---|---|
| F1–F8 | A1–A8, i samma ordning | F9–F22 | B1–B14, i samma ordning |

Rapportens antal per orsak överlappar, särskilt i R12, och ska inte summeras som antal filer.
`$VERSIONGUID` och `$HANDSEED` är följdskillnader, inga ytterligare grundorsaker.
GUID-regeln i `write_header_template()` ska behållas för verkliga ändringar; GUID eller
HANDSEED får inte hårdkodas för att få jämförelsen att gå igenom.

## Gemensamt beslut: template-läge även för R12

**R12 får postbaserat template-läge**, med samma princip T/O/N som 2000+:
T är den bevarade posten, O dess typade ursprung skrivet med dagens skrivare och N
dagens typade post. När O=N skrivs T; vid ändring ersätts berörda fält/listor.
Detta är gemensam infrastruktur för F9–F22, inte en tjugotredje grundorsak.

Riktade formatteringsfixar ensamma räcker inte. F10 visar en tom VIEW-tabell med
`70=10`; poster som AutoCAD räknat finns inte i R12-exporten. F12/F13/F22 visar olika
ordning och precision för exporterade och vanliga poster utan en säker typmarkör.
F9/F19 visar extents från ursprungsritningens objekt, även sådana som R12-exporten inte
innehåller. Dessa uppgifter kan inte återskapas genom att räkna/geometriberäkna den lästa
R12-modellen. Särfall efter filnamn, handtag, koordinater eller enbart `70=8/32` är fel metod.

### Exakt integrationsarbete, gemensamt med F9/F10

- `src/dxf_reader.hpp/.cpp`: skilj **record capture** från `r13_plus_`. Ta bort R12:s
  `stop_capture()` i `DxfReader::read()` och spara HEADER, TABLE-huvuden, tabellposter,
  BLOCK/ENDBLK, entiteter och VERTEX/ATTRIB/SEQEND även i R12. Subclass-/102-tolkning,
  namnupplösning, BLOCK_RECORD-länkning och ACDSDATA ska fortfarande vara versionsstyrda;
  att bevara R12 betyder inte att tolka det som 2000. Behåll standardposter som verkligen
  finns i filen, men inför inga nya BYBLOCK/BYLAYER-poster genom denna ändring.
- `include/hdxf.hpp`, `src/drawing.cpp`: inför privat, explicit tillstånd för lyckad
  inläsning/template-underlag. `source_version_ == R12` kan inte ensam avgöra om en ritning
  lästs: en ny `Drawing()` har samma värde. Nollställ tillståndet vid ny inläsning och sätt
  det först när läsningen lyckas, i både `load()` och `load_from_memory()`.
  Aktivera R12-template bara för läst R12 som skrivs som R12; ändra inte konverteringsavtalet
  eller möjligheten att sätta version på en ritning byggd i minnet.
- `src/template_2000.hpp/.cpp`: generalisera befintliga hjälpare utan filbyte. R12:s
  rekonstruerade jämförelseunderlag får ingen syntetisk OBJECTS-/BLOCK_RECORD-sektion.
  Spara vid lyckad läsning en privat R12-ursprungsrepresentation av de bevarade posterna,
  oberoende av de redigerbara listorna. Läs O från den, så att borttagningar, omordningar,
  namnbyten och ändringar av själva raw-underlaget kan upptäckas. Det är jämförelseunderlag,
  aldrig en genväg som returnerar den ursprungliga filen utan att granska ändringar.
- `src/dxf_writer.hpp`, `src/dxf_writer.cpp`, `src/dxf_writer_template.cpp`: inför privat
  `write_r12_template()` och R12-grenar i `prepare_template()`/`write_entity_template()`.
  N/O-capture ska anropa R12:s rena typade skrivare, inte `write_entity_r2000()` eller
  `write_dimension_r2000()`. Undvik rekursion mellan dispatch och template-wrapper.
  Återanvänd `emit()`/`merge_record()`, rågruppskrivning och filtrering av borttagna pekare.
- Matcha entiteter/barn via bevarade handtag. R12:s tabellposter och vissa BLOCK saknar
  handtag: matcha dem med tabelltyp + ursprungsnamn respektive blocknamn, med separat
  identitet för ENDBLK. Låt inte alla handtagslösa poster hamna under nyckeln 0 i
  `emit()`/ursprungsmapparna. Nya/omdöpta poster får nya identiteter och rätt utdata.
  Behåll läst ordning för oförändrade tabeller, block och barnposter.
- HEADER/TABLES/BLOCKS/ENTITIES behåller R12:s struktur och kodbredd. Använd R12:s
  `$DWGCODEPAGE` även som source code page i `GroupWriter`; återkoda inte bevarade strängar.
  Bevara taltext i ASCII och binärens värden genom befintlig rågruppsväg, med F5:s kontextfix.
  ASCII behåller inlästa radslut genom `Drawing::write_dxf()`. Inga nya 2000-sektioner tillförs.
- Gör extentsberoenden och tabellmedlemskap jämförbara mot O, enligt F9/F10/F19. En
  headerändring eller ändrad färg får inte i sig återskapa all geometri. Mutabla accessorer
  kräver värdejämförelse; anrop av en accessor är inte bevis på en ändring.
- Låt R12 skriva kroppen före HEADER när nya handtag behövs, som 2000-skrivaren gör.
  Oförändrat underlag får inga nya handtag, inte heller för namnlösa tabellposter eller
  befintliga `$MODEL_SPACE`/`$PAPER_SPACE`. HEADER får HANDSEED efter verkliga tillägg.

Gemensamma tester läggs i `test/test_r12_fixes.cpp`: bevarade poster även utan handtag;
ändring via mutabel accessor; tillägg/borttagning/omordning av VERTEX, ATTRIB och block;
ändring av tabellpost utan handtag; två sparningar utan fortsatt drift; misslyckad eller
upprepad inläsning utan gammalt template-tillstånd. Testa både ASCII och binär där det är
tillämpligt. En orörd syskonpost ska vara identisk även när en annan ändras.

## F1 — DIMENSION utan block får ett nytt block (A1, 24 filer)

**AutoCAD:** `P(2000/2004/2007/2010/2013/2018,associative/associative_endpoint_changed)`.
ENTITIES DIMENSION `257`, lager `ADSK_CONSTRAINTS`: `100=AcDbDimension`, ingen grupp 2,
`10=40.0`, `20=-20.0`, `70=128`, `1=d1=80.0000` i grundfilen. AutoCAD har inte glömt ett
`*Dn`; posten beskriver begränsningen utan ett sådant block.

**HDxf:** `DxfWriter::generate_dim_blocks()` i `src/dxf_writer.cpp` bygger varje
DIMENSION som saknar ett hittat block, även vid `write_r2000()`. Resultat `2=*D1`,
`10=80.0`, `70=160`, BLOCK_RECORD `3FF`, BLOCK `400`, HANDSEED `3FF` → `40C`.

**Rättning:** jämför med ursprungsposten innan block och pilblock genereras. En läst
DIMENSION som saknade block och fortfarande har tomt blocknamn ska fortsätta sakna block,
också när dess begränsningsvärde redigeras; reparera inte den avsiktliga frånvaron.
Oförändrad befintlig blockgeometri ska inte heller regenereras. En ny DIMENSION utan
raw-underlag får fortfarande block. Ett uttryckligen angivet block som sedan tagits bort
ska fortfarande ersättas enligt befintligt kontrakt. Använd `prepare_template()`/ursprungs-
uppslag i `src/dxf_writer_template.cpp` utan att först hinna allokera genererade handtag.
Gör inte flaggan 128 ensam till en allmän markör för att undertrycka dimensionsblock.

**Enhetstest:** i `test/test_roundtrip_2000.cpp`, läs grundfilen och kontrollera frånvaro
av grupp 2, oförändrade 10/70, inga nya block och oförändrad HANDSEED/GUID. Redigera
begränsningens geometri/text: ändringen ska synas utan ett tillagt block. I
`test/test_dimensions.cpp` säkras separat att ny DIMENSION och borttaget namngivet block
fortfarande ger AutoCAD-formade block och Defpoints. Facitparen ovan bevisar grundfelet
och visar också hur AutoCAD sparar den ändrade begränsningen.

## F2 — ATTRIB-fragment byggs om till en post per rad (A2, 20 filer)

**AutoCAD:** `P(2000/2004/2007/2010/2013,blocks_attributes/blocks_attributes_attribute_changed)`.
INSERT `25F`, NOTES: fem ATTRIB med `1=First line`, `Second`, `red`, ` line`, `Third line`,
handtag `264,387,389,38B,38D` och taggar NOTES_001–005. Fragment `389` har
`102={ACAD_XDICTIONARY`, `360=38A`, `1=red`, `2=NOTES_003`; XRECORD `391` listar fyra
sekundära poster med sin tredje 70-räknare och 340-pekare. Formateringsbyten kan dela en rad.

**HDxf:** `Drawing::sync_multiline_attributes()` i `src/drawing_attribute.cpp` jämför antalet
befintliga sekundärposter med `attribute_lines()` antal rader även när den logiska texten
är oförändrad. Fem poster blir tre, nytt handtag `3F1`, andra dictionaries rensas och
koordinater räknas om. `write_attrib_r2000()`/`write_attdef_r2000()` i
`src/dxf_writer_2000.cpp` gör dessutom spegeln via `attribute_mirror()`.
Den logiska texten läses från XRECORD i `Drawing::read_multiline_attributes()` i
`src/drawing_roundtrip.cpp`; `link_multiline_attributes()` märker sekundärposterna.

**Rättning:** använd logisk jämförelse mot O innan layout/antal beräknas. Oförändrad
text/tagg/layout med giltiga ursprungliga sekundärreferenser behåller exakt de lästa
fragmenten, spegelvärdena, ordningen, handtagen, dictionaries och XRECORD-data. Konsistens
ska då avse XRECORD:s faktiska referenser, inte ett beräknat radantal. Rensa bara data
som verkligen blivit ägarlös efter en ändring. Bevara också direkt redigering av en
sekundärpost; den får inte skrivas över för att huvudtexten är oförändrad.

För nya/ändrade logiska texter: utöka `AttributeLine` i `src/attribute_mirror.hpp` med
fragmentets skrivbara format och använd `TextLayout::mtext(..., true)`/`MTextRun` i
`src/attribute_mirror.cpp`, i stället för enbart `MTextSize::text_lines`. Befintliga runs
innehåller redan placering, höjd, breddfaktor, lutning, font och färg. Överför de fält
som facit faktiskt skriver, inklusive fragmentgränser och taggsuffix; anta inte att varje
färgbyte måste bli grupp 62 — just `389` saknar 62. Uppdatera `sync_multiline_attributes()`
och skrivarnas spegelanrop till samma fragmentmodell. Om ytterligare layoutuppgifter
behövs tillförs de i `src/text_layout.hpp`/`src/mtext_layout.cpp`, utan en separat textmotor.

**Enhetstest:** i `test/test_attrib_2018.cpp`: oförändrad femfragmentstext i samtliga fem
versioner, med exakt råpost-/objektbevarande; ändra logisk text och kontrollera fragment,
placering, suffix och XRECORD:s antal/340. Bygg samma belagda formaterade text i minnet
och jämför dess ATTRIB-struktur med AutoCAD-paret. Testa borttagning av huvudattribut och
sekundärpost utan kvarlämnade egna dictionaries. `P(2018,blocks_attributes)` är kontroll
för den redan fungerande `101=Embedded Object`-formen, som inte får regressa.

## F3 — RAPIDRT spärras i 2000–2010 (A3, 16 filer)

**AutoCAD:** `P(2000/2004/2007/2010,render_modeldoc/render_modeldoc_ibl_background_off)`
innehåller `0=RAPIDRTRENDERSETTINGS` med `100=AcDbRapidRTRenderSettings` och
`0=RAPIDRTRENDERENVIRONMENT` med `100=AcDbIBLBackground`, trots versionsnumret.

**HDxf:** `r2013_property()` i `src/drawing.cpp` avvisar båda typerna med exempelvis
`A RAPIDRT render preset requires DXF 2013 or later`. Rapportens isolerade ändring tar
bort spärren och ger 16 identiska filer.

**Rättning:** ta bort just dessa två RAPIDRT-fall ur 2013-spärren för de belagda versionerna
2000–2010. Behåll övriga kontroller, bland annat NAVISWORKSMODELDEF och SAB-konvertering.
R12 ska fortsatt neka nya sådana objekt; ta inte bort dess versionsskydd genom ett
globalt undantag. Den typade skrivformen finns redan i `src/dxf_writer_reference.cpp`.

**Enhetstest:** i `test/test_roundtrip_2000.cpp`: båda objekttyperna läses och sparas i
varje belagd version, samma raw-grupper. Bygg objekten i minnet och jämför deras
subclass-/gruppstruktur med respektive facit; R12 ska ge ett precist versionsfel.
De 16 filerna ovan är bevis; inget nytt AutoCAD-facit krävs för dessa versioner.

## F4 — MULTILEADER utan grupp 341 avvisas (A4, 12 filer)

**AutoCAD:** `P(2000/2004/2007/2010/2013/2018,dimensions)`. MULTILEADER `3C9` har svansen
`340=3C8,90=263168,170=1,91=-1073741824,171=-2,290,291,...` utan 341; `3CF` har `341=14`.

**HDxf:** `parse_mleader()` i `src/mleader.cpp` lämnar `leader_linetype` tom när 341
saknas. `write_mleader()` i `src/dxf_writer_mleader.cpp` slår ändå upp namnet med
`mleader_reference()` och `linetype_handle()` i `src/dxf_writer_2000.cpp`:
`MULTILEADER 3C9: linetype "" does not exist`.

**Rättning:** skriv och slå upp common-delens 341 bara när `leader_linetype` inte är tom.
En verkligt angiven men okänd linjetyp ska fortfarande ge fel. Behåll separata
linjeegenskaper och deras 341-kontext. Lägg inte in BYLAYER som ersättning för frånvaro.

**Enhetstest:** i `test/test_mleader.cpp`: frånvarande, befintlig respektive ogiltig
linjetyp; kontrollera att `3C9` saknar 341 och `3CF` behåller sin pekare. En ny post med
tomt namn ska skrivas med samma tillåtna frånvaro. Alla tolv facitfilerna ovan bevisar felet.

## F5 — DATATABLE:s binära grupp 71 har fel bredd (A5, 12 läsfel)

**AutoCAD:** endast binär `P(2000/2004/2007/2010/2013/2018,native_records/native_records_material_changed)`.
Objektnamnet är `ACDBDATATABLE` i 2000 och `DATATABLE` i 2004+. Bool-cellernas grupp 71
skrivs som int32: `47 00 01 00 00 00`, följd av `47 00 00 00 00 00` för nästa värde.

**HDxf:** `group_value_type(71)` i `src/group_codes.hpp` ger Int16 överallt.
`GroupReader::read_binary()` i `src/group_io.cpp` lämnar två byte olästa och tappar
gruppsynk (`Unknown group code 256 in binary DXF`). Skrivvägarna använder samma felaktiga typ.

**Rättning:** behåll grundtabellen och inför en gemensam intern kontextfunktion för
värdetyp: grupp 71 i just dessa två posttyper är Int32, utanför dem fortfarande Int16.
`GroupReader`/`GroupWriter` i `src/group_io.hpp/.cpp` håller aktuell posttyp från grupp 0,
byter vid varje ny post och använder regeln innan värdet avkodas/kodas. Säkerställ samma
regel i `read_binary`, `write_typed`, `write_raw`, `write_int` och explicit heltalsskrivning
samt replay av `CapturedGroup`. Capture av N/O och återspelning måste ha rätt separat
kontext; ett tillbakaskjutet redan läst grupp-0 får inte orsaka dubbel konsumtion.
Ändra inte globalt intervallet 60–79, boolgruppen 290 eller andra objekts grupp 71.

**Enhetstest:** i `test/test_group_io.cpp`: båda postnamnen, 71=1/0 och ett int32-värde
över int16-gränsen, därefter en annan post med int16-grupp 71. Kontrollera exakta byte,
full konsumtion till EOF och båda skrivvägarna raw/typed/captured. Små bytebuffertar är
tekniska typ-/gränsprov, inte goldenfiler. ASCII-paren är kontroll för oförändrat format.
De tolv binärfilerna ovan bevisar grundfelet.

## F6 — BLKREFS läggs till utan ändrad INSERT-relation (A6, 12 filer)

**AutoCAD:** `P(2000/2004/2007,annotative/annotative_scale_removed)`. BLOCK_RECORD `EEA`
för `*U30` har `102={ACAD_XDICTIONARY,360=10F4,102=},330=1,...,2=*U30,340=0`, ingen
BLKREFS. Även `*U31..*U42` saknar listan; `*U43/*U44` har däremot lästa listor.

**HDxf:** `prepare_r2000()` i `src/dxf_writer_2000.cpp` samlar alla INSERT som
`block_refs_`; `write_block_record_r2000()` och `merge_record()`/`flush()` i
`src/dxf_writer_template.cpp` lägger till `102={BLKREFS,331=292,102=}` på `EEA`.

**Rättning:** beräkna relationer för både N och O. I template-läge kompletteras T bara
med **nya eller ändrade relationer**, inte alla relationer som går att härleda ur N.
För oförändrad relation bevaras också frånvaron av en bakåtlänk. Filtrera befintliga
pekare när målposten faktiskt tas bort. Ändra `write_block()` och `flush()` så att en
saknad 102-grupp inte läggs till när motsvarande N- och O-relation är lika. Vid ändrad
INSERT/blockkoppling tillförs den nya relationen; en ny ritning får full härledd lista.
Jämförelsedata för O får inte använda N:s `block_refs_` genom gemensam writer-state.

**Enhetstest:** i `test/test_roundtrip_2000.cpp`: inget tillägg på `EEA`, lästa listor på
`*U43/*U44` kvar. Lägg till, flytta och ta bort INSERT och kontrollera endast berörda
331-listor. En ny ritning med block+INSERT får BLKREFS. Samtliga tolv filer ovan är bevis;
`P(2010,annotative)` kontrollerar att redan fungerande exportform behålls.

## F7 — LEADER-/HATCH-reaktorer läggs till utan ändring (A7, 12 filer)

**AutoCAD:** samma tolv filer som F6. INSERT `39A` har
`102={ACAD_REACTORS,330=39B,102=}`. LWPOLYLINE `39D` har reaktorn `330=3A0`.
LEADER-posterna `100B,1012,1019` pekar på `39A`; HATCH `103D,1048,1053` använder `39D`,
men dessa är inte uppräknade som bakåtlänkar i AutoCAD:s 2000–2007-export.

**HDxf:** samma insamling i `prepare_r2000()` fyller `reactors_`; `write_reactors()` och
template-merge utökar listorna med de tre LEADER-/HATCH-handtagen.

**Rättning:** använd F6:s N/O-jämförelse även för relationerna
LEADER → annotation, HATCH → källobjekt och deras bakåtlänkar. Bevara T:s exakta lästa
reaktorordning när relationen är oförändrad. Vid nya, flyttade eller borttagna relationer
uppdateras bara dessa, med dubblettskydd och befintlig borttagningsfiltrering. Bevara
uttryckliga reaktorer som programmet ändrat. Ändra inte globalt GROUP:s eller nya
ritningars relationsgenerering. Berörda funktioner/filer är desamma som F6 samt
`DxfWriter::write_reactors()` i `src/dxf_writer_2000.cpp`.

**Enhetstest:** i `test/test_roundtrip_2000.cpp`: orörda listor på `39A/39D`; ändra en
LEADER:s annotation och en HATCH:s source_handles, lägg till/ta bort en relation och
kontrollera målens nya/borttagna bakåtlänkar. En ny ritning får fullständiga relationer
och inga dubletter. F6:s facitpar, inklusive scale_removed, bevisar avvikelsen.

## F8 — MTEXT-ramens XDATA och grupp 90 har olika flaggor (A8, 2 filer)

**AutoCAD:** `P(2013,text)`, MTEXT `261`: `90=19`, men XDATA är
`1001=ACAD,1000=ACAD_MTEXT_TEXT_BORDERS_BEGIN,1070=80,1070=16,1070=46,1040=1.5,`
`1070=81,1070=1,1070=5,1005=791,1000=ACAD_MTEXT_TEXT_BORDERS_END`.
`P(2010,text)` har samma ram-XDATA men `90=3`, utan rambit i själva MTEXT.

**HDxf:** `take_mtext_xdata()` i `src/mtext_data.cpp` kräver att 16=19.
XDATA lämnas rått; `mtext_xdata()` skapar dessutom ett nytt block med 80=19 och
`Drawing::sync_mtext_columns()` i `src/drawing_mtext.cpp` skapar en ny ram `792`.

**Rättning:** identifiera ett strukturellt giltigt ramblock med ramflaggan, antal/handtag
och skala; för MTEXT som har bit 16 godtas både läst hel flagga och läst rambit 16.
Kräv inte att fyllningsbitarna 1/2 också står i rammetadata. Knyt den befintliga ramen
`791` så att synkroniseringen inte skapar en till. Oförändrat T bevarar sin 80=16.
För nya/ändrade ramar skrivs 80=16 som i detta facit; fyllningsbitarna tillhör grupp 90.
Låt 2010-fallet utan rambit 16 behålla sitt råa ramblock utan att slå på ramen eller
normalisera dess flaggor. Ändra inte godtyckligt andra XDATA som liknar rammetadata.

**Enhetstest:** i `test/test_mtext_2018.cpp`: 2013 ger exakt en metadatauppsättning och
en befintlig ram, samma HANDSEED; 2010 ger oförändrad rådata. Ändra fyllning/skala och
ta bort ramen: länkar uppdateras utan dubletter. En ny ram med fyllning skriver 90=19
och 80=16 i den belagda 2013-formen. De två 2013-filerna bevisar felet; 2010 är motprov.

## F9 — R12:s modell-extents räknas om (B1, 56 filer)

**AutoCAD:** `P(r12,text)` har enligt rapporten EXTMAX ungefär `(567.9647,30.18,...)`,
inte HDxf:s `(547.3625,0,...)`; `P(r12,images)` har EXTMAX `(280,203.5294,0)` trots att
bildtypen inte finns kvar i R12-modellen. EXTMIN/MAX använder grupperna 10/20/30.
`P(r12,basic)` och flera andra har z `-0.00000001/+0.00000001`. ASCII
`P(r12,empty)` har `1.0000000000000000E+20`, 16 decimaler i exponentformen.

**HDxf:** `DxfWriter::write()` → `compute_extents()` i `src/dxf_writer.cpp` ersätter
alltid lästa värden. `drawing_extents()` i `src/extents.cpp`, textens/fontens mätning i
`src/text_layout.cpp`/`src/mtext_layout.cpp` och `src/fonts.cpp` kan inte återvinna
ursprungsritningens exporterade bortfall. `format_double(R12)` i `src/group_io.cpp`
skriver exponent med `%.15E`.

**Rättning:** genomför den gemensamma R12-infrastrukturen ovan. Bevara råa EXTMIN/MAX
när geometri och dess mätberoenden är oförändrade. Ingen konstgjord epsilon, fontfix
eller simulerad IMAGE/HATCH/solid ska införas för att efterlikna exporterade extents.
Vid ändrad geometri, blockplacering, dimensionsgeometri eller måttpåverkande textstil
används befintlig beräkning för berört utrymme; nya ritningar beräknas som tidigare.
En explicit ändring av headerns extents ska också synas och får inte skrivas över av O.
Rätta R12:s exponentgren till 16 decimaler (`%.16E`), med egen versionsgren så att
2000+ behåller sitt format. De andra layoutfilerna är lästa beroenden, inte tillstånd
att starta en allmän rättning av font-/ACIS-matematik.

**Enhetstest:** i `test/test_r12_fixes.cpp`: oförändrade extents och exakt taltext för
`empty/basic/images/text`; ändra endast en färg/headertext och kontrollera bevarande;
ändra en linjes slutpunkt och kontrollera nya extents med fortsatt oförändrade syskonposter.
I `test/test_group_io.cpp`: ±1e20 får 16 exponentdecimaler endast i R12. En ny tom ritning
och en ny linje/cirkel behåller den befintliga AutoCAD-strukturkontrollen.
Bevis: dessutom `P(r12,unicode/tables/blocks_attributes/hatch/objects/solids/surfaces/`
`dynamic_blocks_rotation_changed/layers_styles)`; hela R12-matrisen avgör de 56 fallen.

## F10 — R12:s TABLE-räknare ersätts med liststorlek (B2, 42 filer)

**AutoCAD:** `P(r12,annotative_scale_removed)`: `0=TABLE,2=VIEW,70=10`, trots tom tabell.
Rapporten visar även VIEW `3→0`, `20→0`, `11→1`, STYLE `3→2`/`4→2`, DIMSTYLE
`3→2`/`5→2` och LTYPE `5→1` i dynamic_blocks. Vänster värde är AutoCAD:s.

**HDxf:** `write_view_table()` och motsvarande STYLE/DIMSTYLE/LTYPE-funktioner i
`src/dxf_writer.cpp` skriver `size()`. Läsningen i `parse_tables()` sparar i dag inte
R12:s TABLE-underlag; räknaren kan inte härledas från de utskrivna posterna.

**Rättning:** spara och skriv TABLE-huvudet med R12-template. Bevara 70 när medlemskapet
är oförändrat, även om ett fält i en befintlig VIEW/STYLE/DIMSTYLE/LTYPE ändras. Alla
åtta R12-tabeller ska omfattas, inte bara de fyra som fallerat. Nya tabeller använder
befintlig kanonisk räknare. Det går däremot inte att belägga AutoCAD:s uppdatering av en
redan exporterad, avvikande räknare vid tillägg/borttagning ur bara dessa oförändrade
rundturer: välj inte utan belägg mellan `size()`, gammalt värde eller gammalt värde + delta.
Bevara ursprungligt medlemskap i jämförelseunderlaget så att situationen kan skiljas ut.

**Enhetstest:** i `test/test_r12_fixes.cpp`: tom VIEW med 70=10, icke-tom tabell med
avvikande räknare, ändrad befintlig post med samma 70; nyskapad tabell med normalt antal.
Bevis: `P(r12,annotative_scale_removed/dynamic_blocks/dynamic_blocks_rotation_changed/`
`layers_styles/layers_styles_dimstyle_changed/render_modeldoc/render_modeldoc_ibl_background_off)`
och fullmatrisen för samtliga 42. **Belägglucka för medlemsändring:** se facitbeställningen
nedan; den blockerar inte de 476 oförändrade rundturerna.

## F11 — R12-extrusion ligger före geometrin (B3, 28 filer)

**AutoCAD:** LINE `316` i `P(r12,layouts)` har
`5,8,10,20,30,11,21,31,210,220,230`, med normal `(0,0,-1)`.
CIRCLE har `...40,210,220,230`, ARC `...40,210,220,230,50,51`, SOLID
`...13,23,33,210,220,230`. Rapporten anger LINE 22, SOLID 6, CIRCLE/ARC vardera 4 filer.

**HDxf:** `write_common_entity_codes()` i `src/dxf_writer.cpp` skriver 210/220/230
innan `write_line/write_circle/write_arc/write_solid()` börjar geometrin.

**Rättning:** template bevarar läst ordning. Dela dessutom ut extrusion i en privat
hjälpare i `src/dxf_writer.hpp/.cpp` och anropa vid respektive typs belagda plats för
nya grupper/poster. Kontrollera samtliga anropare till common-hjälparen; borttagning
därifrån får inte råka utelämna extrusion för andra typer. Låt 2000-skrivaren vara separat.

**Enhetstest:** i `test/test_r12_fixes.cpp`: ny LINE/CIRCLE/ARC/SOLID med icke-standardnormal,
exakt gruppföljd och bibehållen normal efter läsning; ändra geometrin i läst post och
kontrollera bevarad extrusion. Bevis: `P(r12,layouts/layers_styles/layers_styles_dimstyle_changed/`
`basic/solids/surfaces)`; välj respektive typ ur facit, inte en konstruerad helfil som facit.

## F12 — R12-POLYLINE/VERTEX/SEQEND:s 8/5-ordning förloras (B4, 24 filer)

**AutoCAD:** exporterade poster har exempelvis `0=VERTEX,8=0,5=2D1,10,...`,
POLYLINE `343`: `8=0,5=343`; SEQEND `3E4` samma ordning. Vanliga polylinjer har
däremot `5,8`. Rapporten räknar 15 980 VERTEX med exportordningen i ASCII-materialet.

**HDxf:** `write_polyline()` i `src/dxf_writer.cpp` skriver alltid handtag före lager.

**Rättning:** bevara varje delposts raw-record och dess ordning genom
`parse_polyline()` och R12-grenen i `write_entity_template()`/`emit()`.
En ändrad koordinat i en läst VERTEX ska inte flytta 8/5. Nya vanliga polylinjer får
befintlig vanliga AutoCAD-ordning `5,8`; välj aldrig exportordning enbart från flaggor.

**Enhetstest:** i `test/test_r12_fixes.cpp`: båda ordningarna, både i BLOCKS och ENTITIES,
och POLYLINE/VERTEX/SEQEND separat. Ändra en VERTEX och lägg till en annan; bevarade
poster ska behålla sin ordning och den nya få normal form. Bevis:
`P(r12,basic/surfaces/solids/solids_solid_rotated/dimensions/annotative/annotative_scale_removed/associative/`
`associative_endpoint_changed/hatch/hatch_boundary_moved/hatch_boundary_removed)`.

## F13 — R12:s sexsiffriga exportkoordinater skrivs om (B5, 12 ASCII-filer)

**AutoCAD:** en del POLYLINE/VERTEX-värden är `170.000`, `73.4810`, `0.00000`,
`169.813`, motsvarande `%#.6g`. Den binära exporten kan ha ett annat, mer precist värde.

**HDxf:** `format_double(R12)` och `write_polyline()` gör exempelvis `169.813` till
`169.8129999999999882` efter konvertering till double och tillbaka.

**Rättning:** bevara ASCII-värdets taltext i RawRecord och använd den när motsvarande
typade värde är oförändrat. Nytt/ändrat värde använder R12:s vanliga formattering;
runda inte generellt alla polylinjer till sex siffror. Binärens egna doubles ska behållas,
inte bytas mot ASCII-filens avrundade geometri. Berör `src/dxf_reader.cpp`,
`src/dxf_writer_template.cpp` och råskrivvägen i `src/group_io.cpp` genom R12-integrationen.

**Enhetstest:** i `test/test_r12_fixes.cpp`: bevara just `0.00000`/`169.813`, ändra ett
annat fält och kontrollera att taltexten står kvar; ändra koordinaten och kontrollera
att det nya värdet skrivs. Ny vanlig polyline ska behålla full R12-precision.
Bevis, endast ASCII: `P(r12,annotative/annotative_scale_removed/associative/`
`associative_endpoint_changed/basic/dimensions/hatch/hatch_boundary_moved/`
`hatch_boundary_removed/solids/solids_solid_rotated/surfaces)`.

## F14 — POLYLINE:s 10/20 förloras och 66 flyttas (B6, 18 filer)

**AutoCAD:** `P(r12,basic)`, POLYLINE `272`:
`10=170.0,20=73.0,30=0.0,66=1,70=8`. `P(r12,solids)`, `40B`, har i ASCII exakt
`10=112.4120134431175728,20=73.0193176496964327,30=20.4313310928133234`, därefter 66.
Rapportens kortare tal för `40B` är sammanfattningar, inte strängar att hårdkoda i testet.

**HDxf:** `parse_polyline()` i `src/dxf_reader.cpp` kastar 10/20 som ”always 0”.
`PolylineData` i `include/hdxf.hpp` har bara elevation. `write_polyline()` skriver
`66=1,10=0,20=0,30=elevation`.

**Rättning:** tillför `PolylineData::header_x/header_y`, standard 0, för dessa lagrade
koordinater; behåll `elevation` som enda lagring av 30. Läs/skriv dem utan att lägga dem
till VERTEX-koordinaterna i extents. Ta bort det felaktiga ”always 0”-antagandet i
header/läsare. R12:s typade skrivare placerar 10/20/30 före 66 som i facit; template
bevarar läst ordning. För 2000+ ska motsvarande skrivare använda fälten där posten har
dem, utan att ändra vanlig ny 2D-polyline med nollvärden.

**Enhetstest:** i `test/test_r12_fixes.cpp`: kontrollera lästa header_x/y/elevation, ändra
dem och läs tillbaka, separerat från VERTEX. En ny 2D-/3D-polyline får rätt headerordning.
Bevis: `P(r12,basic/solids/solids_solid_rotated/surfaces/associative/`
`associative_endpoint_changed/annotative/annotative_scale_removed/dimensions)`.

## F15 — VERTEX/SEQEND:s egna egenskaper försvinner (B7, 10 filer)

**AutoCAD:** `P(r12,layers_styles)`, VERTEX `2DB` och SEQEND `2DD` har
`6=BYBLOCK,62=0`. I annotative har bland annat VERTEX `8AA`/SEQEND `8AC` grupp 6.

**HDxf:** `parse_polyline()` läser inte dessa egenskaper och `write_polyline()` skriver
barnen med förälderns lager, utan egen linjetyp/färg. De befintliga raw-fälten används
bara för 2000+ i dag.

**Rättning:** bevara raw även för R12 och modellera barnens egna lager/linjetyp/färg så
att även ändring och nyskapande fungerar. Lägg i `include/hdxf.hpp` till `layer`,
`linetype`, `color` på Vertex och motsvarande `seqend_layer`, `seqend_linetype`,
`seqend_color` på PolylineData, med tomt lager som fallback till föräldern och färg
ByLayer som standard. Behåll befintliga handle/reactors-fält som enda lagring för dem.
Läs i `parse_polyline()` och skriv i `write_polyline()` samt `write_polyline_r2000()`
i `src/dxf_writer_2000.cpp`. Ingen omtolkning av 62=0 till ByLayer=256.

**Enhetstest:** i `test/test_r12_fixes.cpp`: barns BYBLOCK/ACI 0 efter läsning, oförändrad
rundtur och ändring av enbart vertexfärg; nyskapade barn med egna egenskaper och med
fallback till förälderns lager. Bevis: `P(r12,layers_styles/layers_styles_dimstyle_changed/`
`annotative/annotative_scale_removed/dimensions)`; kontrollera aktuella barnposter i
dessa filer. 2000+ testas också eftersom modellen och parsern delas.

## F16 — VERTEX:s explicita nollbredder och XDATA tappas (B8, 2 filer)

**AutoCAD:** `P(r12,basic)`: VERTEX `2CF` har `40=0.0,41=0.0`; `258` har
`1001=HDXF_FACIT,1000=vertex,1070=7` på själva VERTEX, efter dess geometri.

**HDxf:** `parse_polyline()` sparar inte VERTEX-XDATA. `write_polyline()` skriver
40/41 bara vid värde !=0 och skriver XDATA bara för POLYLINE.

**Rättning:** tillför `Vertex::xdata` i `include/hdxf.hpp`, använd samma XDATA-tolkning
som för entiteter i `src/dxf_reader.cpp` och skriv listan vid rätt VERTEX i båda
polyline-skrivarna. Ändra bara hjälparens gränssnitt om det behövs för att dela logiken.
Explicita nollgrupper bevaras genom template-gruppernas närvaro, utan globala ”skriv alla
standardvärden”-regler. Ändrad XDATA-lista ersätts som en enhet, borttagen lista skrivs inte.

**Enhetstest:** i `test/test_r12_fixes.cpp`: XDATA finns i typad Vertex efter inläsning,
går att ändra/radera och skrivs exakt en gång på rätt barn. Ändra positionen på `2CF`:
40/41 ska stå kvar med sina nollvärden. Ny Vertex med XDATA ska fungera även utan raw.
De två basic-filerna är fullständigt bevis för grundfelet.

## F17 — R12-DIMENSION:s grupper normaliseras fel (B9, 14 filer)

**AutoCAD:** `P(r12,dimensions)`: `297` har typ 5 utan 16/26/36 och utan grupp 3;
`2FC` har `70=132,15/25/35,...,40=0.0` utan grupp 3. `P(r12,annotative)`, DIMENSION
`604`, har ingen grupp 70 men `3=STANDARD` mellan 11/21/31 och 13/23/33.
Posten `604` i dimensions-filen är däremot en VERTEX; handtag ska alltid anges med fil.

**HDxf:** `write_dimension()` i `src/dxf_writer.cpp` skriver 3 direkt efter 2, alltid
70, alltid 16/26/36 även för typ 5, men utelämnar 40=0.

**Rättning:** lästa posters närvaro, frånvaro och placering bevaras genom R12-template.
Justera också typad R12-form: 3 efter 70/1, före 13; utelämna standardstilen med
skiftlägesokänslig namnkontroll när ingen läst explicit grupp finns; skriv 70 om värdet
är icke-noll, 16/26/36 för tillämplig fyrpunktsvinkel (typ 2), inte automatiskt för
trepunktsvinkel (typ 5); skriv grupp 40 även vid noll för radie/diameter. Bevara explicit
läst `3=STANDARD` och `40=0.0` vid andra ändringar. F1 avgör separat blockgenereringen.

**Enhetstest:** i `test/test_dimensions.cpp`: varje berörd dimensionstyp ny och läst;
kontrollera gruppordning/frånvaro, ändrad definitionspunkt, bibehållen explicit dimstil
och nollängd. Bevis: `P(r12,dimensions/annotative/annotative_scale_removed/associative/`
`associative_endpoint_changed/layers_styles/layers_styles_dimstyle_changed)`.

## F18 — VIEW:s bredd 41 står före centrum (B10, 8 filer)

**AutoCAD:** VIEW `HDXF_RENDER` i `P(r12,render_modeldoc)`:
`2,70,40,10,20,41,11,21,31,...`; 40=`22.29356869806983`,
10=`21.3201880178610885`, 20=`11.6849263078178804`, 41=`44.8514880131128066` i ASCII.
`P(r12,layers_styles)` har motsvarande ordning på VIEW `HDXF_PLAN`.

**HDxf:** `write_view_table()` i `src/dxf_writer.cpp` skriver `70,40,41,10,20,11,...`.

**Rättning:** flytta 41 efter 10/20 i den typade R12-skrivaren. Genom F10:s
template-väg behålls både TABLE-räknaren och den lästa VIEW-postens övriga taltext.

**Enhetstest:** i `test/test_r12_fixes.cpp`: ny VIEW har facits ordning; en ändrad bredd
hamnar på rätt plats och rubbar inte centrum, tabellräknare eller andra VIEW-poster.
Bevis: `P(r12,layers_styles/layers_styles_dimstyle_changed/render_modeldoc/`
`render_modeldoc_ibl_background_off)`, åtta filer.

## F19 — R12:s pappers-extents räknas om (B11, 5 filer)

**AutoCAD:** `P(r12,layouts)`, `$PEXTMAX` grupp 20=`190.0586630286494`, mot HDxf:s 185.
Endast ASCII i `P(r12,dimensions/solids/solids_solid_rotated)` har PEXTMIN
10=`1.1692913385826771`, 20=`0.8267716535433073`, mot omskrivna slutdecimaler.

**HDxf:** samma ovillkorliga `compute_extents()`/headerersättning som F9 i
`src/dxf_writer.cpp`. Binärparen för de tre sistnämnda har ingen motsvarande differens.

**Rättning:** samma bevaranderegel som F9, men med separat beroende på pappersrummets
geometri. Modelländring får inte automatiskt ersätta pappers-extents och tvärtom.
Behåll den befintliga skillnaden mellan tomt papper med nollor och lästa ±1e20-sentineler.
Vid verklig ändring av pappersgeometri används befintlig pappersberäkning.

**Enhetstest:** i `test/test_r12_fixes.cpp`: exakt taltext ur de fem filer som ovan
avgränsats; ändra enbart modellgeometri respektive enbart pappersgeometri och kontrollera
att bara rätt utrymmes extents räknas om. Ny ritning med tomt papper behåller nollor.
Facit är dessa fem filer; binärparen är kontroll för att inte införa en ny differens.

## F20 — XDATA på INSERT:s ATTRIB skrivs två gånger (B12, 4 filer)

**AutoCAD:** `P(r12,annotative/annotative_scale_removed)`, ATTRIB `748,75C,76F` i `*U44`:
en lista `1001=ACADMATTDEFINANNOBLOCKDECO,1000=AnnotativeData,1002={,1070=1,1070=1,1002=}`.

**HDxf:** `write_insert()` i `src/dxf_writer.cpp` skriver listan efter anropet
`write_attrib()`, som redan skriver samma lista. Inläsningen har bara en uppsättning.

**Rättning:** ta bort det andra `write_xdata(attr.props.xdata)` ur INSERT-loopen.
`write_attrib()` äger skrivning av ATTRIB:s XDATA och låsmetadata. Detta ska rättas
även i den typade vägen; template får inte bli en maskering av dubbelskrivningen.

**Enhetstest:** i `test/test_r12_fixes.cpp`: ny INSERT med ATTRIB och denna XDATA
skrivs med ett enda 1001-block på ATTRIB, inte på INSERT/SEQEND; samma efter redigering
och återinläsning. Fristående ATTRIB och låsmetadata ska fortfarande fungera.
De fyra annotative-filerna ovan bevisar grundfelet.

## F21 — INSERT:s explicita enhetsskalor försvinner (B13, 2 ASCII-filer)

**AutoCAD:** endast ASCII `P(r12,associative/associative_endpoint_changed)`, INSERT `2B3`
i block `*U5`: `30=0.0,41=1.0,42=1.0,43=1.0,50=74.9999999999999716` i grundfilen.

**HDxf:** `write_insert()` i `src/dxf_writer.cpp` utelämnar varje skalgrupp som är 1.

**Rättning:** bevara explicita standardvärden via RawRecord och R12-merge, också när
en annan INSERT-egenskap ändras. Ändra inte alla nya INSERT till att alltid ha tre
skalgrupper: frånvaro och explicit 1 är båda belagda. Vid ändring av en skala ska
motsvarande lästa grupp ersättas och de andra bevaras.

**Enhetstest:** i `test/test_r12_fixes.cpp`: ändra endast rotation, kontrollera att
41/42/43 står kvar; ändra x_scale och läs tillbaka utan att y/z-grupperna försvinner.
Kontrollera också ny INSERT med icke-enhetlig skala och ny standard-INSERT mot respektive
belagda form. Facit är exakt de två ASCII-filerna ovan; binärerna ingår i fullmatrisen.

## F22 — TEXT i exporterade dimensionsblock normaliseras (B14, 2 filer)

**AutoCAD:** `P(r12,dimensions)`, TEXT `594/595` i `*D`-block. ASCII `594` har
`0=TEXT,8=0,40=0.18,5=594,62=0,72=0,71=0,10=134.719,20=19.5427,30=0.00000,1=...`.
Det är inte vanlig TEXT-ordning; även explicita nollgrupper och taltext är betydelsefulla.

**HDxf:** `write_text()` i `src/dxf_writer.cpp` skriver som vanlig TEXT:
`5,8,62,10,20,30,40,1,...`, och nollflaggorna tappas.

**Rättning:** R12-template omfattar också entiteter inne i dimensionsblock. Bevara
TEXT-postens ordning, nollgrupper och sexsiffriga koordinater; härled inte exportordning
enbart från att blocknamnet börjar med `*D`. Ny vanlig TEXT får fortsatt normal form.
Om dimensionens geometri verkligen ändras regenereras dess block genom befintlig
dimensionsbyggare med R12-dispatch, inte 2000-subclasses; om bara TEXT-värdet ändras
ska den lästa TEXT-postens övriga grupper bevaras.

**Enhetstest:** i `test/test_r12_fixes.cpp`: råa `594/595` oförändrade, ändring av enbart
textinnehåll behåller ordning och 71/72; ändrad dimensionsgeometri ger ändrat block och
ny vanlig TEXT får normal ordning. Bevis: de två dimensions-filerna. Exportens ovanliga
ordning ska inte införas som generell regel i `src/dimension_block.cpp`.

## Ordning och beroenden — ett implementationssvep

1. **F5 först:** kontextberoende grupp-I/O med tester, eftersom tolv binärfiler annars
   inte kan ingå i jämförelseunderlaget. F3 och F4 kan därefter rättas direkt.
2. **R12-grunden under F9/F10:** capture frikopplad från R13-tolkning, lyckad-inläsning-
   tillstånd, oberoende ursprungsunderlag, handtagslös identitet, R12-dispatch för N/O,
   TABLE-/HEADER-bevarande, kropp före header och rätt code page. Skriv infrastruktur-
   testerna samtidigt. Det räcker inte att enbart ändra `from_file`-villkoret.
3. **F14–F16:** modell/läsning för POLYLINE-header och barnens egenskaper/XDATA,
   sedan typad skrivning i både R12 och 2000+. Dessa fält måste finnas innan N/O-
   jämförelserna ska kunna upptäcka riktiga redigeringar av dem.
4. **F1, F6, F7:** ursprungsjämförelse före generering, separata relationsmängder för N/O,
   template-merge som bevarar frånvarande relationer och samtidigt hanterar ändringar.
   F1 behövs före F9/F19:s bedömning av om geometrin verkligen har ändrats.
5. **F2 och F8:** oförändrade sekundärposter/länkade ramar bevaras före synkronisering;
   ny/ändrad text använder belagda fragment respektive rammetadata. F2 bygger vidare på
   befintliga layout-runs, inte på ny textlayout från ett annat bibliotek.
6. **F9–F13, F17–F22 färdigställs:** extentsberoenden, tabellhuvuden, ordning, precision,
   standardgruppers närvaro och borttagen XDATA-dubbelskrivning. Alla enhetstester skrivs
   nu; ingen av punkterna har ett eget bygge eller en egen testslinga.
7. **Permanent facitprov och kontroll av nybyggda ritningar**, enligt nästa avsnitt.
   Därefter utförs endast det gemensamma slutsteget.

F6 och F7 ska dela relationslösning men ha separata regressionstester. F12/F13/F21/F22
är framför allt bevaranderättningar; de ska inte bli globala formateringsändringar.
Rapportens tidigare resultat 40/40 för A3–A5 motiverar prioriteten men ersätter inget
av slutstegets kontroller av den färdiga implementationen.

## Permanent facitprov och enhetstesters placering

Utöka **befintliga** `test/test_autocad_facit.cpp`; lägg inte till testprogram,
CMake-downloads eller facit i git. `HDXF_SOURCE_DIR` finns redan i testmålet.

Testet `autocad_facit_all_local_files_round_trip` ska:

1. Slå upp `path_from_utf8(HDXF_SOURCE_DIR) / "doc" / "facit"` med `std::filesystem`.
   Saknad katalog eller noll reguljära `.dxf`-filer ger `hdxf_test::skip()` med tydligt
   skäl. Andra katalog-/läsfel ska ge testfel, inte skip.
2. Samla **alla** närvarande `.dxf`, sortera deterministiskt på sökväg. Ingen hårdkodad
   namnlista, ingen vitlista av lyckade fall och inget krav på exakt 476 i det permanenta
   testet. Nya lokala AutoCAD-filer ska tas med automatiskt.
3. Läs originalbyte med kontrollerad filöppning. Avgör ASCII/binär från den fullständiga
   22-byte-sentineln, inte `_bin` i filnamnet. Använd en ny `Drawing` per fil och
   `load_from_memory()` → `save_to_memory(samma format)` utan mellanliggande ändringar.
4. Kontrollera båda anropen, tom utdata/`last_error()` och exakt bytevektor inklusive
   radslut och filslut. Använd UTF-8-sökvägsfunktionerna för diagnostik. Kör inga
   facitfiler genom `save()`/`save_as()`; originalen får aldrig skrivas över.
5. Fortsätt efter en enskild fils läs-/skriv-/jämförelsefel och samla hela resultatet.
   Gör en slutlig assertion med filnamn, version/format, felmeddelande eller första
   avvikande byte och längder. Binärdiagnostik ska ange byteoffset, inte en meningslös
   textrad. Summera identiska/differenser/läsfel/skrivfel per version och format.
6. Under denna fas krävs dessutom i slutredovisningen **476 observerade, 476 identiska**,
   68 per version och 34 per format/version. Ett permanent test som har skippats är
   inte godkänd verifiering av denna fas.

Orsakernas riktade facitprov använder samma läshjälpare och `skip()` när just deras
lokala fil saknas; de får inte skriva ut egna facitkopior. Tekniska enhetstester för
kontext, muterbara fält, explicit noll, relationsdelta och nybyggda ritningar ska också
kunna köras utan `doc/facit`. Behåll befintliga sju `doc/acad_*_dxf.dxf`-tester och
R12:s struktur-/radslutstester. Gör inga tester som jämför enbart HDxf-utdata med
HDxf-utdata och kallar detta bevis för AutoCAD:s form.

### Planerade implementationsfiler

Denna lista är framtida implementation, inte ändringar gjorda i planleveransen.
Återanvänd befintliga filer så att inga nya mål eller CMake-källistor behövs.

| Fil | Ändring |
|---|---|
| `include/hdxf.hpp` | Privat R12-underlag/inläsningstillstånd; POLYLINE-/VERTEX-fälten i F14–F16 |
| `src/drawing.cpp` | Lyckad load-state, R12-template i write_dxf, RAPIDRT-spärr |
| `src/dxf_reader.hpp`, `src/dxf_reader.cpp` | R12-capture frikopplad från R13-tolkning, typade barnfält |
| `src/template_2000.hpp`, `src/template_2000.cpp` | R12-jämförelseunderlag och versionsriktig rekonstruktion |
| `src/dxf_writer.hpp`, `src/dxf_writer.cpp` | R12-template-integration, kanoniska rättningar och genereringsvillkor |
| `src/dxf_writer_template.cpp` | R12-dispatch/identiteter, relationsdelta, bevarande |
| `src/dxf_writer_2000.cpp` | N/O-relationer, attributspeglar, delade POLYLINE-barnfält |
| `src/dxf_writer_mleader.cpp` | Valfri common-341 |
| `src/group_codes.hpp`, `src/group_io.hpp`, `src/group_io.cpp` | DATATABLE-kontext och R12-exponent |
| `src/drawing_attribute.cpp` | Oförändrade attributfragment och ändringsstyrd synkronisering |
| `src/attribute_mirror.hpp`, `src/attribute_mirror.cpp` | Formateringsfragment för ny/ändrad attributtext |
| `src/mtext_data.cpp` | Rammetadata 80/90 |
| `src/drawing_mtext.cpp` | Bevarande/synkronisering av redan länkad ram |
| `test/test_autocad_facit.cpp` | Full lokal facitmatris och gemensamma kontroller |
| `test/test_r12_fixes.cpp` | F9–F22 och R12-template-regressioner |
| `test/test_group_io.cpp` | F5 och exponentformat |
| `test/test_roundtrip_2000.cpp` | F1/F3/F6/F7 |
| `test/test_dimensions.cpp` | Ny/ändrad dimensionsgeometri och R12-grupper |
| `test/test_attrib_2018.cpp` | F2, inklusive oförändrad 2018-form |
| `test/test_mtext_2018.cpp` | F8 och ramändringar |
| `test/test_mleader.cpp` | F4 |

Det är **27 planerade kod-/testfiler**. `src/text_layout.hpp` och
`src/mtext_layout.cpp` är ytterligare två möjliga filer endast om deras befintliga
`MTextRun` inte räcker för F2; grundlösningen ska återanvända den layout de redan ger.
`src/extents.cpp`, `src/text_layout.cpp`, `src/fonts.cpp`, `src/dimension_block.cpp`,
`src/drawing_roundtrip.cpp` och `src/dxf_writer_reference.cpp` är granskade beroenden;
ingen generell omskrivning av dem beställs av dessa rundturfel.

## Beläggluckor och exakt extra AutoCAD-facit

**Ingen av de 22 orsakerna saknar facit för den oförändrade rundturen över de 476 filerna.**
Det finns däremot en avgränsad lucka i F10:s utvidgade kontroll av **ändrat tabellmedlemskap**:
vilket 70 AutoCAD skriver när ursprungsräknaren redan avviker från antalet exporterade
poster. Befintliga `layers_styles_dimstyle_changed` ändrar en posts värden och avgör
inte medlemsfrågan. Att 70=10 ska bevaras vid oförändrat medlemskap är redan avgjort.

För att avgöra medlemsfrågan behövs exakt följande AutoCAD-sparade par från samma
ursprungs-DWG som `acad_r12_dxf_annotative_scale_removed.dxf`, där VIEW-exporten är
tom men 70=10:

- `acad_r12_dxf_annotative_scale_removed_view_added.dxf` och `_bin.dxf`: lägg enbart till
  en namngiven modellvy `HDXF_F10_VIEW` med centrum (0,0), höjd 100, bredd 200,
  riktning (0,0,1) och mål (0,0,0). Spara som R12 ASCII med precision 16 respektive binär.
- `acad_r12_dxf_annotative_scale_removed_view_removed.dxf` och `_bin.dxf`: utgå från
  samma DWG efter tillägget, ta enbart bort `HDXF_F10_VIEW`, spara på samma sätt.

Jämför TABLE/VIEW 70 före/efter, och att bara den avsedda namngivna vyn tillkommit/
försvunnit ur de exporterbara VIEW-posterna. Det avgör om räknaren följer faktiskt antal,
en bibehållen intern mängd med delta eller någon annan AutoCAD-regel. Om samma osäkerhet
kvarstår för STYLE/DIMSTYLE/LTYPE behövs samma isolerade tillägg/borttagning för den
berörda tabellen innan regeln generaliseras; dessa får inte antas fungera som VIEW.

Dessa extra filer behövs inte för 476-målet och ska inte automatiskt beställas eller
genereras av implementationsagenten. Redovisa kvarstående begränsning för den nya
medlemsändringssituationen och förankra fortsatt arbete med Erik om underlaget saknas.
Detta ger inte tillstånd att ersätta AutoCAD-facit med andra biblioteks utdata eller
bredda uppdraget till allmän konvertering, fontmätning eller en ny R12-exportmotor.

## Ett gemensamt slutsteg — först efter alla rättningar

Följande är framtida körning av implementationsagenten. Inget av detta körs vid
planleveransen. Använd den befintliga Ninja/UCRT64-konfigurationen i `build`.

1. **Ett bygge:** `cmake --build build`. Om byggträdet saknas krävs först vanlig
   CMake-konfigurering med Ninja/UCRT64 enligt CLAUDE.md; det är förberedelse, inte ett
   godkänt bygge. Byt inte toolchain. Ett byggfel är ett faktiskt fel, inte testframgång.
2. **En full svit:** `./build/test/hdxf_tests.exe` utan filter. Det permanenta
   `autocad_facit_all_local_files_round_trip` utför den **enda fulla rundturen** i denna
   körning. Kör inte därefter en andra scratchpad-batch över samma 476 filer.
3. **Samma körning ska kontrollera nybyggda/ändrade ritningar:** behåll och utöka
   `test_autocad_facit.cpp`-kontrollerna för headerordning, BLOCKS/ENTITIES och CRLF,
   och använd de riktade testerna ovan för dimensioner, rammetadata, attributfragment,
   relationer, extrusion, VIEW, POLYLINE-header/barn och XDATA. Ny ritning testas i
   alla sju versioner och båda formaten. Struktur jämförs med AutoCAD:s befintliga
   filer för samma innehåll; tillåt endast uttryckligen irrelevanta handtags-/GUID-
   värden i dessa strukturprov. Rundturens bytejämförelse tillåter inga normaliseringar.
4. **Samlad redovisning:** byggutfall, svitens pass/fail/skip, filantal och per-version/
   format-rundtur. Godkänt för befintligt underlag är 476 identiska, 0 differenser,
   0 läsfel, 0 skrivfel, plus godkända kontroller för nya/ändrade ritningar.
   Oförändrad HANDSEED/GUID ska följa av bevarandet. Redovisa F10:s eventuella belägglucka
   separat; dölj den inte bakom ett grönt rundturresultat.

Inga mellanliggande byggen eller testsvep planeras. Om slutsteget fallerar ska agenten
redovisa det, rätta inom uppdraget och upprepa endast nödvändiga kontroller med faktiskt
antal körningar angivet. Ett första misslyckat svep får inte beskrivas som ett lyckat
enda svep eller som 476/476. Ingen release, tagg, commit eller push ingår i planen.

## Utfall F9–F22 (2026-10-25)

Genomfört i ett svep, ett bygge, en full svit (616 av 616) och en full rundtur: 476 av 476 filer
byte-identiska, R12 inklusive. Avvikelser från planen:

- Template-läge för R12 är postbaserat som planerat, men posterna paras ihop med ritningen som den
  lästes efter sin plats bland de poster filen hade (R12:s tabeller saknar handtag), inte med handtag.
  Tabellhuvudet jämförs med antalet poster tabellen hade vid inläsning (`Drawing::loaded_table_sizes_`).
- Facit visade två saker utöver planen: kodrader med fyra kolumner i AutoCADs exporterade
  POLYLINE/VERTEX/TEXT (`DxfGroup::wide`) och NaN-doubles med bitmönster i två binära filer.
- F14: den typade skrivaren behåller ordningen 66, 10, 20, 30 (610 av 622 POLYLINE i facit har den);
  ordningen 10, 20, 30, 66 hos AutoCADs exporterade 3D-polylinjer följer av bevarade poster.
- F2: regeln är en ATTRIB per format-körning i skalad eller roterad INSERT och en per rad i en oskalad
  (se testresultat.md). Dedikerat AutoCAD-facit gjordes inte.
- Belägglucka F10 stängd (2026-10-25): Erik gjorde filerna för VIEW, STYLE och LTYPE tillagd/borttagen med
  `doc/facit/14_annotative.scr`. Regeln: filens räknare sänks inte av en borttagen post, och varje tillagd post
  höjer den med ett; se `doc/facit/testresultat.md` och `autocad_facit_f10_*`.
