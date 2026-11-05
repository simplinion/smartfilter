# smartfilter

Strumieniowy filtr outputu komend w C99 (POSIX).

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build
some-command | smartfilter
```

Domyślna instalacja trafia do `~/.local/bin`, bez potrzeby używania `sudo`.
Katalog musi znajdować się w `PATH` (jeśli go brakuje, dodaj
`export PATH="$HOME/.local/bin:$PATH"` do konfiguracji powłoki).
Własny katalog instalacji można ustawić podczas konfiguracji przez
`-DCMAKE_INSTALL_PREFIX=/wybrana/sciezka`. W istniejącym katalogu `build`
CMake zachowuje wcześniej zapisany prefiks; aby go zmienić, użyj
`cmake -S . -B build -DCMAKE_INSTALL_PREFIX="$HOME/.local"`.

`main.c` czyta stdin po jednej linii i przekazuje ją przez zarejestrowane
poziomy, w kolejności tablicy `levels`. Domyślnie output pozostaje bez zmian.

- `base/base.h` — typ linii, callbacki i interfejs pipeline.
- `base/base.c` — przekazywanie linii między poziomami i do końcowego outputu.
- `level1/` — przykładowy poziom z osobnym kontekstem i konfiguracją;
  `drop_empty_lines` pozwala odrzucać puste linie (domyślnie wyłączone).

Callback `filter` zwraca `true`, żeby przekazać oryginalną linię dalej,
lub `false`, żeby ją zatrzymać. `false` oznacza decyzję filtrowania, nie błąd.
Poziom może wywołać `sf_emit(output, line)` dowolną liczbę razy: każda taka
linia przechodzi synchronicznie przez wszystkie następne poziomy.
Żeby zastąpić oryginalną linię:

```c
sf_emit(output, (sf_line){.data = "replacement\n", .length = 12});
return false;
```

Emitowanie i zwrócenie `true` dodaje nowe linie przed oryginalną.
Opcjonalny callback `finish` służy do opróżniania buforów przy EOF;
wywoływany jest w kolejności poziomów, więc wygenerowany output przechodzi
przez następne poziomy przed ich zakończeniem.

Nowy poziom powinien definiować własne typy konfiguracji i kontekstu oraz
funkcję tworzącą `sf_level`, a następnie zostać dopisany do `levels` w main
i do źródeł w CMake. Konfiguracja, konteksty i tablica poziomów należą do
wywołującego i muszą istnieć przez cały czas przetwarzania.

`sf_line` zawiera wskaźnik i długość; zachowuje końcowy newline, jeśli był
obecny, i nie wymaga terminatora NUL. Dane linii i uchwyt `sf_output` są
ważne tylko podczas callbacku; buforujący poziom musi kopiować dane.
Poziomy przekazują dane przez `sf_emit`, a nie przez bezpośredni zapis do
stdout. Błąd końcowego zapisu zatrzymuje pipeline i jest propagowany jako
`false` z `sf_emit` oraz funkcji pipeline.

Deduplikację w pierwszym poziomie włącza opcja `-d`:

```sh
some-command | smartfilter -d
```

Pozostaje pierwsze wystąpienie każdej linii, także gdy powtórzenia nie są
sąsiadujące. Bez `-d` duplikaty są przepuszczane. `sf_line.hash` zawiera
64-bitowy FNV-1a obliczony raz na wejściu do pipeline (również dla nowych
linii z `sf_emit`); kolejne poziomy dostają już gotowy hash. Po zmianie treści
należy przekazać nową linię przez `sf_emit` i zatrzymać oryginalną.
Hash obejmuje wszystkie bajty, łącznie z zakończeniem linii.

Zbiór hashy korzysta z otwartego adresowania i rośnie przy zapełnieniu 75%.
Wyszukiwanie ma średnią złożoność O(1), a pamięć O(liczba unikalnych hashy).
Przechowujemy wyłącznie hashe: kolizja 64-bitowego hasha również powoduje
pominięcie linii. Kontekst poziomu należy zwolnić przez `level1_destroy`,
także po błędzie. Brak pamięci zatrzymuje program z błędem.

Kompaktowanie wspólnych prefiksów włącza `-c`; można je łączyć z `-d`:

```sh
some-command | smartfilter -dc
```

Przykład (wcięcie to zwykłe spacje):

```text
[INFO]   Installing resource 'dmfc': ./dmcrypto.dmfc
                            ↳ dmd': ./dmcrypto.dmd
                            ↳ version': ./dmcrypto_version.txt
```

Poziom `level2` porównuje linię z poprzednią pełną linią, która przeszła
przez deduplikację. Wspólny prefiks zastępuje spacjami i znacznikiem `↳ `,
pozostawiając różniącą się część w tej samej kolumnie. Minimalny prefiks
wynosi 4 bajty (`level2_config.minimum_prefix`). Pierwsza linia i linie
bez dostatecznie długiego prefiksu pozostają w całości. Pusta linia resetuje
porównanie. Identyczne linie bez `-d` są pokazywane jako samo wcięcie i znacznik.

Pamiętana jest tylko poprzednia pełna linia i bufor wyjściowy; pamięć jest
proporcjonalna do najdłuższej linii. Wygenerowany output przechodzi przez
`sf_emit`, więc kolejne poziomy dostają hash nowej treści. Szerokości znaków
są określane według locale użytkownika, a tabulatory mają tabstopy co 8 kolumn.
Kolory i style ANSI SGR są zachowywane, ale pomijane przy porównywaniu
i liczeniu kolumn (także resety na początku następnej linii). Prefiksy
z innymi kodami sterującymi lub niepoprawnym tekstem w danym locale
nie są kompaktowane.
