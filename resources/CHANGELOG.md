# Unisic — Release notes

Per-version notes shown in-app when the version label is clicked. The section
whose `##` heading matches the running version (see `UNISIC_VERSION`) is shown;
within it, the `### English` / `### Polski` block for the toggled language is
displayed. Keep the newest version at the top; each version is translated as a
whole per release (not per individual change).

## 0.7.2b

### English
**New**
- **Pause and resume a recording**: a pause button on the recording bar (the region frame reads **PAUSED**) — the paused span is cut out of the finished file, video and audio together, so the recording carries on exactly where you left off. Works for screen, region and window recordings and GIFs, with or without audio (not for instant replay).
- **Eyedropper tool** (editor and capture overlay, shortcut **I**): click any pixel to adopt its colour as the current annotation colour.
- **Export selected captures to a ZIP**: pick several in History, then **Export ZIP** bundles them into one archive to save anywhere.

**Improved**
- **OCR detects the language for you**: on by default, it now recognizes text using every installed Tesseract language pack, so you no longer have to type language codes. Turn it off in Settings › OCR to pin a specific, faster set.

### Polski
**Nowości**
- **Pauza i wznawianie nagrywania**: przycisk pauzy na pasku nagrywania (ramka regionu pokazuje **PAUZA**) — wstrzymany fragment jest wycinany z gotowego pliku, obraz i dźwięk razem, więc nagranie kontynuuje dokładnie tam, gdzie je zatrzymano. Działa dla nagrań ekranu, regionu i okna oraz GIF-ów, z dźwiękiem lub bez (nie dla instant replay).
- **Narzędzie Pipeta** (edytor i nakładka przechwytywania, skrót **I**): kliknij dowolny piksel, aby przejąć jego kolor jako bieżący kolor adnotacji.
- **Eksport zaznaczonych zrzutów do ZIP**: zaznacz kilka w Historii, a **Eksport ZIP** spakuje je do jednego archiwum, które zapiszesz gdziekolwiek.

**Ulepszone**
- **OCR sam wykrywa język**: domyślnie włączony, rozpoznaje teraz tekst przy użyciu wszystkich zainstalowanych pakietów językowych Tesseract, więc nie musisz już wpisywać kodów języków. Wyłącz to w Ustawienia › OCR, aby ustawić konkretny, szybszy zestaw.

## 0.7.1

### English
**New**
- **Pixel loupe while selecting a region**: a magnifier follows the cursor with a zoomed pixel grid, the exact hovered pixel highlighted, and its position and colour — so a selection edge lands on exactly the pixel you mean. Hold **Ctrl** and scroll to change the magnification; toggle it in Settings › Capture overlay.
- **Magnifier tool** (editor and capture overlay, shortcut **Z**): drag over a detail and a loupe appears with a 2× enlarged copy of it. Move the loupe anywhere, resize it to change the magnification, restyle its border — the source area stays anchored, so it keeps showing the pixels you picked.
- **Ctrl + scroll zooms at the cursor** in the editor — the pixel under the pointer stays put, so you zoom into what you are aiming at instead of the window centre.

**Improved**
- The **trim window now shows what you are cutting**: the timeline is a filmstrip of the recording, everything outside your selection is dimmed, and dragging a handle scrubs the preview to that exact frame.
- **Play previews the cut, not the file** — playback stays inside the selection and loops it (**L** toggles the loop).
- **History finds things now**: search by file name or link, and filter by images, GIFs, recordings, **instant-replay clips** (their own category — they are ordinary .mp4 files, so nothing but Unisic can tell them apart), starred or uploaded.
- **Work on many captures at once** — click to select (**Ctrl** adds, **Shift** picks a range, **Ctrl + A** takes all), then star, copy the paths, upload or delete the whole selection in one go. Starred captures are still protected from deletion.
- **The History grid takes the keyboard**: arrows move, **Enter** opens the floating preview, **Ctrl + C** copies, **Delete** deletes. Clicking a tile now opens the preview instead of doing nothing.
- **Redesigned the tile actions**: they sit in a strip along the bottom edge on hover, so the thumbnail you are aiming at stays visible. Tiles fill the window width evenly, a floating date tells you where you are while scrolling, and each tile shows its size and dimensions — the date on a tile now shrinks to just a time for today's captures, so the details fit instead of being cut off.

**Removed**
- **The quick task chooser** (its own hotkey, by default Meta + Shift + Space) is gone: the tray icon's menu already offers every capture mode it did. An upgraded install drops the leftover key grab and its shortcut entry by itself.

**Fixed**
- **The smart eraser behaved like a smudge tool**: it filled the whole stroke with a single averaged colour, sampled from the stroke's own border — so it went grey wherever the border clipped the thing you were erasing, and left a visibly flat patch on any background that was not one solid colour. It now rebuilds the background from the pixels around the stroke — following a gradient through it, and ignoring whatever the stroke's edge happens to cut across, so scrubbing several overlapping strokes over a line of text leaves the background instead of smears and bright bands.
- **The Callout tool wore the wrong icon** — a generic info symbol borrowed from the system icon theme instead of a speech bubble. It has its own icon now.
- **Uploading to Imgur never worked**: the built-in destination shipped a placeholder Client-ID, so Imgur rejected every upload. Unisic now asks for your own Client-ID (Servers › Imgur › Edit — register a free one at api.imgur.com/oauth2/addclient), repairs the broken stored destination, and tells you what is missing instead of failing silently. Uploads stay anonymous — they never appear in the ID owner's gallery.
- **Trimming a GIF produced the whole GIF**, ignoring the range you picked. GIFs are now re-rendered through the same palette pipeline a recording uses, so the cut lands on the frame you chose.
- **A trimmed WebM started up to several seconds early**: cutting by stream copy can only start on a keyframe. Trimming now re-encodes the selection by default, so the saved file starts on the exact frame the window showed. The old instant copy is still there as **Fast lossless cut** — with it on, the start visibly snaps onto a keyframe (marked with ticks) so the preview keeps matching the file.
- **Trimming without a video preview could ignore keyframe snapping**: with qt6-qtmultimedia missing, the fallback Start slider bypassed the snap — with **Fast lossless cut** on, the file silently began at an earlier keyframe than the window showed.
- **A lossless cut landed on the wrong keyframe for recordings that do not start at zero** (some phone/OBS files): keyframe times are now measured from the start of the file, the same way the timeline and ffmpeg measure.
- **The "Last frame" preview showed a frame the saved file did not contain** — the out-point is the first excluded frame. Dragging the end handle now previews the actual last frame of the cut.
- **Trimming an imported video with an odd width or height failed** ("width not divisible by 2"): the re-encode now trims at most one edge pixel, the same rule the recorder applies.
- **Quitting mid-way through a GIF trim left a stray palette file next to the recording** — the palette is scratch now, lives in the cache and cleans itself up.
- **The trim window froze on long clips with very frequent keyframes** (all-intra / short-GOP files): the timeline now draws only as many keyframe ticks as fit its pixels; snapping still uses the full list.
- **At the trim window's minimum width the cut-mode description ran underneath the Cancel/Save buttons**; it wraps now.

### Polski
**Nowości**
- **Lupa pikselowa przy zaznaczaniu regionu**: lupa podąża za kursorem z powiększoną siatką pikseli, podświetlonym pikselem pod kursorem oraz jego pozycją i kolorem — krawędź zaznaczenia trafia dokładnie w ten piksel, o który chodzi. Przytrzymaj **Ctrl** i przewiń, aby zmienić powiększenie; przełącznik w Ustawienia › Nakładka przechwytywania.
- **Narzędzie Lupa** (edytor i nakładka przechwytywania, skrót **Z**): przeciągnij po detalu, a nad nim pojawi się lupa z jego 2× powiększoną kopią. Przesuwaj ją gdziekolwiek, zmieniaj rozmiar (zmienia powiększenie), przestylizuj ramkę — obszar źródłowy zostaje zakotwiczony, więc lupa cały czas pokazuje wybrane piksele.
- **Ctrl + scroll przybliża pod kursorem** w edytorze — piksel pod wskaźnikiem stoi w miejscu, więc przybliżasz to, w co celujesz, a nie środek okna.

**Ulepszone**
- **Okno przycinania pokazuje, co tniesz**: oś czasu to pasek miniatur nagrania, wszystko poza zaznaczeniem jest przygaszone, a przeciąganie uchwytu przewija podgląd dokładnie do tej klatki.
- **Odtwarzanie pokazuje wycinek, nie cały plik** — playback trzyma się zaznaczenia i zapętla je (**L** przełącza pętlę).
- **W Historii da się wreszcie czegoś szukać**: po nazwie pliku albo linku, plus filtry — obrazy, GIF-y, nagrania, **klipy z instant replay** (osobna kategoria — to zwykłe pliki .mp4, więc nic poza Unisic ich nie odróżni), oznaczone, wysłane.
- **Praca na wielu zrzutach naraz** — klikaj, żeby zaznaczać (**Ctrl** dokłada, **Shift** bierze zakres, **Ctrl + A** wszystko), a potem jednym ruchem oznacz, skopiuj ścieżki, wyślij albo usuń całe zaznaczenie. Zrzuty oznaczone nadal są chronione przed usunięciem.
- **Siatka Historii słucha klawiatury**: strzałki przechodzą po kafelkach, **Enter** otwiera pływający podgląd, **Ctrl + C** kopiuje, **Delete** usuwa. Kliknięcie kafelka otwiera podgląd, zamiast nie robić nic.
- **Przeprojektowane akcje na kafelku**: siedzą w pasku przy dolnej krawędzi po najechaniu, więc miniatura, w którą celujesz, zostaje widoczna. Kafelki równo wypełniają szerokość okna, pływająca data mówi, gdzie jesteś przy przewijaniu, a każdy kafelek pokazuje swój rozmiar i wymiary — data na kafelku kurczy się do samej godziny dla dzisiejszych zrzutów, więc szczegóły mieszczą się zamiast być ucinane.

**Usunięte**
- **Wybór szybkiego zadania** (osobny skrót, domyślnie Meta + Shift + Spacja) znika: menu ikony w zasobniku i tak daje wszystkie tryby przechwytywania, które oferował. Zaktualizowana instalacja sama zwalnia zostawiony skrót i usuwa jego wpis.

**Naprawione**
- **Inteligentna gumka działała jak rozmazywanie**: wypełniała całe pociągnięcie jednym uśrednionym kolorem, próbkowanym z własnej krawędzi — więc szarzała wszędzie tam, gdzie krawędź przecinała wymazywany obiekt, i zostawiała płaską łatę na każdym tle, które nie było jednolite. Teraz odbudowuje tło z pikseli wokół pociągnięcia — przeciąga przez nie gradient i ignoruje to, co krawędź pociągnięcia akurat przecina, więc kilka nachodzących pociągnięć po linijce tekstu zostawia tło, a nie smugi i jasne pasma.
- **Narzędzie Dymek miało nie swoją ikonę** — ogólny symbol informacji pożyczony z systemowego motywu ikon zamiast dymka. Ma już własną.
- **Wysyłka na Imgur nigdy nie działała**: wbudowana destynacja miała zaślepkę zamiast Client-ID, więc Imgur odrzucał każdy upload. Unisic prosi teraz o twój własny Client-ID (Serwery › Imgur › Edytuj — darmowy do zarejestrowania na api.imgur.com/oauth2/addclient), naprawia zepsutą zapisaną destynację i mówi, czego brakuje, zamiast po cichu zawodzić. Wysyłki pozostają anonimowe — nie trafiają do galerii właściciela ID.
- **Przycinanie GIF-a zapisywało cały GIF**, ignorując wybrany zakres. GIF-y są teraz renderowane od nowa tym samym torem palety co nagrania, więc cięcie trafia w wybraną klatkę.
- **Przycięty WebM zaczynał się nawet o kilka sekund za wcześnie**: cięcie przez kopiowanie strumienia może zacząć się tylko na klatce kluczowej. Przycinanie domyślnie przekodowuje zaznaczenie, więc zapisany plik zaczyna się na dokładnie tej klatce, którą pokazało okno. Dawne błyskawiczne kopiowanie zostaje jako **Szybkie cięcie bezstratne** — przy nim początek widocznie przeskakuje na klatkę kluczową (oznaczone kreskami), więc podgląd nadal zgadza się z plikiem.
- **Przycinanie bez podglądu wideo mogło pominąć przyciąganie do klatek kluczowych**: bez qt6-qtmultimedia zapasowy suwak Start omijał przyciąganie — przy włączonym **Szybkim cięciu bezstratnym** plik po cichu zaczynał się na wcześniejszej klatce kluczowej, niż pokazywało okno.
- **Cięcie bezstratne trafiało w złą klatkę kluczową dla nagrań, które nie zaczynają się od zera** (niektóre pliki z telefonów/OBS): czasy klatek kluczowych są teraz liczone od początku pliku — tak samo, jak liczy oś czasu i ffmpeg.
- **Podgląd „Ostatnia klatka" pokazywał klatkę, której zapisany plik nie zawierał** — punkt końcowy to pierwsza wykluczona klatka. Przeciąganie uchwytu końca pokazuje teraz faktycznie ostatnią klatkę wycinka.
- **Przycinanie zaimportowanego wideo o nieparzystej szerokości lub wysokości kończyło się błędem** („width not divisible by 2"): przekodowanie przycina teraz najwyżej jeden piksel krawędzi — ta sama zasada, którą stosuje rejestrator.
- **Wyjście z aplikacji w trakcie przycinania GIF-a zostawiało zabłąkany plik palety obok nagrania** — paleta jest teraz plikiem roboczym, mieszka w cache i sama się sprząta.
- **Okno przycinania zamarzało na długich klipach z bardzo częstymi klatkami kluczowymi** (pliki all-intra / z krótkim GOP): oś czasu rysuje teraz tylko tyle kresek klatek kluczowych, ile mieści się w jej pikselach; przyciąganie nadal używa pełnej listy.
- **Przy minimalnej szerokości okna przycinania opis trybu cięcia wchodził pod przyciski Anuluj/Zapisz**; teraz się zawija.

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
