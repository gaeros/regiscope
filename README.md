# RegiScope

App nativa per Windows (C++17 + Win32): scrivi uno snippet in C, lo esegui
istruzione per istruzione e vedi registri della CPU e memoria bit per bit.

## Compilare con Visual Studio Community

1. Installa il carico di lavoro **"Sviluppo di applicazioni desktop con C++"**
   (include CMake). Serve anche **Git**, perché CMake scarica TinyCC e Zydis da GitHub.
2. In Visual Studio: **File → Apri → Cartella…** e scegli questa cartella.
3. Attendi che CMake finisca la configurazione (la prima volta scarica le dipendenze).
4. Scegli la configurazione **x64-Debug** o **x64-Release**, poi come elemento di avvio
   **RegiScope.exe** e premi **F5**.

In alternativa, da "Developer PowerShell for VS":

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\Release\RegiScope.exe
```

Accanto all'eseguibile vengono copiate le cartelle `snippet_include` (header per gli
snippet) e `snippet_runtime` (funzioni di supporto di TinyCC): servono durante l'esecuzione.

## Tasti

| Tasto | Azione |
| --- | --- |
| F10 | Avanti: esegue una istruzione macchina (al primo colpo compila e si ferma su `main`) |
| F11 | Avanti riga: esegue fino alla prossima riga di C |
| F5 | Esegui: prosegue fino alla fine senza fermarsi |
| Maiusc+F5 | Ferma |
| F7 | Compila |
| Ctrl+O / Ctrl+S | Apri / Salva un file `.c` |

## Come funziona

- **Compilazione**: TinyCC (libtcc) compila lo snippet direttamente in memoria.
- **Passo passo**: lo snippet gira in un thread separato con il *trap flag* della CPU
  attivo; dopo ogni istruzione Windows solleva un'eccezione "single step" che l'app
  intercetta (gestore di eccezioni vettoriale), fotografa i registri e attende il tasto Avanti.
  Le chiamate a funzioni dell'app (`printf`, `malloc`, …) vengono eseguite in un colpo solo,
  con un breakpoint hardware sull'indirizzo di ritorno.
- **Riga corrente**: TinyCC compila con `-g`; `src/tcc_lines.c` legge le info di debug
  (stabs) e trova la riga di C di ogni istruzione. Una freccia verde la indica nel
  margine dei numeri di riga, e il pulsante Avanti resta verde finché dura il passo passo.
- **Errori**: un crash dello snippet (es. puntatore nullo) o un ciclo infinito fermato
  con Ferma non chiudono l'app.
- **RAM**: i banchi fisici vengono letti da WMI (`Win32_PhysicalMemory`); il pannello
  mostra anche lo stack della funzione corrente (da RSP) e i blocchi `malloc`, con i bit
  cambiati dall'ultima istruzione evidenziati in blu.
- **CPU**: modello e cache da CPUID; 16 registri + RIP a 64 bit e i flag di RFLAGS.

## Esercizi (pulsante "Esempi")

Il pulsante **Esempi** apre un catalogo di esercizi commentati, dal più facile al più difficile.
I file stanno nella cartella `esempi` accanto a RegiScope.exe: puoi aggiungerne di tuoi, basta
che la seconda riga del commento iniziale sia il titolo.

| File | Argomento | Livello |
|---|---|---|
| 00_benvenuto.c | Esempio iniziale | - |
| 01_variabili.c | Variabili e assegnamento | 1 |
| 02_tipi_e_overflow.c | Tipi e overflow | 1 |
| 03_if_e_flag.c | Decisioni con if / else | 2 |
| 04_cicli.c | Cicli for e while | 2 |
| 05_funzioni.c | Funzioni e stack | 3 |
| 06_array.c | Array | 3 |
| 07_puntatori.c | Puntatori | 4 |
| 08_stringhe.c | Stringhe | 4 |
| 09_malloc_free.c | Memoria dinamica: malloc e free | 5 |
| 10_struct.c | Struct | 5 |
| 11_ricorsione.c | Ricorsione | 6 |
| 12_bit.c | Operazioni sui bit | 6 |
| 13_fotocamera.c | Fotocamera: pixel e colori | periferiche |
| 14_posizione.c | Posizione e numeri con la virgola (double) | periferiche |
| 15_microfono.c | Microfono: il suono come numeri | periferiche |
| 16_batteria_sensori.c | Batteria e sensori | periferiche |

Ogni esercizio spiega *cosa impari*, *cosa guardare* nei pannelli RAM e CPU e chiude con un *Prova tu*.

## Periferiche (pulsante "Periferiche")

Apre una seconda finestra con cinque riquadri: **Fotocamera** (anteprima dal vivo, Scatta, e il pixel
al centro mostrato byte per byte), **Posizione** (con i 64 bit della latitudine), **Microfono**
(registra 2 secondi e mostra la forma d'onda), **Batteria** e **Sensori** (luce, accelerometro).

Gli snippet possono usare le stesse periferiche con `#include <periferiche.h>`:

| Funzione | Cosa fa |
|---|---|
| `camera_apri()`, `camera_chiudi()` | accende / spegne la webcam |
| `camera_scatta(rgb, L, A)` | foto ridotta a L x A pixel, 3 byte (R, G, B) per pixel |
| `mostra_immagine(rgb, L, A)` | mostra un'immagine nella finestra Periferiche |
| `posizione_leggi(&lat, &lon, &precisione)` | posizione in gradi e precisione in metri |
| `microfono_registra(campioni, n)` | n campioni a 16000 al secondo (short) |
| `batteria_leggi(&percentuale, &in_carica)` | stato della batteria |
| `luce_leggi(&lux)`, `accelerometro_leggi(&x, &y, &z)` | sensori (se presenti) |

Tutte restituiscono 1 se è andata bene, 0 se no (il motivo compare nell'output).
Windows chiede i permessi in Impostazioni → Privacy e sicurezza (Fotocamera, Posizione, Microfono);
per la posizione va attivato anche "Consenti alle app desktop di accedere alla posizione".

## Librerie disponibili negli snippet

`printf`, `puts`, `putchar`, `sprintf`, `snprintf`, `malloc`, `calloc`, `realloc`,
`free`, `abs`, `labs`, `atoi`, `rand`, `srand`, `memset`, `memcpy`, `memmove`, `memcmp`,
`strlen`, `strcpy`, `strncpy`, `strcmp`, `strncmp`, `strcat`, `strchr`.
Niente file, rete o finestre.

## Prossimi passi

- Nomi delle variabili locali accanto ai byte dello stack.

## Licenza

RegiScope è distribuito con licenza **PolyForm Noncommercial 1.0.0** (vedi [LICENSE.md](LICENSE.md)):
puoi studiarlo, modificarlo e condividerlo liberamente per scopi non commerciali
(studio, scuola, ricerca, uso personale). L'uso a scopo di lucro non è consentito.

Le librerie esterne non sono incluse in questo repository: CMake le scarica durante la
configurazione e restano sotto le loro licenze.

- [TinyCC](https://github.com/TinyCC/tinycc): GNU LGPL 2.1
- [Zydis](https://github.com/zyantific/zydis): MIT

Se distribuisci un eseguibile compilato, ricorda che contiene TinyCC collegato
staticamente: vanno rispettati gli obblighi della LGPL (ad esempio indicare la licenza
e rendere disponibili i sorgenti di TinyCC usati).
