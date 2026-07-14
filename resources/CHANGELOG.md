# Unisic — Release notes

Per-version notes shown in-app when the version label is clicked. The section
whose `##` heading matches the running version (see `UNISIC_VERSION`) is shown;
within it, the `### English` / `### Polski` block for the toggled language is
displayed. Keep the newest version at the top; each version is translated as a
whole per release (not per individual change).

## 0.7.1b

### English
**New**
- **Trim a recording** without leaving Unisic: History › Trim recording opens a preview with draggable in/out handles, and saves a trimmed copy alongside the original.
- **Instant replay** — keep a rolling buffer of the last seconds of your screen and save it after the fact with one hotkey (default **Meta + Shift + I**). Set the length in Settings.
- **Per-application audio**: record the sound of one chosen application, on its own or mixed with your mic.
- **Hardware video encoder** (VAAPI / NVENC) for MP4, with an automatic fall back to software when the card cannot do it.
- **Watermark** every screenshot with your own text or a logo image — six positions, adjustable opacity.
- **Run a program after capture** — pass the capture to any command with `$input` / `$output` tokens (for example `oxipng -o 4 $input --out $output`).
- **Do not disturb while capturing** pauses desktop notifications so they never land in your screenshot (KDE Plasma).
- **Per-hotkey task presets**: each screenshot hotkey can run its own set of after-capture actions and its own upload destination.
- **Quick task** (default **Meta + Shift + Space**) — one hotkey to choose what to capture and what to do with just that one result.
- New annotation tools: **Measure** (a distance/angle ruler kept in the export) and **Callout** (a speech bubble).
- **Arrowheads** can now be filled, open, or double-ended, and the **highlighter mode** (freehand, rectangle, or text pen) is now yours to pick and is remembered.
- **Select text (OCR)** now boxes every recognized line so you can see exactly what is selectable, and the selection can be highlighted or redacted in place.
- **Paste an image** from the clipboard straight onto the editor canvas.
- **Copy as** Markdown, HTML, a data URI, or a file path — plus **Show QR code** for an uploaded link.
- **Drag a capture** out of the history or its notification thumbnail into another app — a file manager, chat, or editor.
- The history gets a split copy button (image on the left, link on the right) and a **More** menu per item.
- On GNOME, the capture notification is now the same styled card as everywhere else, with working action buttons.
- Click the version label to read these release notes.
- Command line: `--delay SECONDS`, `--output <path>` (`-` for stdout), `--format png|jpg|webp`, and `--measure`.

**Fixed**
- On GNOME, reordering pinned Ubuntu Dock icons while recording a region now works. The record frame is drawn as four thin edges around the region instead of one full-screen surface, so nothing covers the rest of the desktop (and drags inside the recorded region work too). Thanks to the user report that pinned this down.
- **Date subfolders** now bucket recordings too, not just screenshots.
- The screenshot sound cue is now a notification event sound, so it no longer disrupts other audio (such as a Discord screen-share capture).
- Settings help **“?”** badges no longer overlap wide fields, and the duplicate question-mark mouse cursor is gone.

**Removed**
- **Smart pick** (the experimental click-to-pick-an-object option in Settings › Capture) is gone. Detection was purely visual and never recognized windows and elements reliably enough to keep. Region selection by dragging is unchanged.
- The editor's **Remove background** action and the U-2-Net model settings are gone, along with the optional onnxruntime dependency. The smart eraser is unaffected.
- The **Ctrl + /** shortcut cheat sheet is gone — the version label now opens these release notes instead. The window shortcuts themselves stay: **Ctrl + W**, **Ctrl + Q**, **Ctrl + ,** and **Ctrl + 1 … Ctrl + 6**.

### Polski
**Nowości**
- **Przytnij nagranie** bez wychodzenia z Unisic: Historia › Przytnij nagranie otwiera podgląd z przesuwanymi uchwytami początku i końca, a przycięta kopia zapisuje się obok oryginału.
- **Powtórka na żądanie** — Unisic trzyma w pamięci ostatnie sekundy ekranu, a Ty zapisujesz je już po fakcie jednym skrótem (domyślnie **Meta + Shift + I**). Długość ustawisz w Ustawieniach.
- **Dźwięk pojedynczej aplikacji**: nagraj dźwięk jednego wybranego programu, osobno albo w miksie z mikrofonem.
- **Sprzętowy koder wideo** (VAAPI / NVENC) dla MP4, z automatycznym powrotem do programowego, gdy karta sobie nie poradzi.
- **Znak wodny** na każdym zrzucie — własny tekst albo logo, sześć pozycji, regulowana przezroczystość.
- **Uruchom program po przechwyceniu** — przekaż zrzut dowolnemu poleceniu przez `$input` / `$output` (na przykład `oxipng -o 4 $input --out $output`).
- **Nie przeszkadzać podczas przechwytywania** wstrzymuje powiadomienia pulpitu, żeby nie wpadły na zrzut (KDE Plasma).
- **Presety zadań per skrót**: każdy skrót zrzutu może mieć własny zestaw akcji po przechwyceniu i własny serwer docelowy.
- **Szybkie zadanie** (domyślnie **Meta + Shift + Spacja**) — jeden skrót, by wybrać co przechwycić i co zrobić z tym jednym wynikiem.
- Nowe narzędzia adnotacji: **Miarka** (linijka odległości i kąta zachowana w eksporcie) oraz **Dymek**.
- **Groty strzałek** mogą być teraz wypełnione, otwarte lub dwustronne, a **tryb zakreślacza** (odręczny, prostokąt, pisak tekstu) sam wybierasz i jest zapamiętywany.
- **Zaznaczanie tekstu (OCR)** obrysowuje teraz każdą rozpoznaną linię, więc dokładnie widać, co można zaznaczyć, a zaznaczenie można od razu zakreślić lub zamazać.
- **Wklej obraz** ze schowka prosto na płótno edytora.
- **Kopiuj jako** Markdown, HTML, data URI lub ścieżkę pliku — plus **Pokaż kod QR** dla wysłanego linku.
- **Przeciągnij zrzut** z historii lub z miniatury powiadomienia prosto do innej aplikacji — menedżera plików, czatu czy edytora.
- Historia dostaje dzielony przycisk kopiowania (obraz z lewej, link z prawej) i menu **Więcej** przy każdej pozycji.
- Na GNOME powiadomienie o zrzucie to teraz ta sama stylowana karta co wszędzie, z działającymi przyciskami akcji.
- Kliknij etykietę wersji, aby przeczytać te informacje o wydaniu.
- Wiersz poleceń: `--delay SEKUNDY`, `--output <ścieżka>` (`-` dla stdout), `--format png|jpg|webp` oraz `--measure`.

**Naprawiono**
- Na GNOME zmiana kolejności przypiętych ikon w Ubuntu Dock podczas nagrywania obszaru już działa. Ramka nagrywania jest rysowana jako cztery cienkie krawędzie wokół obszaru zamiast jednej pełnoekranowej powierzchni, więc nic nie zasłania reszty pulpitu (a przeciąganie wewnątrz nagrywanego obszaru też działa). Dzięki zgłoszeniu użytkownika, które to namierzyło.
- **Podfoldery z datą** grupują teraz także nagrania, nie tylko zrzuty.
- Dźwięk zrzutu jest teraz dźwiękiem powiadomienia, więc nie zakłóca innego audio (np. przechwytywania udostępniania ekranu na Discordzie).
- Znaczniki pomocy **„?”** w Ustawieniach nie nachodzą już na szerokie pola, a zduplikowany kursor ze znakiem zapytania zniknął.

**Usunięto**
- **Inteligentny wybór** (eksperymentalna opcja klikania w obiekt w Ustawieniach › Przechwytywanie) zniknął. Wykrywanie było czysto wizualne i nigdy nie rozpoznawało okien ani elementów na tyle pewnie, by je zostawić. Zaznaczanie obszaru przeciąganiem działa bez zmian.
- Akcja **Usuń tło** w edytorze i ustawienia modelu U-2-Net zniknęły wraz z opcjonalną zależnością onnxruntime. Inteligentna gumka działa bez zmian.
- Ściągawka skrótów **Ctrl + /** zniknęła — etykieta wersji otwiera teraz te informacje o wydaniu. Same skróty okna zostają: **Ctrl + W**, **Ctrl + Q**, **Ctrl + ,** oraz **Ctrl + 1 … Ctrl + 6**.

## 0.7b

### English
**New**
- Window keyboard shortcuts, with a **Ctrl + /** cheat sheet listing them.
- Configurable capture and recording **sound cues**: new presets, a separate recording-start sound, and a playback-volume control.
- **Update-channel** selection in Settings, with automatic background updates and pre-releases offered on the beta channel.
- Smarter **save routing** for screenshots: a record countdown, ask-where-to-save, date subfolders, strip-metadata, and a save-as dialog.
- **Select text (OCR)** drag-selection is character-precise — like the Windows Snipping Tool.

### Polski
**Nowości**
- Skróty klawiszowe okna, ze ściągawką **Ctrl + /**.
- Konfigurowalne **dźwięki** przechwytywania i nagrywania: nowe warianty, osobny dźwięk startu nagrywania i regulacja głośności.
- Wybór **kanału aktualizacji** w Ustawieniach, z automatycznymi aktualizacjami w tle i wydaniami wstępnymi na kanale beta.
- Inteligentniejsze **kierowanie zapisu** zrzutów: odliczanie przed nagraniem, pytanie gdzie zapisać, podfoldery z datą, usuwanie metadanych i okno zapisu jako.
- **Zaznaczanie tekstu (OCR)** przeciąganiem jest precyzyjne co do znaku — jak w narzędziu Wycinanie w Windows.

## 0.6.4

### English
**Fixed**
- Fixed the GNOME capture path.

**New**
- Per-distribution RPM packages.
- openSUSE Leap 15.6 build support.

### Polski
**Poprawki**
- Naprawiono ścieżkę przechwytywania na GNOME.

**Nowości**
- Pakiety RPM dla poszczególnych dystrybucji.
- Obsługa budowania openSUSE Leap 15.6.

## 0.6.3

### English
**New**
- Step-marker size control.
- GNOME record-region border.
- openSUSE packages via COPR.

### Polski
**Nowości**
- Kontrola rozmiaru znacznika kroku.
- Ramka regionu nagrywania na GNOME.
- Pakiety openSUSE przez COPR.

## 0.6.2

### English
**New**
- The **System** theme now mirrors the live KDE color scheme.

### Polski
**Nowości**
- Motyw **System** odzwierciedla teraz kolorystykę KDE na żywo.

## 0.6.1

### English
**Fixed**
- Fixed global shortcuts on GNOME.

### Polski
**Poprawki**
- Naprawiono skróty globalne na GNOME.

## 0.6.0

### English
**New**
- Automatic in-place updates.
- OBS repository channel.

### Polski
**Nowości**
- Automatyczne aktualizacje w miejscu.
- Kanał repozytorium OBS.
