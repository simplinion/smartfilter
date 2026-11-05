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

Moduł `level1/compact.c` porównuje linię z poprzednią pełną linią, która
przeszła przez deduplikację i opcjonalne skracanie ścieżek. Wspólny prefiks zastępuje spacjami i znacznikiem `↳ `,
pozostawiając różniącą się część w tej samej kolumnie. Jeśli różnica wypada
wewnątrz słowa, prefiks cofa się do jego początku: `for` → `from` pokazuje
całe `from`, zamiast samego `rom`. Litery, cyfry i `_` są trzymane razem;
interpunkcja, np. `/` lub `-`, może stanowić granicę skracania. Minimalny prefiks
wynosi 4 bajty (`level1_config.compaction.minimum_prefix`). Pierwsza linia i linie
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

Skracanie ścieżek włącza `-p` (domyślnie wyłączone):

```sh
some-command | smartfilter -p
some-command | smartfilter -dcp
```

Wszystkie te funkcje należą do pierwszego poziomu. Kolejność to deduplikacja
oryginalnych linii, skracanie ścieżek i kompaktowanie prefiksów. Moduły
`level1/paths.c` i `level1/compact.c` mają stan wewnątrz `level1_context`;
nie są osobnymi poziomami pipeline. Przed przetwarzaniem należy wywołać
`level1_init`, a na końcu `level1_destroy` (również po błędzie inicjalizacji).

Słownik od początku zna rzeczywisty katalog uruchomienia (`getcwd`, bez
polegania na zmiennej środowiskowej). Nazwa skrótu pochodzi z nazwy katalogu,
np. `/data/project/build` → `$build`, również dla bieżącego katalogu.

Pozostałe wpisy powstają dopiero po powtórzeniu części katalogowej ścieżki.
Filtr śledzi granice katalogów, więc `/data/projects/toolchain/sdk/include/a.h` oraz
`/data/projects/toolchain/sdk/lib/b.a` wykrywają wspólną część `/data/projects/toolchain/sdk` → `$sdk`.
Jednorazowa ścieżka poza znanym katalogiem nie tworzy skrótu. Wybierany jest
wariant dający najkrótszy zapis spośród powtarzających się prefiksów
na granicy katalogu.
Wykrywanie działa również wewnątrz bieżącego katalogu.

Przetwarzanie jest strumieniowe: pierwsze wystąpienie jest pełne, a skrót
obowiązuje od wykrycia powtórzenia. Dotyczy to również dwóch ścieżek w jednej
linii. Wcześniej wypisany output nie jest przepisywany. Przy `-dp` duplikaty
całych linii są usuwane przed wykrywaniem powtarzających się ścieżek.

Skróty wyświetlane w logach i słowniku nie mają prefiksu `SMARTFILTER_`.
Nazwy zachowują wielkość liter; znaki spoza ASCII liter, cyfr i `_` są
zastępowane `_`, a nazwy rozpoczynające się cyfrą dostają prefiks `_`.
Dla różnych ścieżek o tej samej nazwie katalogu powstają np. `$include`
i `$include_2`. Nazwy są stabilne do końca uruchomienia. Przed utworzeniem nowej zmiennej
porównywane są: pełna ścieżka, zapis przez istniejące zmienne i zapis przez
nową zmienną (łącznie z sufiksem przy konflikcie nazw). Nowa zmienna powstaje
tylko wtedy, gdy daje ściśle krótszy wynik. Przy remisie wygrywa istniejący
skrót, a jeśli żaden skrót nie pomaga, zostaje oryginalna ścieżka. Odrzucone
propozycje nie rezerwują nazw ani nie trafiają do końcowego słownika.

Filtr rozpoznaje ścieżki absolutne, `./…`, `../…` i względne zapisy
z ukośnikiem, np. `generated/include/a.h`. Ścieżki względne są wiązane
z katalogiem uruchomienia, a `.` i `..` normalizowane tekstowo. Wariant
absolutny i względny tej samej ścieżki korzystają ze wspólnego słownika:

```text
./generated/intermediate/include/a.h
$include/b.h
```

Warunek skrócenia porównuje wynik z oryginalnym zapisem, więc np. `./a`
nie zostanie wydłużone do aliasu bieżącego katalogu. Bez podmiany zachowany
jest dokładny oryginalny tekst. Ścieżki ze spacjami powinny być ujęte
w pojedyncze lub podwójne cudzysłowy. Same nazwy plików bez ukośnika
nie są interpretowane jako ścieżki.

Adresy URL, opcje zaczynające się od `-`, ułamki typu `1/10`, daty złożone
z cyfr i ukośników oraz wyrażenia z `$`, `~` lub wildcardami pozostają
bez zmian. Wykrywanie względnych ścieżek jest heurystyką: słowo z ukośnikiem
może być uznane za ścieżkę również wtedy, gdy nie oznacza pliku.

Wykrywanie nie wymaga istnienia plików. Normalizacja nie rozwiązuje dowiązań
symbolicznych, więc `..` jest interpretowane wyłącznie tekstowo. Filtr
nie śledzi zmian katalogu procesów potomnych; bazą jest zawsze katalog
uruchomienia smartfilter. Pamięć rośnie z liczbą poznanych prefiksów.

Domyślnie `-p` tylko podmienia ścieżki. Opcja `-P` dodatkowo wypisuje na
końcu słownik użytych skrótów i sama również włącza podmianę ścieżek:

```sh
some-command | smartfilter -dcp   # podmiany bez słownika
some-command | smartfilter -dcP   # podmiany i słownik
```

Przykładowy output z `-P`:

```text
/data/projects/toolchain/sdk/include/a.h
$sdk/lib/b.a

[paths]
$sdk = /data/projects/toolchain/sdk
```

Definicje wypisywane przez `-P` odwołują się do nadrzędnego katalogu, którego użyty skrót daje
najkrótszy zapis. Jeśli odwołanie nie skraca definicji, zostaje pełna ścieżka. Są wypisywane od katalogów nadrzędnych do podrzędnych,
również jeśli rodzic został wykryty później niż dziecko. Przykład:

```text
$dmod_boot = /data/projects/chocotechnologies/public/dmod-boot
$build = $dmod_boot/build
$include = $build/include
```

Dopasowania respektują granice katalogów i nie tworzą cykli. Definicje
omijają kompaktowanie prefiksów. Nazwy są skrótami w logu; filtr nie ustawia
zmiennych środowiskowych w powłoce wywołującej: proces uruchomiony w pipe
nie może zmienić środowiska swojego rodzica. Do sprawdzenia mapowania służy
`-P`. Bez żadnej podmiany sekcja słownika nie jest dopisywana.
