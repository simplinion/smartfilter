# Lokalna usługa embeddingów i level 5

Usługa ładuje model raz na proces. Klient smartfilter w C nie uruchamia
Pythona, nie pobiera modelu i nie wysyła danych do zewnętrznych API.
Usługa przyjmuje teksty przez Unix socket i zwraca embeddingi. Kody kolorów
ANSI SGR są pomijane przed embeddingiem i w kluczach cache; tekst wyjściowy
pozostaje zachowany. Kosinus,
powiązanie wyników z całymi zdarzeniami i filtrowanie należą do levelu 5.

## Przygotowanie i uruchomienie z checkoutu

Wymagany jest Python 3.11+; zależności ML dotyczą wyłącznie usługi.

```sh
python3 -m venv build/semantic-env
build/semantic-env/bin/python -m pip install -r service/requirements.txt
python3 service/download_model.py build/semantic-model
build/semantic-env/bin/python service/embedding_server.py \
  --model-dir build/semantic-model
```

Ostatnią komendę uruchom w osobnym terminalu i pozostaw działającą. Model
jest wczytywany i rozgrzewany przed utworzeniem socketu. Gotowość sprawdzisz:

```sh
python3 service/embedding_server.py --check
```

Domyślny socket to `$XDG_CACHE_HOME/smartfilter/embeddings.sock`, a gdy XDG
nie jest ustawione na ścieżkę absolutną — `~/.cache/smartfilter/embeddings.sock`.
Własną ścieżkę podaj obu stronom: `--socket /tmp/e.sock` w usłudze,
`--embedding-socket /tmp/e.sock` w smartfilter. Socket i plik blokady są
prywatne dla użytkownika. Usługa nie nasłuchuje na TCP.

Downloader pobiera `intfloat/multilingual-e5-small` z przypiętej rewizji
`614241f622f53c4eeff9890bdc4f31cfecc418b3`: model ONNX, tokenizer i config
(łącznie około 490 MB). Manifest zawiera SHA-256 każdego pliku; serwer
weryfikuje je przy uruchomieniu. Serwowanie działa offline i nie pobiera
brakujących plików. Zmiana modelu wymaga jawnego restartu usługi.

Opis modelu: https://huggingface.co/intfloat/multilingual-e5-small
Embedding ma 384 wymiary. Używamy właściwych prefiksów `query: ` i
`passage: `, średniej po tokenach z maską attention i normalizacji L2,
zgodnie z kartą modelu. Długie teksty są dzielone na fragmenty do 512 tokenów
z nakładką 64 tokenów i średnią z ich wektorów; żaden fragment tekstu nie
jest celowo ucinany. Model nie był dostrajany do naszych logów.

## Używanie levelu 5

```sh
# Wyświetl wynik podobieństwa bez usuwania logów:
cmake --build . 2>&1 | smartfilter --query 'problemy z pamięcią'

# Jawny próg kosinusa; dobierz go na konkretnych danych:
cmake --build . 2>&1 | smartfilter --query 'problemy z pamięcią' --min-score 0.82

# Opcjonalne batchowanie, kategorie, kolory i wcześniejsze kompresje:
some-command 2>&1 | smartfilter -cpu --classify --color \
  --query 'błędy linkera' --semantic-batch 8
```

`--query` automatycznie włącza grupowanie z levelu 4. Całe zdarzenie dostaje
jeden wynik. Do embeddingu trafiają pełne oryginały otrzymanych linii,
a wypisywana jest wersja wyświetlana przez wcześniejsze poziomy. Wiersze
niepowiązane ze zdarzeniem są osobnymi jednostkami wyszukiwania.

Wynik jest dopisywany jako `[sim +0.830] `. `--min-score` wybiera całe
jednostki, nigdy pojedynczy caret z pominięciem reszty błędu. Zachowana jest
kolejność wejścia. Wynik nie jest prawdopodobieństwem ani potwierdzeniem
istotności; zwłaszcza E5 często daje wysokie kosinusy również niepasującym
tekstom. Domyślnie nie ma progu. Ten prototyp nie realizuje jeszcze top-K,
rerankingu ani rozumienia instrukcji typu „pokaż tylko przyczynę awarii”.

`--semantic-batch N` przyjmuje 1..32 jednostki (domyślnie 1). Większy batch
opóźnia output do jego zapełnienia albo EOF. `--semantic-timeout MS` ustala
limit całego RPC (1..60000 ms, domyślnie 5000), nie limit każdego odczytu.
Przekroczenie limitu, utrata połączenia, błędny/zerowy/NaN wektor lub zmiana
identyfikatora modelu wyłączają wyszukiwanie dla reszty tego uruchomienia:
nieocenione bufory i kolejne wejście są przepuszczane, a stderr dostaje
jedną informację. Wcześniejsze świadome odrzucenia przez próg nie są cofane.

W ramach jednostki mieści się do 64 KiB wyświetlanego tekstu i oryginałów.
Za duże zdarzenie jest przepuszczane w całości bez oceny. Dane binarne,
puste linie i syntetyczne dane bez oryginału również są przepuszczane.
Metadane `sf_line.has_similarity` i `similarity` udostępniają wynik następnym
poziomom. Metadane zdarzenia są kopiowane, gdy jednostka jest buforowana.
Znaczniki podglądu `--events` należą do tego samego zdarzenia, ale nie są
częścią tekstu embeddingu.

Level 5 nie odzyska tego, co usunęły wcześniejsze poziomy. Przy wyszukiwaniu
unikaj `-i`, agresywnego `--show` i `-b`, jeśli mogą ukryć szukany materiał.
Także kategoria diagnostic może zostać odrzucona przez jawny próg levelu 5,
razem ze swoim całym zdarzeniem. Nie ma niejawnego progu ani cichego
przejścia na dopasowanie słów kluczowych.

## Instalacja i systemd użytkownika

Domyślnie `cmake --install` przygotowuje i uruchamia usługę dla aktualnego
użytkownika. Konfiguracja i kompilacja nie pobierają modelu ani zależności:

```sh
cmake -S . -B build -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
cmake --install build
smartfilter-embeddings --check
```

Instalację wykonuj bez `sudo`, w swojej sesji `systemd --user`. Wymagany jest
Python 3.11+ z modułem `venv`, `pip` w tworzonym środowisku oraz dostęp do
internetu przy pierwszej instalacji. Instalator tworzy osobny venv,
instaluje `requirements.txt` i pobiera przypięty model (~490 MB). Jeśli
`build/semantic-model` zawiera już zweryfikowany model, kopiuje go zamiast
pobierać. Kolejna instalacja sprawdza zależności i sumy plików; gdy wszystko
jest aktualne, nie uruchamia pip ani pobierania.

Następnie zapisuje jednostkę, wykonuje `daemon-reload`, `enable` i `restart`
oraz czeka na faktyczną gotowość modelu (do 90 sekund). Polecenie instalacji
zwraca błąd, jeśli przygotowanie albo uruchomienie usługi się nie powiedzie.
Usługa uruchamia się również przy kolejnych logowaniach użytkownika.

Venv i modele trafiają do `$XDG_DATA_HOME/smartfilter` (domyślnie
`~/.local/share/smartfilter`), a jednostka do
`$XDG_CONFIG_HOME/systemd/user/smartfilter-embeddings.service` (domyślnie
`~/.config/systemd/user`). Wygenerowana jednostka używa rzeczywistych
ścieżek instalacji, także przy `cmake --install build --prefix /inna/sciezka`.
Socket jest ustawiany zgodnie z `XDG_CACHE_HOME` instalującego użytkownika;
klient powinien używać tego samego ustawienia lub `--embedding-socket`.
Ponowna instalacja aktualizuje zarządzaną jednostkę i restartuje usługę.

```sh
systemctl --user status smartfilter-embeddings
journalctl --user -u smartfilter-embeddings
systemctl --user disable --now smartfilter-embeddings  # zatrzymaj i wyłącz autostart
```

Dla instalacji samego programu, środowiska bez systemd albo instalacji
systemowej skonfiguruj `-DSMARTFILTER_INSTALL_SERVICE=OFF`. Nadal instalowane
są skrypty, wymagania i przykładowa jednostka w `share/smartfilter/systemd`;
usługę można przygotować ręcznie. Ta opcja nie zatrzymuje już działającej
usługi. Pakowanie przez `DESTDIR` zawsze pomija przygotowanie i aktywację.
Instalator usługi odmawia działania jako root.

Usługa obsługuje maksymalnie 16 połączeń. Pozostają otwarte również podczas
długich przerw w logu; każde RPC ma deadline po stronie klienta.
Wszystkie współdzielą jeden model; wnioskowanie jest serializowane i
batchowane w ramach żądania. To nie jest jeszcze batchowanie między
równoległymi klientami. Cache LRU jest wspólny, rozdziela role query/passage,
ma domyślnie limit 64 MiB rozliczanych danych (plus narzut obiektów Pythona)
i istnieje tylko w pamięci procesu. Parametry: `--threads`, `--cache-mib`.

Szczegóły komunikacji: [PROTOCOL.md](PROTOCOL.md).

## Testowanie

CTest zawiera testy protokołu na jawnym test double bez bibliotek ML;
nie są one dowodem jakości semantycznej. Testy sprawdzają cache, batchowanie,
równoległych klientów, utratę serwera, timeout, zmianę modelu, NaN i zachowanie
całego zdarzenia. Osobny test prawdziwego E5 wymaga działającej usługi:

```sh
python3 tests/semantic_real_test.py ./build/smartfilter \
  "$HOME/.cache/smartfilter/embeddings.sock"
```

Test można też uruchomić z `--model-dir build/semantic-model` zamiast
ścieżki socketu: uruchomi na chwilę własną instancję i posprząta ją po teście.

To mały test polskiego zapytania na angielskim przykładzie, nie benchmark
jakości wyszukiwania. Nadal potrzebujemy większego oznaczonego zbioru logów.
