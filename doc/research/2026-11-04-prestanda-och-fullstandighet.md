# Prestanda och fullständighet på verkliga ritningar

Analys av HDxf 1.9.1 mot verkliga, tunga ritningar: om all data går att få ut, om det finns något tak, vad öppning och
sparning kostar i tid och minne, och vad som inte fungerar. Åtgärderna finns i
[planen för patch 1.9.2](../plans/2026-11-04-patch-1-9-2-prestanda.md).

## Sammanfattning

| Fråga | Svar |
|---|---|
| Går all data att få ut? | Ja. Alla 46 filer är byte-identiska vid oförändrad sparning, och nästan allt finns kvar när posterna skrivs enbart från typade värden. |
| Finns det något tak? | Inte i data eller filstorlek upp till det som provats (4,1 GB). Minnet är det verkliga taket: en sparning kräver 9–26 gånger filens storlek. En hård gräns hittades: nästlade block (krasch). Värden över 32 767 i 16-bitarsgrupper kapas, som AutoCAD själv gör, men utan varning. |
| Hur lång tid tar öppning? | Högst 5,7 s, de flesta under 1 s. HDxf är snabbare än AutoCAD:s kärna på alla jämförda filer. |
| Hur lång tid tar sparning? | Sparning hänger på ritningar med vissa solider (över 74 CPU-minuter, avbruten). Övriga tar 4–7 gånger öppningstiden. |
| Vad är fel? | Sex saker, listade under [Fel](#fel), plus några [mindre avvikelser](#mindre-avvikelser). Allvarligast: sparningen som hänger, och att `$EXTMIN/$EXTMAX` både räknas fel och aldrig skrivs i en läst 2000+-ritning. |

Prototyper visar att de stora tidsvinsterna går att ta utan att ändra ett enda byte i oförändrade filer. De ligger utanför
källträdet och är inte körda mot testsviten; se [Prototyper](#prototyper).

## Underlag och metod

### Filerna

Tre verkliga DWG-ritningar (A, B och C) sparades med AutoCAD 2025:s kärna som DXF i R12, 2000, 2004, 2007, 2010, 2013 och
2018, ASCII och binärt: 42 filer. Till det kommer fyra R12-exporter av A och B som AutoCAD sparat tidigare: 46 filer, 8,9–233
MB. R13 och R14 saknas eftersom AutoCAD 2025 inte kan spara dem.

| Ritning | Innehåll | Storlek i 2018, ASCII | Storlek i R12, ASCII |
|---|---|---|---|
| A | 263 3DSOLID och 31 SURFACE i 14 block, 275 INSERT (17 382 solidinstanser), mest vridna andra vinklar än multiplar av 90° | 19 MB | 207 MB |
| B | 63 444 3DFACE, 31 584 LINE, 2 936 proxy-objekt | 37 MB | 34 MB |
| C | 1 544 POLYLINE med 131 664 VERTEX, block, 2 928 proxy-objekt | 31 MB | 20 MB |

### Mätningarna

- **Bygge:** GCC, UCRT64, Release, 24 kärnor, 64 GB. Tider är enstaka körningar utan upprepning och har cirka ±30 % brus när
  andra jobb går samtidigt. Sekventiellt mätta tider anges där de finns.
- **Oberoende räkning:** en egen enkel läsare, utan kod från HDxf, räknar posterna per sektion i ASCII-filerna.
- **Byte-jämförelse:** filen läses och skrivs oförändrad i sitt format och jämförs byte för byte.
- **Från typade värden:** alla bevarade poster (`RawRecord`) töms före skrivning, så allt måste komma från typade värden.
  Resultatet jämförs grupp för grupp med originalet: det som saknas finns bara i råposten.
- **Andra läsare:** `tools/fas9_verify_batch.py` låter AutoCAD:s kärna, ODA File Converter och ezdxf läsa original och utfiler.
- **Ändringar:** programmet ändrar en läst ritning på sex sätt (färg, flytt, lager, ta bort, lägga till …), sparar, läser
  tillbaka och kontrollerar. AutoCAD:s kärna kör AUDIT på resultatet.
- **Stress:** egna syntetiska filer, utan innehåll från ritningarna, skrivna som text utan HDxf.
- **Profilering:** gprof, stackdumpar med gdb, och klockor runt stegen. gprof fångar bara ungefär en tredjedel av tiden i
  öppningen, eftersom Windows-heapen inte räknas. Siffrorna från klockorna är de tillförlitliga.

## Fullständighet

**All data går att få ut.**

| Kontroll | Resultat |
|---|---|
| Oförändrad sparning, byte för byte | 46 av 46 identiska |
| Omläsning i andra formatet (ASCII till binärt och tvärtom) | samma antal poster i modell, block och objekt, alla filer |
| Antal poster mot den oberoende räkningen | stämmer för alla ASCII-filer |
| Skrivet enbart från typade värden | 62 174 998 av 62 175 030 grupper kvar, i 23 ASCII-filer |
| Tal i stickprov (B och C i 2018, B:s R12-export): original mot typat skrivet | 0 skillnader av 2,3 miljoner tal |
| AutoCAD, ODA och ezdxf mot original | 176 utfiler jämförda, samma antal poster. ODA läste 222 av 222 filer utan fel. ezdxf (filer upp till 60 MB): samma antal, samma antal fixar som originalet |
| Ändrade ritningar | 126 ändringar sparade och lästa tillbaka utan fel. AutoCAD öppnade 120; de 6 övriga kommer från en fil AutoCAD inte heller öppnar i original |

De 32 grupper som saknas när allt skrivs från typade värden är fem DIMSTYLE-värden i en stil och `74` i en R12-linjetyp, se
[Mindre avvikelser](#mindre-avvikelser). Övriga skillnader i det läget:

- **Ordning:** VERTEX och POLYLINE från AutoCAD:s R12-export har `8` före `5`. Typat skrivna har `5` först. Värdena är lika.
- **Tabellräknare:** i R12 får tabellens `70` antalet poster där AutoCAD har ett annat tal, vilket redan är beskrivet som ej fastställt.
- **XDATA-ordning:** typat skrivna lager och dimensionsstilar kan ha sina XDATA-block i annan ordning.
- **`$VERSIONGUID`:** ny, som väntat när något ändrats.

Vissa filer skiljer sig i den utbredning AutoCAD själv räknar fram när allt skrivs från typade värden, med högst cirka 0,01
ritningsenheter. Det beror inte på geometrin: en kopia av AutoCAD:s R12-fil där bara headerns `$EXTMIN/$EXTMAX` byttes ut ger
exakt samma avvikande värden i AutoCAD, så AutoCAD:s tal efter `ZOOM E` påverkas av vad headern säger.

## Tak

Inga fasta tak hittades i data. Tiden växer linjärt med antalet poster. Alla värden är mätta på oförändrad kod utom 4,1
GB-filen, som kräver prototyperna för en rimlig sparningstid.

| Fall | Öppna | Spara | Minnestopp | Resultat |
|---|---|---|---|---|
| LINE, 500 000 | 0,9 s | 5,4 s | 1,1 GB | rätt |
| LINE, 1 000 000 | 1,8 s | 11 s | 2,2 GB | rätt |
| LINE, 2 000 000 | 3,6 s | 22 s | 4,4 GB | rätt |
| LINE, 4 000 000 | 7,3 s | 46 s | 8,2 GB | rätt |
| POLYLINE med 5 000 000 hörn | 7,5 s | 43 s | 7,2 GB | rätt |
| 200 000 lager | 0,2 s | 1,5 s | 0,4 GB | rätt |
| 200 000 block med INSERT | 1,1 s | 13 s | 1,4 GB | rätt |
| INSERT med 1 000 000 ATTRIB | 2,2 s | 13 s | 2,4 GB | rätt |
| XDATA, 1 000 000 grupper på en post | 0,2 s | 0,6 s | 0,7 GB | rätt |
| Text på 20 MB i ett värde | 0,1 s | 0,1 s | 0,3 GB | rätt |
| Handtag med 15 hexsiffror | – | – | – | rätt |
| Fil på 4,1 GB (4 400 TEXT om 1 MB) | 10 s | 22 s | 37 GB | rätt |
| Block nästlade 700 nivåer | – | – | – | rätt |
| Block nästlade 1 000 nivåer | – | – | – | **krasch**, se fel 4 |
| Polyface med 40 000 hörn, ASCII | – | – | – | rätt |
| Polyface med 40 000 hörn, binärt | – | – | – | kapat till −25 536, utan varning, se fel 6 |

En minimal R12-fil utan `TABLES` och `BLOCKS` får dem tillagda vid sparning och blir därför inte byte-identisk. Alla filer
AutoCAD sparat har sektionerna.

Konstanter i källkoden som kan se ut som tak är beräkningsgränser och begränsar inte data: `refined.size() < 20000`,
`depth >= 80`, `out.size() >= 65536` och `pops < 100000` i `src/geometry/acis_extents.cpp`, `kMaxFaces` i `src/geometry/mesh.cpp`
(AutoCAD:s `SMOOTHMESHMAXFACE`), `kMaxDegree` 32 för ACIS-kurvor (ett högre gradtal gör att kroppen inte räknas in i
utbredningen), och djupgränsen 16 för typsnittsformer, fält och sammansatta kurvor.

## Tider

Öppna och spara i HDxf idag och med prototyperna, och AutoCAD:s kärnas öppning (öppna och avsluta, minus 0,58 s för att starta
och avsluta på en tom ritning). AutoCAD och öppningstiderna är mätta sekventiellt.

| Fil | Öppna idag | Öppna, prototyp | AutoCAD öppnar | Spara idag | Spara, prototyp |
|---|---|---|---|---|---|
| A, R12, 207 MB | 3,5 s | 2,5 s | 11,5 s | 30 s | 6–10 s |
| A, R12, binär, 121 MB | 5,7 s | 2,4 s | 9,7 s | 26 s | 10 s |
| A, 2018, 19 MB | 0,08 s | 0,07 s | 7,5 s | över 74 min* | 0,16 s |
| A, 2018, binär, 9,5 MB | 0,25 s | 0,24 s | – | över 74 min* | 0,28 s |
| A, 2007/2010 | 0,1 s | 0,1 s | – | ej mätt | 0,5 s |
| A, 2000/2004 | 0,1 s | 0,1 s | – | ej mätt | 2,5 s |
| B, 2018, 37 MB | 0,53 s | 0,38 s | 2,0 s | 3,5 s | 1,1 s |
| B, 2018, binär, 19 MB | 0,82 s | 0,35 s | 1,8 s | 3,8 s | 1,45 s |
| C, 2018, 31 MB | 0,42 s | – | 1,0 s | 3,6 s | – |
| B, R12, 34 MB | 0,47 s | – | 0,65 s | 2,5 s | – |

*Mätt en gång på den binära filen och avbruten efter 74 CPU-minuter utan att bli klar.

Summan över de 34 filer som går att spara idag: öppning 39,6 s till 20,4 s, sparning 217 s till 65 s. Binärt är lika snabbt som
ASCII med prototyperna, vid halva filstorleken, men inte snabbare.

## Fel

### 1. Sparning hänger på ritningar med vissa solider

`DxfWriter::compute_extents` körs vid varje sparning. I en läst 2000+-ritning används resultatet aldrig
(`write_header_template` skriver utbredningen bara för R12), så beräkningen är bortkastad. Den fastnar på ritning A av sex
orsaker, alla i `src/geometry/acis_extents.cpp` och `src/geometry/extents.cpp`:

1. **Exponentiell förgrening vid en pol.** En spline-yta som dras ihop till en punkt har ingen entydig uv där. Inversionen
   hoppar mellan olika u (uppmätt 0,33 och 0,47 med parameteravstånd 10⁻¹¹, ned till 10⁻¹⁷). `FaceRegion::refine` stannar bara
   när avvikelsen i uv blir liten, vilket aldrig händer. Förgreningen fortsätter tills `out.size()` når 65 536, för varje
   segment och varje fråga. Dessutom blåser hoppen upp segmentets `band` (uppmätt 0,28–0,8 i ytans enhet), så nästan alla
   segment räknas som nära i alla frågor. Det gäller 3D-punkter som redan är samma punkt, med avstånd långt under modellens
   upplösning. Sfär, kon, torus och rotationsyta har polhantering i `SurfaceParam::param`, spline-ytor saknar den.
2. **Inget återanvänds i `inside`.** Varje `inside` kör `refine` på nytt, med tre ytinversioner per steg (Newton mot spline-ytan),
   trots att samma segment ger exakt samma resultat varje gång.
3. **Den kända lådan används inte.** `BodyExtents::spline_face` börjar med ytans egen kant som bästa värde, inte med lådan
   som redan är känd. Ett kontrollnät ligger inom sitt konvexa hölje, så en yta vars hölje ligger inne i lådan kan aldrig
   ändra resultatet, men räknas ändå exakt. På ritning A gäller det 119 566 av 119 785 spline-ytor.
4. **Samma kropp tolkas om.** INSERT med annan vinkel än multiplar av 90° räknar blockets entiteter på plats, vilket är
   AutoCAD:s regel. Då anropas `AcisSource::bodies` 17 382 gånger för 294 entiteter.
5. **Varje vriden INSERT räknas fyra gånger.** `ExtentsBuilder::entity` går igenom `{0, cols - 1}` × `{0, rows - 1}`, vilket ger
   fyra varv också för en vanlig INSERT med en rad och en kolumn. Nästlade vridna block växer därför med 4ⁿ: en kedja med
   12 nivåer tar 2,5 s och 14 nivåer 39,5 s, alltså cirka två dygn vid 20 nivåer. Med ett varv per verkligt hörn tar 14 nivåer 0,02 s.
6. **Blocklådan räknas i onödan.** `const Extents& box = block_extents(*b)` körs före grenen för vridna INSERT, som inte använder den.
   Blockets låda startar tom, så höljeskontrollen hjälper lite där. På ritning A är det här den största kostnaden.

`Drawing::entity_extents` skapar en ny `ExtentsBuilder` per anrop, och varje svar ska ha sin egen låda, så en annan entitets
resultat kan inte användas som startlåda. Med alla åtgärder tar ett anrop i snitt 0,43 s per INSERT på ritning A, högst 2,6 s.
Ett batchanrop skulle bara spara tolkningen av kropparna.

Mätt effekt på hela ritningens utbredning, ritning A i 2018:

| Variant | Tid |
|---|---|
| Idag | avbruten efter 74 CPU-minuter |
| Polkontroll i `param` (Su eller Sv nära noll längs hela isolinjen) plus sparade `refine`-resultat | fastnar på en senare solid |
| Enbart 3D-stopp, med eller utan sparade `refine`-resultat | över 280 s |
| Enbart höljeskontroll | över 280 s |
| 3D-stopp och höljeskontroll | 71 s |
| 3D-stopp, höljeskontroll och sparade `refine`-resultat | 17–20 s |
| Dessutom kanterna först och varje kropp tolkad en gång | 14 s |
| Dessutom ett varv per verkligt INSERT-hörn | 12,6 s |
| Dessutom ingen blocklåda för vridna INSERT, och bara ytor i pass två där pass ett gått samma väg | **0,49 s** |

Tidsfördelning i den sista varianten: kanterna 0,42 s, plana ytor hoppas över (17 799), koner 0,01 s (6 380), toroider i lådan (32),
spline-ytor i lådan 0,01 s (29 633). Ingen spline-yta behöver räknas exakt, och kroppstolkningen tar resten.

3D-stoppet och de sparade `refine`-resultaten används därmed inte alls för hela ritningens utbredning på ritning A. De behövs
när en yta sticker ut ur den kända lådan, vilket händer för `Drawing::entity_extents` på en enskild INSERT och för
en blocklåda.

Alla varianter av ritning A i 2000–2018 ger samma resultat, och alla sex värden stämmer med `$EXTMIN/$EXTMAX` som AutoCAD sparade i
filen, på 15 siffror, när också fel 2 är rättat.

### 2. `$EXTMIN/$EXTMAX` räknas fel och skrivs aldrig i en läst 2000+-ritning

Två delfel:

- **Osynliga entiteter räknas med.** `ExtentsBuilder::entity` tittar inte på `EntityProperties::visible` (`60 = 1`). I ritning A
  har ett block en osynlig solid och en osynlig polylinje, vilket ger Z-spannet knappt 100 enheter och en större Y-gräns i
  stället för AutoCAD:s cirka 15 enheter. När de utesluts blir alla sex värden exakt AutoCAD:s. AutoCAD:s R12-export tappar
  osynligheten, så R12-filens värden stämmer med HDxf:s felaktiga och är inget giltigt facit för detta. Kontrollerat i
  AutoCAD med `REGEN` och `ZOOM E` på DWG:n och på 2000- och 2018-filerna: inga lager är frysta eller släckta.
- **Ändrad geometri skriver inga nya värden.** En linje långt utanför ritningen ändrar `$EXTMAX` i R12, men inte i 2000–2018.
  Kontrollerat på oförändrad kod. Regeln för R12 finns i `write_header_template`: filens värden står kvar tills
  geometrin i den rymden har ändrats, och då skrivs nya.

### 3. Proxy-objekt går inte att läsa typade

AutoCAD skriver `bits/8 + 1` byte i grupp 310/311 när bitantalet i 161/162 är jämnt delbart med 8 och större än noll.
`bytes_for` i `src/records/proxy_record.cpp` kräver `(bits + 7) / 8`, så posten underkänns och blir otypad med en varning. Uppmätt i
en fil: alla 384 poster med jämnt delbart bitantal över noll följer regeln, och 4 med 0 bitar har 0 byte. Den extra byten är
oftast `00`, ibland annat (`CC`, `FF`). Antal varningar över alla filer: 7 144, 152–371 per fil. Prototypen ger 0 varningar
och fortsatt byte-identiska filer.

### 4. Djupt nästlade block kraschar processen

`ExtentsBuilder::block_extents` och `ExtentsBuilder::entity` är rekursiva. Mellan 700 och 1 000 nivåer tar stacken slut
(`0xC00000FD`) i huvudtråden. I en tråd med mindre stack kommer det tidigare. Läsning klarar 100 000 nivåer. Sparning är
provad hela vägen bara upp till 700 nivåer, så andra rekursiva vägar kan finnas: `MLeaderDrawer` och `TableProxy` ritar
block rekursivt, och `table_block_extents` startar en ny beräkning. Vridna block hänger långt tidigare, se fel 1, punkt 5.

### 5. VERTEX följer inte POLYLINE

När ett program ändrar en POLYLINE:s färg eller linjetyp behåller dess VERTEX och SEQEND sina värden, så AutoCAD:s AUDIT rapporterar
ett fel per hörn: `AcDb3dPolylineVertex … color != owner's … set to owner's`, och motsvarande för linjetyp. Uppmätt i ritning C:
131 664 fel för färg och cirka 86 000 för linjetyp, utöver filens egna 300. AutoCAD rättar felen själv och ritningen går att
öppna. Lager ger inga AUDIT-fel, men VERTEX behåller även där sitt gamla lager.

### 6. 16-bitarsgrupper över gränsen kapas utan varning, och skiljer sig mellan formaten

`GroupWriter::write_int16` kapar värdet med `static_cast<int16_t>` i binärt: polyface med 40 000 hörn får grupp 71 = −25 536,
och 70 000 blir 4 464. ASCII skriver talet som det är. Kapningen i sig är AutoCAD:s egen form. AutoCAD:s kärna som öppnat egna filer och
sparat dem skriver, i ASCII och binärt, R12 och 2018:

- Polyface med 40 000 hörn: `71 = -25536`, och ytindex `-25536`, `-25537`, `-25538`.
- Tabell med 40 001 lager: `70 = -25535`. Alla lager finns kvar i filen.

Felen i HDxf är alltså två: en typad ASCII-fil skriver något annat än AutoCAD, och programmet får inget veta när värdet ändras.
I en polyface har ett kapat index dessutom ny betydelse, eftersom negativt index betyder osynlig kant. Att vägra spara skulle
införa ett tak som AutoCAD inte har. Kärnan kraschar vid avslut efter att ha sparat tabellen med 40 000 lager.

## Flaskhalsar

### Öppning

Mätt med klockor, ritning A i R12 (2,5 s med prototyperna):

| Steg | ASCII | Binärt |
|---|---|---|
| `DxfReader::read` | 2,10 s | 1,87 s |
| `preserved_handles` | 0,47 s | 0,59 s |

Övriga steg i `Drawing::load` tar under 0,01 s.

Orsaker, från de största:

- **Tal via text i binärt:** `read_binary` gör om varje double till text med `snprintf("%.17g")`, och läsaren tolkar den
  tillbaka med `atof`. Båda är långsamma i MinGW. Det är därför binärt är långsammare idag. `std::to_chars` med precision 17
  ger exakt samma text, och `std::from_chars` samma värde.
- **`snprintf` per ASCII-gruppkod:** `read_ascii` formaterar koden för att kontrollera bredden, också i normalfallet tre kolumner.
- **`istream` tecken för tecken i binärt:** `in.get` per tecken i strängar, sentry per värde.
- **`preserved_handles` packar upp allt:** `RawRecord::groups()` bygger en hel vektor med strängar per post, bara för att
  hitta handtaget som står först.
- **Polylinjen kopieras:** `entity.data = d` kopierar alla hörn. Det finns på 17 ställen i `src/read/dxf_reader.cpp`. Kopian klipper
  dock vektorns överkapacitet: med `std::move` steg minnestoppen från 810 till 1 061 MB, så flytten behöver kombineras med
  rätt kapacitet.
- **Många små allokeringar:** `RawRecord::append` växer strängen flera gånger per post. Gissningsvis huvuddelen av den tid gprof
  inte ser, men inte mätt separat.

Binärt blir klart snabbare än ASCII först när talen läses utan att gå via text. Det kräver att `GroupCode` kan bära ett tal
och att `RawRecord` kan lagra det binärt, vilket är en större ändring och inte ingår i prototyperna.

### Sparning

Mätt med klockor, ritning A i R12, med prototyperna: medel av en ASCII- och en binär sparning, cirka 8,6 s.

| Steg | Tid |
|---|---|
| Läsa in hela ritningen igen (`template_text` plus `DxfReader::read`) | 2,8 s |
| Kontroller och handtag | 0,4 s |
| `DxfWriter::write` | 5,3 s |
| Radslut (CRLF) | 0,1 s |

Orsaker, från de största (antal från en körning med två sparningar):

- **`merge_record` körs även när posten är oförändrad.** `emit` och `emit_r12` skriver posten två gånger, jämför `n == o`,
  och kör ändå hela sammanslagningen: 3,9 miljoner anrop, 1,76 miljarder strängjämförelser (902 miljoner i `list_code`, 546
  miljoner i `list_groups`, 273 miljoner i `merge_record`), 82 miljoner uppslag i `std::map<std::string, int>` och 137 miljoner i
  `unordered_map<std::string, …>` med hopslagna nycklar. Det här är utan jämförelse störst, och uteblir om en oförändrad post
  skrivs direkt från sin råpost.
- **Hela ritningen läses in igen.** `Drawing::write_dxf` bygger en andra `Drawing` ur råposterna för att ha något att jämföra med.
- **`compute_extents`,** se fel 1. I R12 räknas dessutom både nuvarande och ursprunglig ritning, vid varje sparning.
- **Texten kopieras:** `oss.str()` kopierar, CRLF skrivs tecken för tecken, och `save_to_memory` kopierar en gång till till `vector`.

### Minne

Minnet är det enda verkliga taket i HDxf. Prototyperna minskar det knappt. Raderna för A i 2018 och 4,1 GB är mätta med
prototyperna, eftersom sparningen annars inte blir klar eller tar orimlig tid; övriga utan.

| Fall | Filstorlek | Efter öppning | Topp vid sparning |
|---|---|---|---|
| A, R12 | 207 MB | 810 MB (4×) | 2,6 GB (13×) |
| B, 2018 | 37 MB | 168 MB (5×) | 480 MB (13×) |
| B, 2018, binär | 19 MB | 168 MB (9×) | 480 MB (25×) |
| A, 2018 | 19 MB | 44 MB (2×) | 190 MB (10×) |
| 1 000 000 LINE | 85 MB | 1,1 GB (13×) | 2,2 GB (26×) |
| 4,1 GB text | 4,1 GB | 12 GB (3×) | 37 GB (9×) |

Orsaker:

- `sizeof(Entity)` är 736 byte, varav `EntityProperties` 360. `Vertex` är 304 byte, `Object` 936.
- Varje post har dessutom sin råpost.
- Sparning bygger en andra `Drawing`, och resultatet finns i upp till tre kopior.

Att minska `Entity` och `Vertex` påverkar det publika API:t, eftersom fälten är publika `std::string`.

## Filerna själva

Flera av AutoCAD:s egna filer har fel som inte kommer från HDxf:

- **AutoCAD öppnar inte 3 av sina egna original.** Två R12-filer av ritning C (ASCII och binär): `Unknown group 74 for table`, från
  en linjetyp i tabellen. En R12-export: `Improper text style name`. HDxf läser dem och bevarar dem byte för byte.
- **AUDIT hittar fel i originalen.** 300 fel i 24 filer, i ordböcker för tilläggsobjekt som kärnan inte har laddat, och 2 fel
  i 3 R12-filer (tomt textstilsnamn). HDxf:s utfiler ger samma antal.
- **Låg precision i R12.** Solider som AutoCAD exporterar som polylinjer i ASCII-R12 skrivs med sex siffror (som `1.23456` och
  `1.23456e-14`). Den binära filen från samma ritning har full precision, vilket ger cirka 1,9 miljoner skillnader per
  koordinat mellan AutoCAD:s egna ASCII- och binärfiler. R12-formen har också `8` före `5`.
- **Headern i en sparad DXF.** `$EXTMIN/$EXTMAX` är AutoCAD:s sparade värden. Efter `REGEN` och `ZOOM E` ger AutoCAD något
  annorlunda tal, i storleksordningen 10⁻⁵ ritningsenheter. Facit för utbredningen är därför de sparade värdena.

## Mindre avvikelser

De här påverkar bara poster som skrivs nya, inte oförändrade poster.

- **`\U+` i namn i 2000/2004:** `dxf_writer_2000.cpp` sätter kodsidan till 0, så allt utanför ASCII blir `\U+XXXX`. AutoCAD:s facit
  skriver namn med kodsidans bytes (`Lager_åäö_\U+65E5…`) och MTEXT med `\U+` genomgående. AutoCAD läser båda rätt: en LINE
  med lagernamnet i `\U+`-form hamnar på rätt lager, kontrollerat med `LIST`.
- **DIMSTYLE:** värden lika med HDxf:s standardvärde utelämnas (`41`, `44`, `140`, `141`, `147`), fast AutoCAD skriver dem i en
  stil. Effekten i AutoCAD är inte testad. Det typade värdet är rätt.
- **R12-linjetyp:** `74 = 0` i ett mönsterelement utelämnas.
- **SURFACE i 2000/2004:** en yta som HDxf inte kan rita isolinjer för går inte att skriva utan sin råpost; sparningen ger ett
  namngivet fel. Omfärgning och flytt av andra entiteter fungerar.

## Prototyper

Allt nedan är provat som kopior av källfilerna, länkade före biblioteket. Inget är kört mot testsviten.

| Prototyp | Var | Vad | Effekt |
|---|---|---|---|
| Hoppa över oanvänd utbredning | `compute_extents` | Returnera direkt i `template_mode() && !r12_template()` | Oförändrad sparning från över 74 min till 0,16 s (A, 2018) |
| Ett varv per hörn | `ExtentsBuilder::entity` | Index 1 i varje riktning bara när `cols > 1` respektive `rows > 1` | Vriden kedja med 14 nivåer: 39,5 s till 0,02 s |
| Ingen blocklåda för vridna | `ExtentsBuilder::entity` | `block_extents` bara i grenen för multiplar av 90° | 12,6 s till 0,49 s, tillsammans med nästa rad |
| Bara ytor i pass två | `BodyExtents::run`, `ExtentsBuilder` | Pass två räknar inte kanterna där pass ett redan gått samma väg | Se raden ovan |
| 3D-stopp | `FaceRegion::add_coedge`, `FaceRegion::refine` | En bit vars ändpunkter ligger närmare än `max(1e-9 × kroppens storlek, 8 × eps × största koordinat)` är en punkt: förfinas inte. Storleken räknas en gång, från kroppens hörn | Explosionen borta |
| Sparade `refine`-resultat | `FaceRegion` | Ett binärt träd per segment med `dev` och mittpunkt. Resultatet beror bara på vägen genom trädet, så svaret är bit för bit detsamma | 71 s till 17–20 s |
| Höljeskontroll | `BodyExtents::face`, `box_holds`, `spline_face`, `knot_lines` | Ytan hoppas över om hela kontrollnätet (positiva vikter) ligger i den kända lådan. `best` startar från lådan. Kandidater i lådan testas inte med `inside` | 119 566 av 119 785 ytor slipper arbete |
| Kanterna först | `ExtentsBuilder`, `acis_extents` | Ett första pass med bara kropparnas kanter och INSERT. Lådan är en delmängd av den sanna, så den är en giltig startlåda | 17 s till 14 s tillsammans med raden under |
| En tolkning per kropp | `ExtentsBuilder` | Karta från `const Entity*` till tolkade kroppar | 17 382 tolkningar till 294 |
| Osynliga utesluts | `ExtentsBuilder::entity` | `if (!e.props.visible) return;` | Alla sex värden lika AutoCAD:s |
| Oförändrad post | `emit`, `emit_r12` | När `n == o` och inget har tagits bort ur ritningen skrivs råposten direkt | R12: 30 s till 6–10 s; 46 av 46 byte-identiska |
| Talen | `group_io.cpp`, `dxf_reader.cpp` | `to_chars` för double, `from_chars` i stället för `atof`, `snprintf` bara när koden inte har tre kolumner, `getline` för strängar, `sgetn` i stället för `read` | Binär öppning 5,7 s till 2,4 s, ASCII 3,5 s till 2,5 s |
| Texten | `write_dxf` | `std::move(oss).str()`, CRLF med `memchr` och förallokering | Liten |
| Proxy | `proxy_record.cpp` | Godta också `bits/8 + 1` byte när `bits % 8 == 0` och `bits > 0`, på tre ställen | 7 144 varningar till 0 |

Prototypens `nothing_removed` är en lokal lösning eftersom klassens medlemmar inte fick ändras, och ska inte kopieras.

## Inte testat och osäkert

- **Testsviten:** prototyperna är inte körda mot projektets svit, och inte mot AutoCAD:s facit i `doc/facit`.
- **3D-stoppets tolerans** är ett eget val. Den är prövad bara på ritning A, men där mot AutoCAD:s värden. Att en kandidat nära
  en kant bedöms fel ändrar utbredningen bara i andra ordningen, eftersom ytan är plan i den riktningen vid ett extremvärde.
- **Polfallet i ett litet eget prov:** det är inte återskapat. En kon av `LOFT` till en punkt och samma kon genomskuren av `SLICE`,
  båda sparade av AutoCAD:s kärna, hänger inte idag, eftersom kanterna inte går längs polen.
- **Polkontrollen** i `param` provades men räckte inte, och är inte med i slutvarianten.
- **Andra rymden:** osynliga entiteter i paper space är inte kontrollerade. ATTRIB med osynlig-flaggan (`70`) är inte kontrollerade.
- **Visuell kontroll** i AutoCAD har inte gjorts: kärnan ritar inget.
- **R13 och R14:** inga sådana filer från dessa ritningar, eftersom AutoCAD 2025 inte kan spara dem.
- **Övriga plattformar:** allt är mätt på Windows med MinGW. Vinsten av att ersätta `snprintf` och `atof` kan vara mindre på Linux.
- **Sparning av A i 2000/2004:** 2,5 s mot 0,16 s i 2018. Orsaken är inte utredd.
- **Sparning av A i 2000–2013 utan prototyperna:** ej mätt; samma kodväg som 2018.
