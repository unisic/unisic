# Unisic — Release notes

Per-version notes shown in-app when the version label is clicked. The section
whose `##` heading matches the running version (see `UNISIC_VERSION`) is shown;
within it, the `### English` / `### Polski` block for the toggled language is
displayed. Keep the newest version at the top; each version is translated as a
whole per release (not per individual change).

## 0.7

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
- **Ctrl + W** closes the editor and the trim window too, not just the main window.
- The **trim window** now wears the same styled title bar as the rest of Unisic (and follows the system-decoration setting in Appearance).
- **Distance from the screen edge** for the capture card (Settings › Notifications) — raise it when a dock or panel sits where the card lands.
- **Live card preview**: open the style, position or distance dropdown for the capture card and the real card appears on screen — walking the list walks the card through the options, so you see each one where your next capture's card will actually sit before you pick it.
- **Pick the capture card's buttons** (Settings › Notifications): switch off the actions you never use and the rest spread out over the freed room.
- **Trim straight from the notification**: a finished recording's card now offers Trim, next to the buttons a screenshot gets — no detour through History.
- **Edit** is a page of its own now: open a picture you already have in the editor, or a video in the trim window. Unisic edits your own files, not just its own captures.
- Command line: `--delay SECONDS`, `--output <path>` (`-` for stdout), `--format png|jpg|webp`, and `--measure`.

**Fixed**
- On GNOME, reordering pinned Ubuntu Dock icons while recording a region now works. The record frame is drawn as four thin edges around the region instead of one full-screen surface, so nothing covers the rest of the desktop (and drags inside the recorded region work too). Thanks to the user report that pinned this down.
- **Date subfolders** now bucket recordings too, not just screenshots.
- On GNOME, the capture card no longer lands under the top bar or a dock: it is now placed inside the desktop's work area instead of the raw screen rectangle. (Panels already pushed the card aside everywhere layer-shell is available — KDE, wlroots, COSMIC.)
- The screenshot sound cue is now a notification event sound, so it no longer disrupts other audio (such as a Discord screen-share capture).
- Settings help **“?”** badges no longer overlap wide fields, and the duplicate question-mark mouse cursor is gone.
- On desktops with no ScreenCast portal backend (Cinnamon, MATE, XFCE), the recording pages claimed Unisic "was built without PipeWire support" — wrong, and misleading when a PipeWire process is plainly running. They now name the actual missing piece: the portal that asks for permission and opens the stream.

**Removed**
- **Smart pick** (the experimental click-to-pick-an-object option in Settings › Capture) is gone. Detection was purely visual and never recognized windows and elements reliably enough to keep. Region selection by dragging is unchanged.
- The editor's **Remove background** action and the U-2-Net model settings are gone, along with the optional onnxruntime dependency. The smart eraser is unaffected.
- The **0x0.st** built-in upload host is gone — it rejected the uploads, so it only ever produced errors. If it was your selected server, Unisic switches you back to catbox.moe; your own destinations are untouched.

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
- **Ctrl + W** zamyka też okno edytora i okno przycinania, nie tylko okno główne.
- **Okno przycinania** ma teraz ten sam stylowany pasek tytułu co reszta Unisic (i słucha ustawienia dekoracji systemowych w Wyglądzie).
- **Odległość od krawędzi ekranu** dla karty przechwytywania (Ustawienia › Powiadomienia) — zwiększ ją, gdy dok lub panel stoi tam, gdzie ląduje karta.
- **Podgląd karty na żywo**: rozwiń listę stylu, pozycji lub odległości karty przechwytywania, a prawdziwa karta pojawi się na ekranie — przechodzenie po opcjach przeprowadza kartę przez kolejne warianty, więc każdy widzisz dokładnie tam, gdzie stanie karta następnego zrzutu, zanim go wybierzesz.
- **Wybierz przyciski karty przechwytywania** (Ustawienia › Powiadomienia): wyłącz akcje, których nie używasz, a reszta rozłoży się na zwolnionym miejscu.
- **Przytnij wprost z powiadomienia**: karta gotowego nagrania ma teraz przycisk Przytnij, obok tych, które dostaje zrzut — bez wycieczki przez Historię.
- **Edytuj** to teraz osobna zakładka: otwórz zdjęcie, które już masz, w edytorze, albo film w oknie przycinania. Unisic edytuje Twoje własne pliki, nie tylko własne zrzuty.
- Wiersz poleceń: `--delay SEKUNDY`, `--output <ścieżka>` (`-` dla stdout), `--format png|jpg|webp` oraz `--measure`.

**Naprawiono**
- Na GNOME zmiana kolejności przypiętych ikon w Ubuntu Dock podczas nagrywania obszaru już działa. Ramka nagrywania jest rysowana jako cztery cienkie krawędzie wokół obszaru zamiast jednej pełnoekranowej powierzchni, więc nic nie zasłania reszty pulpitu (a przeciąganie wewnątrz nagrywanego obszaru też działa). Dzięki zgłoszeniu użytkownika, które to namierzyło.
- **Podfoldery z datą** grupują teraz także nagrania, nie tylko zrzuty.
- Na GNOME karta przechwytywania nie ląduje już pod górnym paskiem ani pod dokiem: jest teraz umieszczana w obszarze roboczym pulpitu, a nie w surowym prostokącie ekranu. (Tam, gdzie jest layer-shell — KDE, wlroots, COSMIC — panele i tak odsuwały kartę.)
- Dźwięk zrzutu jest teraz dźwiękiem powiadomienia, więc nie zakłóca innego audio (np. przechwytywania udostępniania ekranu na Discordzie).
- Znaczniki pomocy **„?”** w Ustawieniach nie nachodzą już na szerokie pola, a zduplikowany kursor ze znakiem zapytania zniknął.
- Na pulpitach bez backendu portalu ScreenCast (Cinnamon, MATE, XFCE) strony nagrywania twierdziły, że Unisic „zbudowano bez obsługi PipeWire” — nieprawda, i mylące, gdy proces PipeWire jawnie działa. Teraz nazywają to, czego naprawdę brakuje: portalu, który pyta o zgodę i otwiera strumień.

**Usunięto**
- **Inteligentny wybór** (eksperymentalna opcja klikania w obiekt w Ustawieniach › Przechwytywanie) zniknął. Wykrywanie było czysto wizualne i nigdy nie rozpoznawało okien ani elementów na tyle pewnie, by je zostawić. Zaznaczanie obszaru przeciąganiem działa bez zmian.
- Akcja **Usuń tło** w edytorze i ustawienia modelu U-2-Net zniknęły wraz z opcjonalną zależnością onnxruntime. Inteligentna gumka działa bez zmian.
- Wbudowany serwer **0x0.st** zniknął — odrzucał wysyłki, więc dawał wyłącznie błędy. Jeśli był Twoim wybranym serwerem, Unisic przełącza z powrotem na catbox.moe; Twoje własne serwery zostają nietknięte.

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
