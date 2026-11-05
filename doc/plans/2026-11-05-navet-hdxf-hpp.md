# Debuginformationen: `hdxf.hpp` delas upp, `src` delas i kataloger

Mål: Debug-biblioteket ska ner från 2,10 GB. Bara refaktor: inget beteende ändras, ett program tar fortfarande in bara
`hdxf.hpp`, inga virtuella funktioner och ingen ändrad `sizeof(Entity)`.

## Orsaken, mätt med GCC 16.2 på MinGW

| Vad | Hela biblioteket |
|---|---|
| Debug idag | 2 099 MB: `.debug_*` 1 297 MB, symbolnamn 638 MB, relokeringar 80 MB, kod 16 MB |

- `.debug_info` har strängarna inne i sig på MinGW. `std::variant` med 209 alternativ skriver hela typlistan på varje nivå:
  7,5 MB i varje fil som har hela `Object`, även när de 172 boxade typerna bara är framåtdeklarerade. Deras innehåll är 1,5 MB.
- 86–91 % av symbolnamnen är `std::__detail::__variant::*`, från att `EntityData` kopieras och förstörs i varje fil.
- Kompilatorn säger att 74 av 121 filer behöver hela `Object` och 47 inte.

Uppskattat utfall ca 1 GB. Målet 100–200 MB nås inte med den här planen: de 74 filerna behåller varianten.

## Steg

1. `src` flyttas med `git mv` till `core`, `io`, `read`, `write`, `drawing`, `records`, `geometry`, `graphics`, `text`.
   Includes skrivs `"katalog/fil.hpp"` med `src` på include-sökvägen, i biblioteket och i testerna.
2. `include/hdxf.hpp` blir en samlande header med `struct Version`. Delarna ligger i `include/hdxf/`:
   `core`, `boxed`, `fwd`, entiteterna, tabellerna, `object` med de raka alternativen, en header per objektfamilj, `drawing`, `ecs`.
3. `EntityData` blir en struct som `ObjectData`: kopiering, flytt, destruktion, `index()` och `held()` i
   `src/core/entity_data.cpp` (`-O2 -g0`). `Entity::as_*()` går genom `data.get<T>()`. `std::get`, `std::holds_alternative`
   och `e.data = LineData{...}` fungerar som förut.
4. De boxade typerna är framåtdeklarerade i `entity.hpp` och `object.hpp`. Det som kräver hela typen flyttar ut ur headern:
   `ObjectData()`, `Entity::dimension_part()`, `Object::as_view_style()`.
5. Varje fil i `src` tar in bara de delar kompilatorn kräver.
6. Mät Debug-biblioteket, kör `python -I tools/run_tests.py` en gång, uppdatera `CLAUDE.md` och `TODO.md`.

## Utfall

Debug-biblioteket, GCC 16.2 på MinGW, samma flaggor före och efter:

| | Före | Efter |
|---|---|---|
| Hela biblioteket | 2 099 MB | 1 003 MB |
| `.debug_*` | 1 297 MB | 770 MB |
| Symbolnamn | 638 MB | 146 MB |
| Relokeringar | 80 MB | 43 MB |
| Symboltabell | 34 MB | 16 MB |
| Sektionshuvuden | 28 MB | 14 MB |
| Kod | 16 MB | 11 MB |
| Symboler | 1 904 005 | 933 048 |
| Sektioner | 742 153 | 355 052 |

`sizeof(Entity)` 736, `sizeof(EntityData)` 336, `sizeof(Object)` 936 och `sizeof(Drawing)` 816 är oförändrade.

Filer som ser en del: `object.hpp` 75, `entity.hpp` 61, `drawing.hpp` 57, `tables.hpp` 46 av 122.

Det som är kvar av debug är framför allt varianten i de 75 filer som har hela `Object`, minst 7,5 MB var. Att gå längre kräver att de
filerna når objekten genom smala funktioner och att `Drawing` och `DxfWriter` lägger sina medlemmar bakom pekare.

## Inte i planen

- De 74 filerna når inte objekten genom smala funktioner.
- `Drawing` och `DxfWriter` behåller sina medlemmar.
- MSVC och Linux provas inte här. Ofullständiga typer är den riskabla delen på MSVC; det står i `TODO.md`.
