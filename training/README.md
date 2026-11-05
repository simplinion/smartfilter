# Trening klasyfikatora levelu 3

To prototyp klasyfikacji, nie streszczanie ani interpretacja wyniku komendy.
Model rozróżnia `progress`, `result`, `stats`, `version`, `error`, `warning`, `info`, `unknown`.

Trening wymaga Pythona 3 i NumPy. Uruchom z katalogu projektu:

```sh
OPENBLAS_NUM_THREADS=1 python3 training/train_level3.py
cmake --build build
ctest --test-dir build --output-on-failure
```

Dane są w `training/log_categories.tsv`: etykieta, tabulator, tekst.
`{target}` jest zastępowany przykładowymi nazwami. To ręcznie przygotowany
zbiór startowy, częściowo rozszerzony regularnymi wariantami komend.
Nie jest reprezentatywnym korpusem rzeczywistych awarii.

Skrypt dzieli szablony na trening i walidację **przed** podstawieniem nazw;
walidacja ma po jednym przykładzie na szablon. Słownik cech powstaje wyłącznie
z części treningowej. Wynik tego podziału nie oznacza jakości na nowych
narzędziach, językach lub rodzinach logów. Progi dobierano podczas prac nad
prototypem; walidacja nie jest niezależnym końcowym testem jakości.

Cechy to obecność znanych angielskich słów oraz bigramów, z normalizacją L2. Dodatkowa cecha `@version` rozpoznaje zapis
`pakiet@wersja`, także z ograniczeniami typu `>=0.3<1.0`, niezależnie
od nazwy pakietu.
Kody SGR są pomijane, a tokeny zawierające `/` lub `$` nie uczestniczą
w cechach, aby nie uczyć się nazw ścieżek. Ogólne angielskie stop words
nie są samodzielnymi cechami. Sieć ma jedną warstwę ukrytą z 24 neuronami
ReLU i wyjście softmax. Adam, inicjalizacja i podział mają stałe seedy.

Eksport do `level3/model/` zawiera:

- `weights.h` — słownik i wagi, wkompilowane w program C;
- `report.json` — pochodzenie danych, SHA-256, parametry i wyniki walidacji;
- `reference.json` — wyniki Pythona do kontroli zgodności obliczeń w C.

Uruchamianie smartfilter nie wymaga Pythona, NumPy ani pobierania modelu.
Testy C/Python porównują wszystkie wyniki softmax z tolerancją 2e-5.
Testy nie uruchamiają treningu i nie wymagają NumPy.

Wynik softmax nie jest skalibrowanym prawdopodobieństwem poprawności.
Polityka wykonania wymaga co najmniej dwóch znanych cech treściowych
(słów lub cech strukturalnych),
wyniku >=0.90 i przewagi nad drugim wynikiem >=0.30. W przeciwnym razie
wynikiem jest `unknown`, który zawsze jest zachowywany. Jawne oznaczenia
ERROR/WARNING mają pierwszeństwo; fragmenty kodu, podkreślenia diagnostyk,
metadane wcześniejszych poziomów, dane binarne i zbyt długie linie są
zachowywane. To ograniczenia pomocnicze, nie gwarancja braku pomyłek.

Najpierw oceniaj nowe dane przez `--classify`. Błędne rozpoznania warto
etykietować i dodawać do osobnego korpusu treningowego, zachowując oddzielny
zestaw rzeczywistych logów do końcowego testowania.

`result` opisuje zakończenie kroku (`Built target …`, `Build finished …`),
a nie potwierdzenie sukcesu całego procesu. Zakończona instalacja pakietu
(`Installed: nazwa@wersja`) również jest `result`; trwająca (`Installing …`)
pozostaje `progress`.
Przy wykonaniu model analizuje `sf_line.original`, jeśli jest dostępne;
trening powinien więc otrzymywać pełne linie przed kompaktowaniem.

Dla `stats` cechy `@size` i `@percentage` wykrywają pomiary i procenty.
Są obliczane identycznie w Pythonie i C; sama nazwa pliku nie jest wymagana.
Jednoliterowe `b` nie stanowi samodzielnej cechy, aby jednostka B nie
zmieniała kategorii pliku `b.o`; jednostkę wykrywa cecha strukturalna rozmiaru.
W danych są także negatywne przykłady postępu z procentem i pobierania z
rozmiarem, aby te same liczby nie wystarczały do przypisania statystyk.

`formatting` jest dodatkową kategorią regułową w C, poza wyjściami sieci.
Model i jego referencyjne wyniki nadal opisują tylko kategorie trenowane.
Czyste separatory `====`, `----`, `****` rozpoznaje reguła przed wnioskowaniem;
nie zmieniamy z tego powodu wag ani wyników walidacji sieci.

`listing` jest kategorią regułową w C dla długich listingów plików, poza
wyjściami sieci. Rozpoznaje strukturę uprawnień i kolejnych pól, a nie
konkretne nazwy właścicieli, plików czy lokalizowany skrót miesiąca.

Korpus zawiera ręcznie oznaczone przykłady metadanych OpenOCD i bannerów
wersji dla kategorii `version`. Identyfikatory z `_`, np.
`download_ext_modules`, nie są rozbijane na słowa sugerujące postęp;
są traktowane jako parametry. Identyczne maskowanie działa w Pythonie i C.
Przy niepewnym wyniku modelu level 3 może zachować kategorię jawnego INFO;
nie jest to wynik samej sieci uwzględniony w macierzy walidacji modelu.

`diagnostic` jest kolejną kategorią regułową poza wyjściami sieci. Rozpoznaje
nagłówki kontekstu kompilatora i fragmenty źródła z gutterem lub podkreśleniem.
Level 3 zawsze je zachowuje, również gdy `--show` nie wymienia diagnostic,
żeby zmiana kategorii z unknown nie usuwała wcześniej zachowywanego kontekstu.
