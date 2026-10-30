# Rättningsfas — inga förenklingar (efter 1.3.0)

**Status:** klar 2026-10-24 (R.1–R.9), utgiven som 1.3.1 (tagg `v1.3.1`). En Codex-granskning
av R.7 och R.9 vid fasens slut (fyra fynd, alla rättade: måttblock i måttets OCS, spline-ytors
knutlinjer, ytor begränsade av sömmar), och därtill exakta rotations- och summaytor och ett
exakt test av om en punkt ligger i en yta. TrueView-filerna testar Erik efter fas 9.
**Bakgrund:** Erik, 2026-10-23, efter fas 2: "INGA förenklingar, vi ska göra detta rätt." Allt
där HDxf räknar ungefärligt, skriver annorlunda än AutoCAD eller ger upp rättas innan fas 3.
Grundregeln gäller: AutoCAD:s egna filer och Autodesks DXF-referens avgör.

## Steg

| Steg | Vad | Varför |
|---|---|---|
| R.1 | Heltal högerjusterade som AutoCAD | HDxf skrev `0`, AutoCAD `     0` |
| R.2 | DIMSTYLE i 2000/2004 med bara det som skiljer från AutoCAD:s standard | AutoCAD skriver inte alla värden |
| R.3 | Ny ritning med Layout1 och Layout2 | AutoCAD:s nya ritning har båda |
| R.4 | `$LASTSAVEDBY` som AutoCAD (användarnamnet) | HDxf lämnade den tom |
| R.5 | Alla kodsidor AutoCAD använder (`$DWGCODEPAGE`) | Bara ANSI_1250–1258 gick att läsa |
| R.6 | Exakta extents för SPLINE och spline-kanter | HDxf provtog 64 punkter per intervall |
| R.7 | Måttblock (`*D`) som AutoCAD bygger dem | Förenklad geometri, alla DIMSTYLE-variabler följdes inte |
| R.8 | Typsnitt (SHX, TrueType): text-extents och MTEXT `42`/`43` | Text räknades på insättningspunkten |
| R.9 | ACIS (3DSOLID/REGION/BODY) i extents | Bidrog inte alls |

Varje steg: tester, B7 (ODA, ezdxf, strukturkontrollen), `codex_review`, commit och push.
TrueView-filerna samlas till Erik i slutet. Det som bara riktiga AutoCAD kan avgöra (bred
polylinjes extents, ordningen `48`/`370`/`390`, gråskalorna i ACI-paletten) står kvar som
kontrollpunkter tills AutoCAD är installerat.

## Utfall

**R.1 (2026-10-23).** AutoCAD:s ASCII-filer (R12, 2000, 2004, 2018) högerjusterar heltal:
16- och 8-bitars tal och bool i 6 tecken (`70`, `280`, `290`, `370`, `1070` …), 32-bitars i 9
(`90`, `420`, `440`, `1071`), 64-bitars i 18 (`160`). `GroupWriter` gör nu så, också för
heltal som ges som text (okända typers grupper). Mallens text skrivs som förut. Alla golden
(R12, 2000, 2004) omgenererade — bara justeringen skiljer; ODA och ezdxf godkänner dem.
Testernas läsare jämför heltal som tal (`dxf_structure::number_text()`). 354 tester gröna.

**R.2 (2026-10-23).** DIMSTYLE i 2000/2004 skrivs som AutoCAD: `2`, `70`, sedan bara de
variabler som skiljer från AutoCAD:s egna värden (`autocad_dim_style()`, samma som läsaren
utgår från för R13+), i stigande kodordning, `340` (textstil) alltid och `341`–`344` när de är
satta — som AutoCAD:s `Standard` (`2 70 340`), `ISO-25` och `custom_style_ltypes` i ACadSharps
filer. Förut skrevs alla 60 variablerna, och flyttalen före heltalen. En läst stil som inte
ändrats skrivs med mallens text som förut. HDxf:s standardvärden för en ny stil (`DimStyle{}`,
metriska storlekar sedan R12) ändras inte — de skrivs nu som det de skiljer sig med. Golden 2000
och 2004 omgenererade; ODA och ezdxf godkänner dem.

**R.3 (2026-10-23).** En ny 2000/2004-ritning har Model, Layout1 (`*Paper_Space`) och Layout2
(`*Paper_Space0`, BLOCK med `67 1`, egen BLOCK_RECORD), som AutoCAD:s nya ritning (NextGIS
`line_2004.dxf`). Layout2 och Model har extents 0, som där; Layout1 de tomma `1E+20`/`-1E+20`.
`add_layout()` ger därför `*Paper_Space1` för den första egna layouten. Golden 2000 och 2004
omgenererade; ODA och ezdxf godkänner dem.

**R.4 (2026-10-23).** `$LASTSAVEDBY` (2004) får inloggningsnamnet (`USERNAME` på Windows,
`USER`/`LOGNAME` annars), som AutoCAD vid varje sparning: i en ny ritning, och i en läst ritning
som ändrats. En oförändrad rundtur behåller filens värde (B12); ett namn programmet själv satt
(`set_header_string`) står kvar. Golden-testerna sätter `HDxf` så filerna inte beror på vem som
kör dem. Test: `write_2004_last_saved_by`.

**R.5 (2026-10-23).** Alla kodsidor AutoCAD skriver i `$DWGCODEPAGE`: ANSI_874 och
ANSI_1250–1258, DOS437/850/852/855/857/860/861/863/864/865/866/869 (även som `CP…`),
ISO8859-1–10, MACINTOSH, ASCII, och de dubbelbytiga ANSI_932 (DOS932), ANSI_936 (GB2312),
ANSI_949 (KSC5601), ANSI_950 (BIG5), ANSI_1361 (JOHAB). Tabellerna genereras ur Windows egna
mappningar (`MultiByteToWideChar` — det AutoCAD använder, med Windows tillägg som CP936
`0x80` = € och de privata områdena i 936/950) av ett eget skript, `tools/gen_code_pages.py`,
till `src/code_pages.cpp`; ISO8859-10, som Windows saknar, ur Unicodes mappning. Läsning: råa bytes (en byte, eller ett par), och AutoCAD:s escape
`\M+nXXXX` (n 1 Shift-JIS, 2 Big5, 3 Wansung, 4 Johab, 5 GB) i alla filer. Ett pars andra byte
kan vara `^` eller `\` — caret-avkodningen hoppar över den. Ett odefinierat tecken blir U+FFFD.
Skrivning (R12): ett DBCS-tecken som `\M+nXXXX`, som AutoCAD, så filen är ASCII; en sida som
saknar tecknet `\U+XXXX`. Codex-granskning: tabellerna för omvänd uppslagning byggs nu en gång,
trådsäkert; tabellerna kommer från Windows i stället för Pythons codecs (som saknar Windows
tillägg); caret-kodningen tillbaka (bevarad binär post skriven som ASCII) hoppar också över ett
pars andra byte. Tester: `test/test_code_pages.cpp`. 361 tester gröna.

**R.6 (2026-10-23).** SPLINE och spline-kanter i HATCH får exakta extents i stället för 64
provpunkter per knutintervall: styrpunkterna transformeras först (NURBS är affint invariant),
varje intervall görs om till ett rationellt Bézier-segment (blossom-värden), och per axel
hittas extremvärdena där derivatans täljare `A'W - AW'` är 0 (en egen polynomrotsökare,
bisektion till sista biten mellan derivatans rötter); punkterna utvärderas med de Casteljau i
homogena koordinater. En spline med bara anpassningspunkter räknas på kurvan genom dem — kubisk,
kordalängd ("Chord", AutoCAD:s standard), ändtangenterna som derivata, en ände utan tangent
naturlig (andraderivata 0), bandat ekvationssystem (linjärt minne och tid) — inte på punkterna.
Prövat: kubisk Bézier med extremvärde exakt 1/√3, full NURBS-cirkel exakt ±2, punkter på en
linje, 20 000 anpassningspunkter. Codex-granskning: naturliga ändar i stället för not-a-knot,
stabil utvärdering för extrema vikter, bandad lösning — rättat. Kontrollpunkt för AutoCAD: dess
egna randvillkor utan tangenter står inte i referensen. 363 tester gröna.

**R.8, del 1 (2026-10-23, steget pågår).** Typsnitten läses som AutoCAD läser dem
(`src/fonts.cpp`): TrueType (glyf, cmap 4/12, samlingar `.ttc` per ansikte), OpenType med
CFF-konturer (Type 2-charstrings med subrutiner, CID-typsnitt med FDSelect; lådan exakt ur
kurvorna), SHX (shapes 1.0/1.1, unifont, bigfont). Texthöjden är höjden av `H` (Arial 1466/2048,
som AutoCAD:s MTEXT), underhänget usWinDescent (det GDI ger) respektive SHX-fontens `below`.
Former i formfiler kan hittas på namn (SHAPE). Typsnitt hittas i `font_paths()`, och på familj
med fet/kursiv som Windows väljer (namn-ID 1/16, OS/2-vikt och kursiv). Stilens typsnittsfamilj
(XDATA `ACAD 1000 <familj> 1071 <flaggor>`, som AutoCAD skriver den) är nu `TextStyle::font_family`
/`font_flags`, och övrig XDATA ligger i `TextStyle::xdata`. Förut försvann XDATA när en läst stil
ändrades. ACAD skrivs sist, som i AutoCAD:s fil. I R12 skrivs en TrueType-stil med `txt`, som
AutoCAD gör (ACadSharps `sample_AC1009`), och det ger en varning. Annan stil-XDATA går inte att
skriva som R12. Tester: `test/test_fonts.cpp`. 369 tester gröna.

**R.8, del 2 (2026-10-24, steget klart).** Text räknas i extents med sitt typsnitt
(`src/text_layout.cpp`, `src/mtext_layout.cpp`). Facit är AutoCAD:s egna filer: 2018-filens MTEXT
`42`/`43` och R12-filen av samma ritning, där AutoCAD skrev varje MTEXT-rad som TEXT — vår
layout ger exakt (1e-9) samma rader för tio MTEXT (bilaga 1/2/5/6/7, radbrytning vid bredd,
123 rader, centrerat, bläck över höjden som `$`, `%`, siffror) och samma `42`/`43`. Det AutoCAD
gör: en MTEXT-rad placeras efter sitt bläck (vänster, mitt, höger; mellanslag räknas som
pennans väg), textens topp är första radens bläcktopp, nästa baslinje (minst) `f·5/3·H − H +`
nästa rads bläcktopp, (exakt) `5/3·h·f` och texten slutar en femtedel därav under sista
baslinjen, botten (minst) sista radens bläckbotten; `42` är bredaste raden. AutoCAD 2000/2004
skriver inte `42`/`43` (bara 2018:s inbäddade objekt), så HDxf skriver dem inte heller — de finns
i API:t: `Drawing::mtext_size()`, och en entitets extents `Drawing::entity_extents()`.
Formatkoderna (`\P \N \X \~ \L \O \K \C \c \A \H \W \T \Q \f \F \S \p`, klamrar, `%%`, `\U+`),
radbrytning vid mellanslag (ett för långt ord bryts inte, som Autodesk dokumenterar), `\T` som
multipel av avståndet mellan tecknen (AcGi), staplat enligt Autodesks beskrivning (`/` centrerat
med streck, `^` vänster, `#` diagonalt; `\A`; ingen egen 70 % — AutoCAD skriver `\H0.7x`), `\px`
i MTEXT-höjder, kolumner (`ACAD_MTEXT_COLUMN_INFO`), extents med bredden texten sätts i (41) och
bakgrundens marginal. TEXT/ATTRIB/ATTDEF: bläcklådan placerad av justeringen, Middle (`72 4`)
efter tecknens mitt, Bottom efter typsnittets underhäng, Aligned/Fit, spegling, lutning,
rotation, `%%c/d/p/u/o/%%%/%%nnn`; en konstant ATTDEF i ett block visas med sitt värde. Typsnitt
numrerade efter kodsidan (shapes 1.0) får `%%d/p/c` som 127/128/129, bigfont tecknets bytepar i
`$DWGCODEPAGE`. SHAPE ur stilens formfil. TOLERANCE: rader DIMTXT + 2·DIMGAP höga, rutor bläcket
± DIMGAP (gånger DIMSCALE, DSTYLE-överstyrning) — Autodesk anger inte måtten; ODA File
Converters ramar för ACadSharps tre (R12-utdata) stämmer exakt med Arial och inom 0,01 för GDT.
Ett typsnitt som saknas ger en varning (stilens, bigfontens, MTEXT:s `\f`/`\F`, formfil) och
simplex.shx (FONTALT), annars insättningspunkten. Golden-filerna sparas utan typsnitt så de är
lika på alla maskiner. Hela ACadSharp-ritningen (2000/2004) får samma `$EXTMIN`/`$EXTMAX` x/y som
AutoCAD skrev (z väntar på ACIS, R.9). Codex research (Autodesk-dokumentation och -forum) och
Codex-granskning: tabbar räknas vid radbrytning, linjär radbrytning, lutning på över-/under-
strykning, `\A`, `\U+` och tal oberoende av locale — rättat; exakt radavstånd till bläcket
avvisat (AutoCAD:s `43 = 3.0` visar annat). B7: ODA läser filerna utan fel. 380 tester gröna.

**R.7 (2026-10-24, steget klart).** Del 1: DIMSTYLE får DIMTFILL, DIMTFILLCLR, DIMLTYPE,
DIMLTEX1/2, DIMFXLON, DIMFXL, DIMJOGANG och övrig XDATA. AutoCAD 2000/2004 har dem som XDATA
(`ACAD_DSTYLE_*`, `1070 <variabelns nummer>`, värdet; färgen som binär AcCmColor
`18000000 01000000 <0xMMRRGGBB>`, linjetyperna som handtag, DIMJOGANG i radianer), 2007+ som
gruppkoder (`69`, `70`, `345`–`347`, `290`, `49`, `50`) — båda läses, 2000/2004 skrivs som
AutoCAD. En 2007-DIMSTYLE:s andra `70` är DIMTFILLCLR, inte flaggorna (förut skrev den över
dem). Skrivarna lägger till APPID för varje XDATA-applikation som används men saknas i tabellen.

Del 2: måttblocken byggs som AutoCAD bygger dem (`src/dimension_block.cpp`). Facit är AutoCAD:s
egna block i ACadSharps `sample_AC1015`/`AC1018` (11 mått: linjära, vridna, justerade,
vinkel med två linjer och tre punkter, radie, diameter, koordinat, `custom_dim_style` med
fyllning, egna linjetyper, bredder och färger) — varje entitet i AutoCAD:s ordning med typ,
lager, färg, linjebredd, linjetyp, transparens (2004: ByBlock), geometri (1e-9), text, bakgrund
och XDATA, och DIMENSION:s `10`/`11`/`16`/`42`, i både 2000 och 2004. Utöver proverna är ODA File
Converters ombyggda block facit (de stämmer med AutoCAD:s för alla prover): ett eget testprogram
med 251 fall (passning, DIMATFIT 0–3, DIMTIX, DIMSOXD, DIMTOFL, DIMTAD 0–4, DIMTIH/DIMTOH,
DIMJUST, användarplacerad text, DIMTMOVE, sneda måttlinjer, DIMDLE/DIMTSZ, alla 20 inbyggda
pilar, DIMSE/DIMSD, DIMGAP < 0, toleranser och gränser, alternativa enheter, alla längd- och
vinkelenheter, DIMRND, DIMZIN/DIMAZIN, DIMPOST, radie/diameter inne och ute, DIMCEN ±,
vinkel åt alla håll, koordinat med och utan knyck) — samma block i alla utom fyra, där ODA
avviker från AutoCAD (ODA saknar DIMFXL; ODA skriver `{\H1.000000x;…}` där AutoCAD skriver
`\S…;`). Det AutoCAD gör: texten är MTEXT (mitten, `\A<DIMTOLJ>;` före; 2000 med radavståndet
som XDATA `MTEXTBEGIN`, och DIMTFILL som `ACAD_MTEXT_BBRT`; 2004 bakgrunden i MTEXT:s egna
koder), staplade bråk i 2000 `\S1/4;`, från 2004 `{\H1x;\S1#4;}`; textens plats ett mellanrum
och halva höjden ovanför linjen; vad som får plats mellan hjälplinjerna (texten sin längd och ett
mellanrum på var sida, pilarna sina och ett mellanrum, texten i linjen båda och två mellanrum
till); text utanför bortom hjälplinje 2 (en pilstorlek och ett mellanrum); pilarnas svansar
utanför; radie/diameter med knyck och landning för vågrät text; vinkelbågens punkt `16` (`10`
för tre punkter) en tredjedel längs bågen; koordinatmåttets knyck över två pilstorlekar fyra
pilstorlekar från änden. Fylld pil är SOLID med hörnen i AutoCAD:s ordning, de andra INSERT av
sitt block skalat med DIMASZ; de inbyggda pilblocken skrivs med ritningen (2000: LWPOLYLINE med
bredd, R12: POLYLINE; allt ByBlock). Definitionspunkterna är POINT på lagret `Defpoints`, som
skapas (`70 0`, i 2000 utan plottning). Ett mått i ett block får sitt block, `*Dn`-namnen undviker
befintliga, och ett mått vars block saknas (borttaget) får ett nytt i stället för en referens
till ingenting. R12: texten som TEXT. Golden (R12, 2000, 2004) omgenererade: förut gav den gamla
motorns båge extents ner till y −206, nu ritningens verkliga −160. ODA läser alla utan
anmärkning. Tester: `test/test_dimensions.cpp` (AutoCAD:s 44 mått i fyra filer, ODA-fall, pilblock,
Defpoints, textformer, saknat block). 386 tester gröna.

Kontrollpunkter för AutoCAD (R.7): 2000:s form för DIMFRAC 0 (ACadSharp har bara 1); diagonal
staplings mått inuti (bara ODA); 2004:s toleransform; DIMFXL (ODA saknar den, AutoCAD:s prov har
den inte påslagen); LINE-riktningen (AutoCAD:s `*D11` och ODA skiljer i ett fall); R12:s
måtttext (AutoCAD skriver ibland `*U`-INSERT, ibland TEXT); R12 DIMENSION `3`.

Kontrollpunkter för AutoCAD (inte i AutoCAD:s filer eller Autodesks dokumentation): TEXT
Center/Right/Aligned/Fit efter pennan eller bläcket; var `%%u`/`%%o`/`\L`/`\O` ligger;
staplingens avstånd och diagonalens bredd; standardtabbar (4 h, rapporterat men inte
dokumenterat); exakt radavstånd med faktor ≠ 1 och minst-avstånd med `\H` större; MTEXT
`72 = 3` (uppifrån och ner: AutoCAD:s 2018- och R12-utdata av samma text stämmer inte med
varandra, HDxf lägger ut vågrätt); stil med vertikal text (70 bit 4) läggs ut vågrätt; om
extents tar med bakgrunden och bredden 41 (rapporterat för en version); TOLERANCE-måtten.

**R.9 (2026-10-24).** 3DSOLID, REGION och BODY räknas i `$EXTMIN`/`$EXTMAX` efter sin
B-rep, exakt (`src/acis.cpp`, `src/acis_extents.cpp`; i `src/extents.cpp` bara ACIS-fallet
och ett parameterintervall för R.6:s `add_nurbs`). Läsningen följer Spatials "SAT Save File
Format" (ACIS 7.0, med `versions.hxx`): SAT ur 1/3-raderna (R13–2010, varje tecken utom
mellanslaget skrivet som 159 − c — kontrollerat mot AutoCAD:s filer, ASCII och binärt) och
SAB ur ACDSDATA (2013+: `320` handtaget, `ASM_Data` i `310`), både `ACIS BinaryFile` (21800)
och `ASM BinaryFile4` (22300); ODA:s SAB med data som text (tagg 18) också. Versionerna 106
(R13/R14), 400 (2000), 600, 700 (ODA, Inventor) och AutoCAD:s 20800/21200/21500 (2004/2007/
2010): sekvensnummer, `#` direkt följt av nästa post (AutoCAD skriver `… #eye_refinement`),
`@n`-strängar, `{ ref n }`, ASM:s procedurella subtyper (`exact_int_cur`, `cyl_spl_sur`,
`sum_spl_sur` — approximationen efter subtypens egna data). Topologin efter ägarpekarna
(kropp → klump → skal → ytor → öglor → coedges → kanter; trådar), kroppens transform (rader,
skala). Kanterna exakt: linjer i sina hörn, ellipser i sina extremvärden, spline-kurvor med
`add_nurbs` över kantens del; en skärningskurva (`int_int_cur`), vars spline bara närmar sig
kurvan inom sin fit-tolerans (endstop-filen: 4·10⁻⁴), flyttas till kurvans egna punkter med
Newton på de två ytornas ekvationer där koordinaten har extremvärde längs kurvan. Ytor vars
inre kan nå längre än kanterna: sfär (c ± r·ŵ), torus (där normalen är ±ŵ; toppcirkeln när ŵ
är axeln; citron och äpple), konens spets, spline-ytor (gren och gräns på Bézier-lappar:
kontrollnätet begränsar, Bernstein-koefficienterna för derivatornas täljare avgör om en
kritisk punkt kan finnas, Newton finner den). Plan, cylindrar och extruderingar
(`cyl_spl_sur`, summa med en rak kurva) är raka i en riktning — kanterna räcker. En sådan
punkt räknas när den ligger i ytan: öglorna ritas i ytans parametrar, ytan ligger till vänster
om sina coedges sett från utsidan (ACIS normaler: sfärens och torusens radietecken, konens
cosinustecken; sidan prövad på bottenskivan i AutoCAD:s cylinder, konens spets på AutoCAD:s
koner, trimmad torus på ODA:s rundning), och en punkt är inne när vägen
dit från strax innanför en ögla korsar öglorna ett jämnt antal gånger (periodiska bilder
med). En världskoordinat är en linjär funktion av kroppens koordinater, så varje affin
avbildning (transform, INSERT-block, OCS) blir exakt.

Facit, AutoCAD:s egna `$EXTMIN`/`$EXTMAX` i filer med bara solider: jscads `3Ddraw.dxf`
(AutoCAD 2017, SAB; låda, kil, kon med spets, sfär, cylinder, torus, vriden pyramid) lika
inom 4·10⁻¹⁵ (AutoCAD:s egen avrundning, t.ex. 5.999999999999996 mot 6); Asposes
`conic_pyramid.dxf` (SAT 21500; åtta toriska ringar, en kon vars spets är toppen) inom
6·10⁻¹³. Lokalt, utan licens för testerna: capp_first (2004; solider, regioner) inom
3·10⁻¹²; penholder (AutoCAD för Mac 2024, SAB med spline-kanter) exakt — dess största y
ligger mitt på en spline-kant där kontrollpolygonen når 0,05 längre, så AutoCAD räknar
kurvans låda, inte polygonens; lilli_137 (AutoCAD 2020, `ASM BinaryFile4`) exakt.
ACadSharps solider ger sina egna data i alla tolv filer 2000–2018: D65 (15.276725469823759,
−223.35012266071593, 0)–(20.276725469823759, −218.35012266071593, 5), REGION D69
(21.772069826395153, −223.79081030579778, 0)–(28.199438729025417, −217.0092253777116, 0),
D6A (29.667678768417478, −222.09968099858145, 0)–(33.70384675508885, −218.11496059988497,
4.380487155114504). ODA File Converters R12-nät av varje solid (alla filer ovan och ett tiotal
till) ligger inom vår låda och är lika där ytorna är plana (6·10⁻¹⁴). Hela ACadSharp-
ritningen: x/y och z max som AutoCAD:s; z min (−5.091688756873281) kommer inte från soliderna
(z 0–5) utan från MESH 343 (AcDbSubDMesh, nivå 0, en hörnpunkt) som HDxf läser som okänd
entitet — ett eget steg, liksom AcDbSurface-entiteterna (EXTRUDEDSURFACE m.fl., ACIS de
också). B7: HDxf:s 2000/2004-filer med soliderna (ACadSharp, capp, conic pyramid) läses av
ODA utan fel och får AutoCAD:s extents. Tester: `test/test_acis.cpp` (syntetiska SAT för
sfär-, torus- och spline-yta, skärningskurva, transform, SAB, 1.06, sparad 2000-fil, och
filerna ovan; nya i `manifest.cmake`). Ingen Codex i steget (uppdraget). 394 tester gröna.

Efter Codex-granskningen (2026-10-24): (1) spline-ytans gren och gräns släppte lappar utan
inre kritisk punkt, men där ytan bara är kontinuerlig (en inre knut, t.ex. grad 1) kan
största värdet ligga på knutlinjen eller där två knutlinjer korsar — ett tält av grad 1 × 1
gav z max 0 i stället för 2. Nu räknas varje inre knutlinje som en kurva på ytan
(kontrollpunkterna: raderna viktade med basfunktionerna vid knuten), med sina exakta
extremvärden och knutar där de ligger i ytan; ytans ändar likadant för en yta utan öglor, och
en sluten ytas söm alltid. (2) En sluten yta vars öglor bara är sömmar (en kant som ytan
löper längs åt båda hållen) fick ingen referenspunkt och föll bort — en sfär med söm från pol
till pol gav (0, 0, −1)–(1, 0, 1). Nu är en yta med bara sömmar, liksom en yta utan öglor
(ACIS), hela ytan, och sömmar väljs sist för referenspunkten (prövat för sfär, torus och
periodisk spline-torus). (3) Ytor av rotation (`rotsur`/`rot_spl_sur`) och summor av två
kurvor (`sumsur`/`sum_spl_sur`) räknas efter sin egen definition, med eller utan sparad
approximation: profilkurvan (rak, ellips eller intcurve, med sitt område) och axeln (punkt,
riktning), respektive de två kurvorna och baspunkten, och spl_sur-områdena (`none`: u- och
v-intervallen, `full`: bs3-ytans knutar). Rotation: S(u, v) = A + R(v)(C(u) − A), v vinkeln
kring axeln efter högerhandsregeln (speaker.sat:s rotationsytor slutar vid kanter vridna så);
en världskoordinat är a + b cos v + c sin v, extrem över varven där tan v = c/b och längs
profilen där a ± √(b² + c²) har derivatan 0 — på varje rationellt Bézier-spann av profilen
är kvadraten av villkoret ett polynom, vars rötter tas med R.6:s rotfinnare; profilens
knutar (veck runt varvet) och polen (där profilen når axeln) som konens spets; en yta utan
öglor dessutom ändcirklarna och ändprofilerna. Summa: koordinaten separerar, så alla par av
kurvornas extremvärden och knutar som ligger i ytan (med en rak kurva en extrudering: kanterna
räcker). Prövat: sfär och torus som rotationsytor (ellips- och splineprofil, båda sidor, även
genom en allmän affin avbildning) ger de analytiska ytornas lådor inom 1·10⁻¹⁴; en
tre-kvarts-varvs vågig kubisk profil, vriden och flyttad, stämmer med tät sampling och lokal
sökning inom 1·10⁻¹⁵; summan av två paraboler (toppen och den vridna toppen analytiskt). ODA
File Converter maskar rotationstorusen inom vår låda. AutoCAD:s SAB `sum_spl_sur` 20900
(klonk_base, extruderingar) läses. Dessutom `-Wmissing-field-initializers` i
`Color::from_aci`/`by_block`/`by_layer`. (4) Om en punkt ligger i ytan avgjordes mot
öglornas samplade polygoner (inom ungefär 1·10⁻⁶ av parameterområdet), så en punkt strax
utanför en krökt kant kunde räknas — vid ett veck ger det ett fel av samma storlek. Nu
behåller varje segment sitt ursprung (kant, kurva, parametrarna i ändarna) och ett band (8
gånger samplingens tolerans, eller kantens uppmätta avstånd där den inte förfinades): ligger
punkten (eller referenspunkten) inom bandet följs kanten där på sin egen kurva — halveras och
avbildas punkt för punkt — tills punkten ligger längre från varje bit än kanten kan (två gånger
kantpunkternas avstånd vid en fjärdedel, hälften och tre fjärdedelar), eller biten är kanten
till sista biten; med periodiska bilder. En punkt på en ögla (till sista biten) ligger i ytan.
Likadant för `line()` (öglornas skärningar exakt), för gren och gräns (rektangeln breddas med
banden), och för konens spets och rotationsytans pol: `reaches()` tar öglornas exakta minsta
avstånd från spetsens linje, och remsan där emellan avgörs av en punkt i den (förut en punkt
1·10⁻⁷ från linjen). Prov: åsen z = (1 − |x|)(1 − y²) skuren av en exakt spline-kant x = c +
a y² (c = −1·10⁻⁷) vars kordor går utanför kurvan; vecket toppar 1·10⁻⁷ utanför ytan — förut
z max 1, nu 1 − 1·10⁻⁷ (kantens spets). De 24 orakelfilerna ger samma lådor, körtiden som
förut (wrist 11 s mot 10,8 s). 406 tester gröna.

Kontrollpunkter för AutoCAD (gick inte att avgöra ur AutoCAD:s filer): svep och loft
(`sweep_spl_sur`, `skin_spl_sur`/`loft_spl_sur`) som sparas utan bs3 räknas efter sina kanter —
filen har deras data (profil, bana, räls- och lagfunktioner, tangentfaktorer) men inte hur
ACIS bygger ytan av dem (svepets ramar, skinningens interpolation); det står inte i Spatials
publicerade SAT-format och inget prov har sådana ytor; nivån `summary` (formatet
odokumenterat) ger inget parameterområde, vilket bara spelar roll för en rotations- eller
summayta utan öglor, som då tar kurvans hela område; en profil som själv är en approximerad
intcurve räknas efter sin spline; procedurella ytor och kurvor med sparad approximation
(rundning `rbblnsur`, offset, projektion …) efter approximationen, inom dess fit-tolerans
(skärningskurvor med läsbara ytor exakt); trimmade sfärer, tori, spline-, rotations- och
summaytor vars inre extremvärde ligger i ytan — exakt här, inget AutoCAD-prov (ODA:s
torusrundning i safecast stämmer); transformens skala gånger matrisen (ACIS SPAtransf; AutoCAD skriver skala 1 i alla
prov); SAT under 1.06 läses efter samma regler men är oprövat. lilli_090 (AutoCAD 2020) har
x/y max lika med spline-kanternas kontrollpunkter, men samma projekts lilli_001 har z max 5,0
för en 0,35 tjock del: inaktuella extents (AutoCAD krymper dem inte förrän regen), och
penholder visar kurvans låda.
