# smartfilter

Strumieniowy filtr outputu komend w C99 (POSIX).

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
some-command | ./build/smartfilter
```

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
sf_emit(output, (sf_line){"replacement\n", 12});
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
