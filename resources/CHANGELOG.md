# Unisic - Release notes

Per-version notes shown in-app when the version label is clicked. The section
whose `##` heading matches the running version (see `UNISIC_VERSION`) is shown;
within it, the `### English` / `### Polski` block for the toggled language is
displayed. Keep the newest version at the top; each version is translated as a
whole per release (not per individual change).

## 0.7.4

### English
**New**
- **A setup flow on the first launch**: Unisic now opens with a short walkthrough inside its own window - theme (every installed one, including the Catppuccin/Dracula/Nord/Gruvbox files), language and window decoration; what happens after each capture (save, clipboard, editor); a few behaviours worth deciding on (ask-where-to-save, stay in the tray, start at login); the capture card, with a live preview right in the window where you pick its style and corner and click the action buttons to add or remove them; and the capture shortcuts, which can be recorded on the spot. The card's action buttons are live in that preview: hover one to see what it does, click to remove or restore it, and hold and drag to reorder them, with the row animating each swap. Every step is optional: skip it and the shipped defaults stay. It only ever appears on a fresh install - updating an existing Unisic never interrupts you with it - and you can reopen it any time from Settings › General › Diagnostics.

**Fixed**
- **The KDE title bar no longer lingers after switching to the styled decoration**: turning the system window decoration off left KWin's title bar painted on top of Unisic's own until you clicked outside the window. The window now re-maps itself so the change takes effect immediately.
- **Click selects the whole screen**: on the region-selection overlay, a plain click (no drag) now selects the entire screen you clicked on - annotate and confirm as usual. With “Capture on release” enabled the click captures immediately. Dragging still selects a region, exactly as before.
- **Keep region between captures** (Settings › Capture): the selection overlay opens with your last region already pre-selected on its screen - repeating a shot is just Enter, or a drag to adjust. The rectangle survives an app restart.
- **Full screen captures** (Settings › Capture): choose what the full-screen capture takes - all monitors stitched together (the default, as before) or only the screen under the cursor. Applies to the hotkey, the tray entry and `unisic --fullscreen` alike.
- **Screenshots and recordings no longer crash the app on COSMIC**: on System76's COSMIC desktop the capture card, the region-selection overlay and the record-region frame each disconnected Unisic from the display the instant they closed - a quirk in how this compositor tears down these on-top surfaces - so every screenshot and every recording took the whole app down with it. Unisic now draws those surfaces the same way it does on GNOME, and both capture and recording run through cleanly.

**Changed**
- **Fewer hotkeys**: “Capture screen under cursor” and “Re-capture last region” are no longer assignable hotkeys - the two new capture preferences cover both jobs with the keys you already have. The features themselves remain in the tray menu and on the command line (`unisic --monitor`, `unisic --recapture`); any key you had bound to them is released cleanly on upgrade.
- **Scroll to zoom the pixel loupe, no modifier**: on the region overlay the magnifier now zooms with a plain scroll (Ctrl is no longer needed) in single, pixel-perfect steps. Scroll all the way out and the loupe hides itself so it stops covering what you are aiming at; scroll back in to bring it straight back.

**Improved**
- **WebM recordings convert several times faster on recent NVIDIA GPUs**: WebM can't carry H.264, so hardware encoding never applied to it and every WebM conversion ran on the CPU - a long recording could take longer to convert than to record. On GPUs with an AV1 encoder (GeForce RTX 40 series and newer) Unisic now converts WebM with AV1 on the GPU - measured about 3.5× faster, with slightly better quality. Everywhere else (and with the encoder set to Software) WebM stays software VP9, and MP4 is unchanged.
- **A bigger, centered app logo in the sidebar**: the logo grew from a small inline icon to a centered emblem above the app name, filling the space the sidebar always had to spare.
- **Settings now look like the welcome screen**: every setting is its own card - the label, a one-line explanation right under it, and the control - grouped under plain section headers, matching the first-run walkthrough. Settings › Notifications carries the very same live capture card the welcome flow uses: it shows the real card in your chosen style and corner right there in the window, where you click the action buttons to hide or restore them and drag to reorder - no more throwing a preview onto the corner of your screen.

**Fixed**
- **A recording can no longer hang forever on "Encoding…"**: if the audio source died mid-recording (a PipeWire link failure), the encoder inside ffmpeg could deadlock waiting for sound that would never arrive - the recording never finished, and memory grew without bound until the app was killed, taking the recording with it. Unisic now watches the stop for real progress, force-ends a wedged encoder after ~25 seconds, and salvages everything recorded up to the stall into a normal finished video instead of discarding it.

### Polski
**Nowe**
- **Konfiguracja przy pierwszym uruchomieniu**: Unisic otwiera się teraz krótkim przewodnikiem wewnątrz własnego okna - motyw (każdy zainstalowany, łącznie z plikami Catppuccin/Dracula/Nord/Gruvbox), język i dekoracja okna; co dzieje się po każdym przechwyceniu (zapis, schowek, edytor); kilka zachowań wartych decyzji (pytanie o miejsce zapisu, pozostawanie w zasobniku, start przy logowaniu); karta po przechwyceniu z żywym podglądem w oknie, gdzie wybierasz jej styl i róg oraz klikasz przyciski akcji, żeby je dodać lub usunąć; a także skróty przechwytywania, które można od razu nagrać. Przyciski akcji karty są w tym podglądzie żywe: najedź, aby zobaczyć, co robią, kliknij, aby usunąć lub przywrócić, przytrzymaj i przeciągnij, aby zmienić kolejność - każda zamiana jest animowana. Każdy krok jest opcjonalny: pomiń go, a zostaną ustawienia domyślne. Pojawia się wyłącznie przy świeżej instalacji - aktualizacja istniejącego Unisica nigdy nim nie przerywa - a otworzysz go ponownie w Ustawienia › Ogólne › Diagnostyka.

**Naprawione**
- **Pasek tytułu KDE nie zostaje już po przejściu na dekorację stylizowaną**: wyłączenie systemowej dekoracji okna zostawiało pasek tytułu KWin narysowany na własnym pasku Unisic, dopóki nie kliknęło się poza oknem. Okno przemapowuje się teraz samo, więc zmiana działa od razu.
- **Kliknięcie zaznacza cały ekran**: na nakładce wyboru obszaru samo kliknięcie (bez przeciągania) zaznacza teraz cały ekran, na który kliknięto - adnotuj i potwierdź jak zwykle. Przy włączonym „Przechwytuj po puszczeniu" kliknięcie przechwytuje od razu. Przeciąganie nadal zaznacza obszar, dokładnie jak wcześniej.
- **Zachowaj obszar między zrzutami** (Ustawienia › Przechwytywanie): nakładka wyboru otwiera się z ostatnim obszarem już zaznaczonym na swoim ekranie - powtórzenie ujęcia to tylko Enter, albo przeciągnięcie, aby dopasować. Prostokąt przetrwa restart aplikacji.
- **Przechwytywanie pełnego ekranu** (Ustawienia › Przechwytywanie): wybierz, co obejmuje zrzut pełnego ekranu - wszystkie monitory zszyte w jedno (domyślnie, jak dotąd) albo tylko ekran pod kursorem. Dotyczy skrótu, pozycji w zasobniku i `unisic --fullscreen`.
- **Zrzuty i nagrania nie zamykają już aplikacji na COSMIC**: na pulpicie COSMIC (System76) karta po przechwyceniu, nakładka wyboru obszaru i ramka nagrywania rozłączały Unisica z ekranem w chwili zamknięcia - to cecha tego kompozytora w sposobie usuwania takich nakładek na wierzchu - więc każdy zrzut i każde nagranie kładło całą aplikację. Unisic rysuje teraz te nakładki tak samo jak na GNOME i przechwytywanie oraz nagrywanie działają do końca.

**Zmienione**
- **Mniej skrótów**: „Przechwyć ekran pod kursorem" i „Ponów ostatni obszar" nie są już przypisywalnymi skrótami - dwie nowe preferencje przechwytywania załatwiają oba zadania klawiszami, które już masz. Same funkcje pozostają w menu zasobnika i w wierszu poleceń (`unisic --monitor`, `unisic --recapture`); klawisz przypisany do nich wcześniej jest czysto zwalniany przy aktualizacji.
- **Przewijanie przybliża lupę pikseli, bez modyfikatora**: na nakładce wyboru obszaru lupa przybliża się teraz zwykłym przewijaniem (Ctrl nie jest już potrzebny), pojedynczymi, dokładnymi krokami. Oddal ją całkowicie, a schowa się sama, żeby nie zasłaniać tego, co bierzesz na cel; przewiń z powrotem, aby ją od razu przywrócić.

**Ulepszone**
- **Nagrania WebM konwertują się kilkukrotnie szybciej na nowszych kartach NVIDIA**: WebM nie może zawierać H.264, więc enkodowanie sprzętowe nigdy go nie obejmowało i każda konwersja WebM szła na CPU - długie nagranie potrafiło konwertować się dłużej, niż trwało. Na kartach z enkoderem AV1 (GeForce RTX serii 40 i nowsze) Unisic konwertuje teraz WebM przez AV1 na GPU - zmierzone około 3,5× szybciej, przy nieco lepszej jakości. Wszędzie indziej (oraz przy enkoderze ustawionym na Programowy) WebM pozostaje programowym VP9, a MP4 bez zmian.
- **Większe, wycentrowane logo aplikacji w pasku bocznym**: logo urosło z małej ikonki w rzędzie do wycentrowanego emblematu nad nazwą aplikacji, wypełniając miejsce, które pasek boczny zawsze miał wolne.
- **Ustawienia wyglądają teraz jak ekran powitalny**: każde ustawienie to osobna karta - etykieta, jednolinijkowe wyjaśnienie tuż pod nią i kontrolka - pogrupowane pod zwykłymi nagłówkami sekcji, spójnie z przewodnikiem pierwszego uruchomienia. Ustawienia › Powiadomienia mają dokładnie tę samą żywą kartę przechwycenia co przewodnik: pokazuje prawdziwą kartę w wybranym stylu i rogu wprost w oknie, gdzie klikasz przyciski akcji, żeby je ukryć lub przywrócić, i przeciągasz, by zmienić kolejność - koniec z rzucaniem podglądu w róg ekranu.

**Naprawione**
- **Nagranie nie może już wisieć w nieskończoność na „Enkodowanie…"**: gdy źródło dźwięku padło w trakcie nagrywania (awaria linku PipeWire), enkoder wewnątrz ffmpeg potrafił się zakleszczyć, czekając na dźwięk, który nigdy nie nadejdzie - nagranie nigdy się nie kończyło, a pamięć rosła bez ograniczeń, aż aplikacja została ubita razem z nagraniem. Unisic pilnuje teraz realnego postępu przy zatrzymaniu, wymusza zakończenie zaklinowanego enkodera po ~25 sekundach i ratuje wszystko nagrane do momentu zacięcia jako normalnie ukończone wideo, zamiast je odrzucać.

## 0.7.3

### English
**New**
- **First-run system check**: on first launch Unisic now points out any optional tool that isn't installed - FFmpeg for recording and GIF export, wl-clipboard for the most reliable clipboard copy, a Tesseract language pack for text recognition (OCR) - and tells you how to install each. Unisic still runs on the built-in Wayland APIs alone; these just unlock more. Re-run the check any time from Settings › General › Diagnostics.
- **Copy diagnostics**: a one-click text summary of your setup - Unisic and Qt versions, desktop and session, compiled-in features and detected tools - for a bug report, in Settings › General › Diagnostics. Nothing is sent anywhere; you paste it into an issue yourself.
- **Capture the screen under the cursor**: one monitor, the one the pointer is on - the multi-monitor middle ground between Region and Full screen. On KDE the compositor itself resolves which screen the pointer is on, so it works from a global hotkey too. In the tray menu, as an assignable hotkey (Settings › Hotkeys, unbound by default) and on the command line as `unisic --monitor`. Runs the full-screen task preset.
- **Re-capture the last region**: takes the exact rectangle of your most recent region screenshot again, without opening the selection overlay - for documenting something that changes over time. The rectangle survives an app restart, and the tray entry stays greyed out until there is a region to repeat. In the tray menu, as an assignable hotkey (unbound by default) and on the command line as `unisic --recapture`. Runs the region task preset.
- **Show pressed keys in recordings** (Settings › Recording): a screenkey-style badge at the bottom of the recording shows what you press - shortcuts like “Ctrl+Shift+T” with held modifiers, and a ×N counter when you hit the same key repeatedly. Works in GIF and video recordings, with or without the cursor overlay. Like the click ripple, it needs access to input devices (the toggle explains how to grant it) and key labels use the physical (US) key legend.
- **Make your own themes** (Settings › Interface): drop a small .json file into the themes folder and it appears in the theme list - 8 colors are enough, everything else (surfaces, dividers, text shades) is derived, and any derived color can be overridden. Themes hot-reload while you edit the file, so you can tune colors live. Opening the folder for the first time creates a commented example to start from; share the file to share the theme. Themes also restyle the recording overlays: the REC badge (pill, text, red dot), the 3-2-1 countdown (disc and number) and the pressed-keys badge each have their own override keys, on every desktop (including the GNOME helper frame).

**Improved**
- **OCR auto-language now works across scripts**: with the Tesseract “osd” data pack installed, auto-detect recognizes the script of each capture - Latin, Arabic, Hebrew, Chinese/Japanese/Korean, Devanagari and more - and recognizes with just that script's installed language packs, which is faster and more accurate than loading every pack at once. Without the OSD pack it falls back to loading them all (unchanged). Install the language packs for the scripts you use.
- **Clearer empty states**: the OCR settings now say so when OCR is built in but no language pack is installed (so it can't recognize anything yet), and the Servers page explains itself when you have no upload destinations.
- **The recording frame's pause/stop buttons now visibly press** - the same springy press feedback as every other button.
- **Starting a recording or a conversion no longer briefly freezes the app**: every ffmpeg launch used to hold the interface for up to 3 seconds (and a cold hardware-encoder check for up to 8), which could read as a hang on slower machines. All of it now happens in the background.

**Fixed**
- **The recording frame no longer blocks clicks during the countdown**: while the 3-2-1 ticked before a region recording (and whenever the badge had no room to show), the invisible full-screen frame swallowed every click, so you couldn't get the recorded app ready. Everything outside the small REC badge is now always click-through.
- **Leftovers of a crashed recording are cleaned up more thoroughly**: the next recording already swept a crash-orphaned temp video from the cache folder; now it also removes an orphaned audio pipe and an unfinished instant-replay snapshot folder. The History page also frees the small per-entry size cache when entries are deleted, so a long-running session no longer grows with every capture that comes and goes.

### Polski
**Nowości**
- **Sprawdzenie systemu przy pierwszym uruchomieniu**: przy pierwszym starcie Unisic wskazuje teraz każde opcjonalne narzędzie, które nie jest zainstalowane - FFmpeg do nagrywania i eksportu GIF, wl-clipboard do najbardziej niezawodnego kopiowania, pakiet językowy Tesseract do rozpoznawania tekstu (OCR) - i podpowiada, jak je zainstalować. Unisic nadal działa na samych wbudowanych API Wayland; to tylko odblokowuje więcej. Sprawdzenie uruchomisz ponownie w każdej chwili z Ustawienia › Ogólne › Diagnostyka.
- **Kopiuj diagnostykę**: jednym kliknięciem tekstowe podsumowanie konfiguracji - wersje Unisic i Qt, pulpit i sesja, wkompilowane funkcje oraz wykryte narzędzia - do zgłoszenia błędu, w Ustawienia › Ogólne › Diagnostyka. Nic nie jest nigdzie wysyłane; sam wklejasz to do zgłoszenia.
- **Zrzut ekranu pod kursorem**: jeden monitor - ten, na którym jest wskaźnik - wielomonitorowy środek między Obszarem a Pełnym ekranem. Na KDE to kompozytor sam ustala, na którym ekranie jest wskaźnik, więc działa też z globalnego skrótu. W menu tacki, jako przypisywalny skrót (Ustawienia › Skróty, domyślnie nieprzypisany) i z wiersza poleceń jako `unisic --monitor`. Używa presetu zadań pełnego ekranu.
- **Ponów zrzut ostatniego obszaru**: wykonuje ponownie dokładnie ten sam prostokąt ostatniego zrzutu obszaru, bez otwierania nakładki wyboru - do dokumentowania czegoś, co zmienia się w czasie. Prostokąt przetrwa restart aplikacji, a pozycja w tacce pozostaje wyszarzona, dopóki nie ma obszaru do powtórzenia. W menu tacki, jako przypisywalny skrót (domyślnie nieprzypisany) i z wiersza poleceń jako `unisic --recapture`. Używa presetu zadań obszaru.
- **Pokazywanie naciskanych klawiszy w nagraniach** (Ustawienia › Nagrywanie): plakietka w stylu screenkey na dole nagrania pokazuje, co naciskasz - skróty jak „Ctrl+Shift+T” z przytrzymanymi modyfikatorami i licznik ×N przy wielokrotnym naciśnięciu tego samego klawisza. Działa w nagraniach GIF i wideo, z nakładką kursora lub bez niej. Tak jak fala przy kliknięciu wymaga dostępu do urządzeń wejściowych (przełącznik wyjaśnia, jak go przyznać), a etykiety klawiszy używają fizycznego (US) układu klawiatury.
- **Twórz własne motywy** (Ustawienia › Interfejs): wrzuć mały plik .json do folderu motywów, a pojawi się na liście motywów - 8 kolorów wystarczy, reszta (powierzchnie, separatory, odcienie tekstu) jest wyprowadzana, a każdy wyprowadzony kolor można nadpisać. Motywy przeładowują się na żywo podczas edycji pliku, więc kolory stroisz na bieżąco. Otwarcie folderu po raz pierwszy tworzy skomentowany przykład na start; udostępnij plik, aby udostępnić motyw. Motywy przemalowują też nakładki nagrywania: plakietka REC (pastylka, tekst, czerwona kropka), odliczanie 3-2-1 (dysk i cyfra) oraz plakietka naciskanych klawiszy mają własne klucze nadpisań, na każdym pulpicie (także w ramce helpera GNOME).

**Ulepszone**
- **Automatyczny język OCR działa teraz na różnych pismach**: z zainstalowanym pakietem danych „osd” Tesseract auto-wykrywanie rozpoznaje pismo każdego zrzutu - łacińskie, arabskie, hebrajskie, chińskie/japońskie/koreańskie, dewanagari i inne - i rozpoznaje tylko zainstalowanymi paczkami tego pisma, co jest szybsze i dokładniejsze niż ładowanie wszystkich naraz. Bez pakietu OSD wraca do ładowania wszystkich (bez zmian). Zainstaluj paczki językowe dla pism, których używasz.
- **Czytelniejsze puste stany**: ustawienia OCR mówią teraz wprost, gdy OCR jest wbudowany, ale nie zainstalowano pakietu językowego (więc nie może jeszcze niczego rozpoznać), a strona Serwery wyjaśnia się sama, gdy nie masz żadnych miejsc docelowych wysyłania.
- **Przyciski pauzy/stopu na ramce nagrywania widocznie się wciskają** - ta sama sprężysta reakcja na naciśnięcie co w każdym innym przycisku.
- **Start nagrywania ani konwersja nie zamrażają już na chwilę aplikacji**: każde uruchomienie ffmpeg potrafiło wstrzymać interfejs do 3 sekund (a zimne sprawdzenie enkodera sprzętowego do 8), co na wolniejszych maszynach wyglądało jak zawieszenie. Wszystko dzieje się teraz w tle.

**Naprawione**
- **Ramka nagrywania nie blokuje już kliknięć podczas odliczania**: gdy przed nagraniem obszaru tykało 3-2-1 (oraz gdy plakietka nie miała się gdzie pokazać), niewidzialna pełnoekranowa ramka połykała każde kliknięcie, więc nie dało się przygotować nagrywanej aplikacji. Wszystko poza małą plakietką REC jest teraz zawsze przepuszczalne dla kliknięć.
- **Pozostałości po nagraniu przerwanym awarią są sprzątane dokładniej**: kolejne nagranie już wcześniej usuwało osierocony tymczasowy plik wideo z folderu cache; teraz usuwa też osierocony potok audio i niedokończony folder migawki instant replay. Strona Historia zwalnia też mały podręczny rozmiar wpisu przy jego usunięciu, więc długo działająca sesja nie rośnie z każdym przychodzącym i znikającym zrzutem.

## 0.7.2

### English
**New**
- **Pause and resume a recording**: pause/resume and stop buttons live both on the recording bar and on the floating region frame itself, so you can control a recording without hunting for the main window (the frame reads **PAUSED** while held). The paused span is cut out of the finished file, video and audio together, so the recording carries on exactly where you left off. Works for screen, region and window recordings and GIFs, with or without audio (not for instant replay).
- **Eyedropper tool** (editor and capture overlay, shortcut **I**): click any pixel to adopt its colour as the current annotation colour.
- **Export selected captures to a ZIP**: pick several in History, then **Export ZIP** bundles them into one archive to save anywhere.
- **Auto-redact**: in the editor's text mode, **Auto-redact** blacks out every e-mail address, IP address or long number it recognizes - no selecting needed. It covers the matches and nothing else, and the whole sweep is a single undo. As with the existing Redact, the bars are opaque: a blur or mosaic of a password can be reversed.
- **Highlight the cursor in recordings** (Settings › Recording): a halo follows the pointer so it stays findable in a downscaled clip, and each click leaves a ripple. The pointer is smoothed so it stops jittering and stays sharp. You can recolour the halo (any colour) or turn it off. The pointer is only drawn while the desktop is actually showing one, so a game that hides the cursor stays cursor-less. Needs a desktop whose screen-cast portal can send the cursor as data (KDE does). The click ripple additionally needs access to input devices and quietly leaves only the halo without it.
- **Measure works like a ruler now**: pick Measure, then **drag** to place a measurement - **Tab** switches between a distance line and a size box (W × H), and **Ctrl+C** copies the sizes to the clipboard (format in Settings › Capture). The overlay stays up so you can keep measuring; Esc closes.
- **Style presets**: once a colour, width and font are dialled in, **+** at the end of the tool options keeps that whole style as a dot. Click the dot to bring the style back, in the editor or on the capture overlay (up to six; middle-click removes one).

**Improved**
- **Video saves much faster**: recordings now default to a working hardware encoder (VAAPI/NVENC) when your machine actually has one, instead of always encoding on the CPU. A listed-but-broken hardware encoder is detected and skipped. WebM stays software-only and is now tuned for a several-times-faster save.
- **The recorded cursor is sharp again**: the system pointer was being blended twice and came out soft; it is now drawn 1:1.
- **History's selection toolbar no longer runs off the window** in longer languages (Polish): the Delete button was pushed past the right edge. The batch actions are compact icons now, so they fit in every language.
- **A full-screen countdown is now on-screen**: the 3-2-1 before a full-screen or window recording used to be a small toast that was easy to miss - it is now a big number centred on the screen (layer-shell on KDE/wlroots, the XWayland helper on GNOME), and it disappears the instant recording begins.
- **The tray menu now offers every mode, each with an icon**: measure, select text, full-screen video and GIF, instant replay and copy last capture were only in the window before.
- **OCR detects the language for you**: on by default, it now recognizes text using every installed Tesseract language pack, so you no longer have to type language codes. Turn it off in Settings › OCR to pin a specific, faster set.

**Fixed**
- **Screenshots now land in KDE Plasma's clipboard history (Klipper)**: a copied capture was pasteable but never appeared in the clipboard applet, so it was gone the moment you copied anything else. Unisic now tags the image the way KDE expects, so Klipper keeps it in history just like Spectacle does. Thanks to Augusto-Lescano for reporting this ([#51](https://github.com/unisic/unisic/issues/51)).

### Polski
**Nowości**
- **Pauza i wznawianie nagrywania**: przyciski pauzy/wznowienia i zatrzymania są zarówno na pasku nagrywania, jak i na samej pływającej ramce regionu, więc sterujesz nagraniem bez szukania głównego okna (ramka pokazuje **PAUZA**, gdy wstrzymane). Wstrzymany fragment jest wycinany z gotowego pliku, obraz i dźwięk razem, więc nagranie kontynuuje dokładnie tam, gdzie je zatrzymano. Działa dla nagrań ekranu, regionu i okna oraz GIF-ów, z dźwiękiem lub bez (nie dla instant replay).
- **Narzędzie Pipeta** (edytor i nakładka przechwytywania, skrót **I**): kliknij dowolny piksel, aby przejąć jego kolor jako bieżący kolor adnotacji.
- **Eksport zaznaczonych zrzutów do ZIP**: zaznacz kilka w Historii, a **Eksport ZIP** spakuje je do jednego archiwum, które zapiszesz gdziekolwiek.
- **Auto-redakcja**: w trybie tekstowym edytora **Auto-redakcja** zaczernia każdy rozpoznany adres e-mail, adres IP lub długi ciąg cyfr - bez zaznaczania czegokolwiek. Zakrywa dopasowania i nic poza nimi, a całość to jedno cofnięcie. Tak jak przy dotychczasowej Redakcji paski są nieprzezroczyste: rozmycie lub mozaikę hasła da się odwrócić.
- **Podświetlanie kursora w nagraniach** (Ustawienia › Nagrywanie): poświata podąża za wskaźnikiem, więc nie gubi się w pomniejszonym klipie, a każde kliknięcie zostawia falę. Wskaźnik jest wygładzany, więc przestaje drgać i zostaje ostry. Poświatę można przekolorować (dowolny kolor) albo wyłączyć. Wskaźnik jest rysowany tylko wtedy, gdy pulpit faktycznie go pokazuje, więc gra chowająca kursor zostaje bez kursora. Wymaga pulpitu, którego portal przechwytywania ekranu potrafi wysłać kursor jako dane (KDE potrafi). Fala przy kliknięciu wymaga dodatkowo dostępu do urządzeń wejściowych i bez niego po cichu zostawia samą poświatę.
- **Miarka działa teraz jak linijka**: wybierz Miarkę, potem **przeciągnij**, aby postawić pomiar - **Tab** przełącza między linią dystansu a prostokątem rozmiaru (szer. × wys.), a **Ctrl+C** kopiuje wymiary do schowka (format w Ustawienia › Przechwytywanie). Nakładka zostaje, więc mierzysz dalej; Esc zamyka.
- **Presety stylu**: gdy kolor, grubość i font są już dobrane, **+** na końcu opcji narzędzia zapamiętuje cały ten styl jako kropkę. Kliknięcie kropki przywraca styl - w edytorze i na nakładce przechwytywania (do sześciu; środkowy przycisk usuwa).

**Ulepszone**
- **Wideo zapisuje się dużo szybciej**: nagrania używają teraz domyślnie działającego enkodera sprzętowego (VAAPI/NVENC), gdy maszyna faktycznie go ma, zamiast zawsze kodować na procesorze. Enkoder sprzętowy wymieniony, lecz zepsuty, jest wykrywany i pomijany. WebM zostaje wyłącznie programowy i jest teraz nastrojony na kilka razy szybszy zapis.
- **Nagrany kursor znów jest ostry**: systemowy wskaźnik był mieszany dwukrotnie i wychodził rozmyty; teraz jest rysowany 1:1.
- **Pasek zaznaczenia w Historii nie wychodzi już poza okno** w dłuższych językach (polski): guzik Usuń był wypychany za prawą krawędź. Akcje wsadowe to teraz zwarte ikony, więc mieszczą się w każdym języku.
- **Odliczanie pełnoekranowe jest teraz na ekranie**: 3-2-1 przed nagraniem pełnego ekranu lub okna było małym dymkiem, łatwym do przeoczenia - teraz to duży numer wyśrodkowany na ekranie (layer-shell na KDE/wlroots, helper XWayland na GNOME), znikający w chwili rozpoczęcia nagrywania.
- **Menu w zasobniku ma wreszcie wszystkie tryby, każdy z ikoną**: miarka, zaznaczanie tekstu, wideo i GIF pełnoekranowy, instant replay oraz kopiowanie ostatniego zrzutu były dotąd tylko w oknie.
- **OCR sam wykrywa język**: domyślnie włączony, rozpoznaje teraz tekst przy użyciu wszystkich zainstalowanych pakietów językowych Tesseract, więc nie musisz już wpisywać kodów języków. Wyłącz to w Ustawienia › OCR, aby ustawić konkretny, szybszy zestaw.

**Naprawione**
- **Zrzuty ekranu trafiają teraz do historii schowka KDE Plasma (Klipper)**: skopiowany zrzut dało się wkleić, ale nie pojawiał się w aplecie schowka, więc znikał, gdy tylko skopiowałeś cokolwiek innego. Unisic oznacza teraz obraz tak, jak oczekuje tego KDE, dzięki czemu Klipper zachowuje go w historii, tak jak robi to Spectacle. Dziękujemy Augusto-Lescano za zgłoszenie ([#51](https://github.com/unisic/unisic/issues/51)).

## 0.7.1

### English
**New**
- **Pixel loupe while selecting a region**: a magnifier follows the cursor with a zoomed pixel grid, the exact hovered pixel highlighted, and its position and colour - so a selection edge lands on exactly the pixel you mean. Hold **Ctrl** and scroll to change the magnification; toggle it in Settings › Capture overlay.
- **Magnifier tool** (editor and capture overlay, shortcut **Z**): drag over a detail and a loupe appears with a 2× enlarged copy of it. Move the loupe anywhere, resize it to change the magnification, restyle its border - the source area stays anchored, so it keeps showing the pixels you picked.
- **Ctrl + scroll zooms at the cursor** in the editor - the pixel under the pointer stays put, so you zoom into what you are aiming at instead of the window centre.

**Improved**
- The **trim window now shows what you are cutting**: the timeline is a filmstrip of the recording, everything outside your selection is dimmed, and dragging a handle scrubs the preview to that exact frame.
- **Play previews the cut, not the file** - playback stays inside the selection and loops it (**L** toggles the loop).
- **History finds things now**: search by file name or link, and filter by images, GIFs, recordings, **instant-replay clips** (their own category - they are ordinary .mp4 files, so nothing but Unisic can tell them apart), starred or uploaded.
- **Work on many captures at once** - click to select (**Ctrl** adds, **Shift** picks a range, **Ctrl + A** takes all), then star, copy the paths, upload or delete the whole selection in one go. Starred captures are still protected from deletion.
- **The History grid takes the keyboard**: arrows move, **Enter** opens the floating preview, **Ctrl + C** copies, **Delete** deletes. Clicking a tile now opens the preview instead of doing nothing.
- **Redesigned the tile actions**: they sit in a strip along the bottom edge on hover, so the thumbnail you are aiming at stays visible. Tiles fill the window width evenly, a floating date tells you where you are while scrolling, and each tile shows its size and dimensions - the date on a tile now shrinks to just a time for today's captures, so the details fit instead of being cut off.

**Removed**
- **The quick task chooser** (its own hotkey, by default Meta + Shift + Space) is gone: the tray icon's menu already offers every capture mode it did. An upgraded install drops the leftover key grab and its shortcut entry by itself.

**Fixed**
- **The smart eraser behaved like a smudge tool**: it filled the whole stroke with a single averaged colour, sampled from the stroke's own border - so it went grey wherever the border clipped the thing you were erasing, and left a visibly flat patch on any background that was not one solid colour. It now rebuilds the background from the pixels around the stroke - following a gradient through it, and ignoring whatever the stroke's edge happens to cut across, so scrubbing several overlapping strokes over a line of text leaves the background instead of smears and bright bands.
- **The Callout tool wore the wrong icon** - a generic info symbol borrowed from the system icon theme instead of a speech bubble. It has its own icon now.
- **Uploading to Imgur never worked**: the built-in destination shipped a placeholder Client-ID, so Imgur rejected every upload. Unisic now asks for your own Client-ID (Servers › Imgur › Edit - register a free one at api.imgur.com/oauth2/addclient), repairs the broken stored destination, and tells you what is missing instead of failing silently. Uploads stay anonymous - they never appear in the ID owner's gallery.
- **Trimming a GIF produced the whole GIF**, ignoring the range you picked. GIFs are now re-rendered through the same palette pipeline a recording uses, so the cut lands on the frame you chose.
- **A trimmed WebM started up to several seconds early**: cutting by stream copy can only start on a keyframe. Trimming now re-encodes the selection by default, so the saved file starts on the exact frame the window showed. The old instant copy is still there as **Fast lossless cut** - with it on, the start visibly snaps onto a keyframe (marked with ticks) so the preview keeps matching the file.
- **Trimming without a video preview could ignore keyframe snapping**: with qt6-qtmultimedia missing, the fallback Start slider bypassed the snap - with **Fast lossless cut** on, the file silently began at an earlier keyframe than the window showed.
- **A lossless cut landed on the wrong keyframe for recordings that do not start at zero** (some phone/OBS files): keyframe times are now measured from the start of the file, the same way the timeline and ffmpeg measure.
- **The "Last frame" preview showed a frame the saved file did not contain** - the out-point is the first excluded frame. Dragging the end handle now previews the actual last frame of the cut.
- **Trimming an imported video with an odd width or height failed** ("width not divisible by 2"): the re-encode now trims at most one edge pixel, the same rule the recorder applies.
- **Quitting mid-way through a GIF trim left a stray palette file next to the recording** - the palette is scratch now, lives in the cache and cleans itself up.
- **The trim window froze on long clips with very frequent keyframes** (all-intra / short-GOP files): the timeline now draws only as many keyframe ticks as fit its pixels; snapping still uses the full list.
- **At the trim window's minimum width the cut-mode description ran underneath the Cancel/Save buttons**; it wraps now.

### Polski
**Nowości**
- **Lupa pikselowa przy zaznaczaniu regionu**: lupa podąża za kursorem z powiększoną siatką pikseli, podświetlonym pikselem pod kursorem oraz jego pozycją i kolorem - krawędź zaznaczenia trafia dokładnie w ten piksel, o który chodzi. Przytrzymaj **Ctrl** i przewiń, aby zmienić powiększenie; przełącznik w Ustawienia › Nakładka przechwytywania.
- **Narzędzie Lupa** (edytor i nakładka przechwytywania, skrót **Z**): przeciągnij po detalu, a nad nim pojawi się lupa z jego 2× powiększoną kopią. Przesuwaj ją gdziekolwiek, zmieniaj rozmiar (zmienia powiększenie), przestylizuj ramkę - obszar źródłowy zostaje zakotwiczony, więc lupa cały czas pokazuje wybrane piksele.
- **Ctrl + scroll przybliża pod kursorem** w edytorze - piksel pod wskaźnikiem stoi w miejscu, więc przybliżasz to, w co celujesz, a nie środek okna.

**Ulepszone**
- **Okno przycinania pokazuje, co tniesz**: oś czasu to pasek miniatur nagrania, wszystko poza zaznaczeniem jest przygaszone, a przeciąganie uchwytu przewija podgląd dokładnie do tej klatki.
- **Odtwarzanie pokazuje wycinek, nie cały plik** - playback trzyma się zaznaczenia i zapętla je (**L** przełącza pętlę).
- **W Historii da się wreszcie czegoś szukać**: po nazwie pliku albo linku, plus filtry - obrazy, GIF-y, nagrania, **klipy z instant replay** (osobna kategoria - to zwykłe pliki .mp4, więc nic poza Unisic ich nie odróżni), oznaczone, wysłane.
- **Praca na wielu zrzutach naraz** - klikaj, żeby zaznaczać (**Ctrl** dokłada, **Shift** bierze zakres, **Ctrl + A** wszystko), a potem jednym ruchem oznacz, skopiuj ścieżki, wyślij albo usuń całe zaznaczenie. Zrzuty oznaczone nadal są chronione przed usunięciem.
- **Siatka Historii słucha klawiatury**: strzałki przechodzą po kafelkach, **Enter** otwiera pływający podgląd, **Ctrl + C** kopiuje, **Delete** usuwa. Kliknięcie kafelka otwiera podgląd, zamiast nie robić nic.
- **Przeprojektowane akcje na kafelku**: siedzą w pasku przy dolnej krawędzi po najechaniu, więc miniatura, w którą celujesz, zostaje widoczna. Kafelki równo wypełniają szerokość okna, pływająca data mówi, gdzie jesteś przy przewijaniu, a każdy kafelek pokazuje swój rozmiar i wymiary - data na kafelku kurczy się do samej godziny dla dzisiejszych zrzutów, więc szczegóły mieszczą się zamiast być ucinane.

**Usunięte**
- **Wybór szybkiego zadania** (osobny skrót, domyślnie Meta + Shift + Spacja) znika: menu ikony w zasobniku i tak daje wszystkie tryby przechwytywania, które oferował. Zaktualizowana instalacja sama zwalnia zostawiony skrót i usuwa jego wpis.

**Naprawione**
- **Inteligentna gumka działała jak rozmazywanie**: wypełniała całe pociągnięcie jednym uśrednionym kolorem, próbkowanym z własnej krawędzi - więc szarzała wszędzie tam, gdzie krawędź przecinała wymazywany obiekt, i zostawiała płaską łatę na każdym tle, które nie było jednolite. Teraz odbudowuje tło z pikseli wokół pociągnięcia - przeciąga przez nie gradient i ignoruje to, co krawędź pociągnięcia akurat przecina, więc kilka nachodzących pociągnięć po linijce tekstu zostawia tło, a nie smugi i jasne pasma.
- **Narzędzie Dymek miało nie swoją ikonę** - ogólny symbol informacji pożyczony z systemowego motywu ikon zamiast dymka. Ma już własną.
- **Wysyłka na Imgur nigdy nie działała**: wbudowana destynacja miała zaślepkę zamiast Client-ID, więc Imgur odrzucał każdy upload. Unisic prosi teraz o twój własny Client-ID (Serwery › Imgur › Edytuj - darmowy do zarejestrowania na api.imgur.com/oauth2/addclient), naprawia zepsutą zapisaną destynację i mówi, czego brakuje, zamiast po cichu zawodzić. Wysyłki pozostają anonimowe - nie trafiają do galerii właściciela ID.
- **Przycinanie GIF-a zapisywało cały GIF**, ignorując wybrany zakres. GIF-y są teraz renderowane od nowa tym samym torem palety co nagrania, więc cięcie trafia w wybraną klatkę.
- **Przycięty WebM zaczynał się nawet o kilka sekund za wcześnie**: cięcie przez kopiowanie strumienia może zacząć się tylko na klatce kluczowej. Przycinanie domyślnie przekodowuje zaznaczenie, więc zapisany plik zaczyna się na dokładnie tej klatce, którą pokazało okno. Dawne błyskawiczne kopiowanie zostaje jako **Szybkie cięcie bezstratne** - przy nim początek widocznie przeskakuje na klatkę kluczową (oznaczone kreskami), więc podgląd nadal zgadza się z plikiem.
- **Przycinanie bez podglądu wideo mogło pominąć przyciąganie do klatek kluczowych**: bez qt6-qtmultimedia zapasowy suwak Start omijał przyciąganie - przy włączonym **Szybkim cięciu bezstratnym** plik po cichu zaczynał się na wcześniejszej klatce kluczowej, niż pokazywało okno.
- **Cięcie bezstratne trafiało w złą klatkę kluczową dla nagrań, które nie zaczynają się od zera** (niektóre pliki z telefonów/OBS): czasy klatek kluczowych są teraz liczone od początku pliku - tak samo, jak liczy oś czasu i ffmpeg.
- **Podgląd „Ostatnia klatka" pokazywał klatkę, której zapisany plik nie zawierał** - punkt końcowy to pierwsza wykluczona klatka. Przeciąganie uchwytu końca pokazuje teraz faktycznie ostatnią klatkę wycinka.
- **Przycinanie zaimportowanego wideo o nieparzystej szerokości lub wysokości kończyło się błędem** („width not divisible by 2"): przekodowanie przycina teraz najwyżej jeden piksel krawędzi - ta sama zasada, którą stosuje rejestrator.
- **Wyjście z aplikacji w trakcie przycinania GIF-a zostawiało zabłąkany plik palety obok nagrania** - paleta jest teraz plikiem roboczym, mieszka w cache i sama się sprząta.
- **Okno przycinania zamarzało na długich klipach z bardzo częstymi klatkami kluczowymi** (pliki all-intra / z krótkim GOP): oś czasu rysuje teraz tylko tyle kresek klatek kluczowych, ile mieści się w jej pikselach; przyciąganie nadal używa pełnej listy.
- **Przy minimalnej szerokości okna przycinania opis trybu cięcia wchodził pod przyciski Anuluj/Zapisz**; teraz się zawija.

## 0.7

### English
**New**
- **Trim a recording** without leaving Unisic: History › Trim recording opens a preview with draggable in/out handles, and saves a trimmed copy alongside the original.
- **Instant replay** - keep a rolling buffer of the last seconds of your screen and save it after the fact with one hotkey (default **Meta + Shift + I**). Set the length in Settings.
- **Per-application audio**: record the sound of one chosen application, on its own or mixed with your mic.
- **Hardware video encoder** (VAAPI / NVENC) for MP4, with an automatic fall back to software when the card cannot do it.
- **Watermark** every screenshot with your own text or a logo image - six positions, adjustable opacity.
- **Run a program after capture** - pass the capture to any command with `$input` / `$output` tokens (for example `oxipng -o 4 $input --out $output`).
- **Do not disturb while capturing** pauses desktop notifications so they never land in your screenshot (KDE Plasma).
- **Per-hotkey task presets**: each screenshot hotkey can run its own set of after-capture actions and its own upload destination.
- **Quick task** (default **Meta + Shift + Space**) - one hotkey to choose what to capture and what to do with just that one result.
- New annotation tools: **Measure** (a distance/angle ruler kept in the export) and **Callout** (a speech bubble).
- **Arrowheads** can now be filled, open, or double-ended, and the **highlighter mode** (freehand, rectangle, or text pen) is now yours to pick and is remembered.
- **Select text (OCR)** now boxes every recognized line so you can see exactly what is selectable, and the selection can be highlighted or redacted in place.
- **Paste an image** from the clipboard straight onto the editor canvas.
- **Copy as** Markdown, HTML, a data URI, or a file path - plus **Show QR code** for an uploaded link.
- **Drag a capture** out of the history or its notification thumbnail into another app - a file manager, chat, or editor.
- The history gets a split copy button (image on the left, link on the right) and a **More** menu per item.
- On GNOME, the capture notification is now the same styled card as everywhere else, with working action buttons.
- Click the version label to read these release notes.
- **Ctrl + W** closes the editor and the trim window too, not just the main window.
- The **trim window** now wears the same styled title bar as the rest of Unisic (and follows the system-decoration setting in Appearance).
- **Distance from the screen edge** for the capture card (Settings › Notifications) - raise it when a dock or panel sits where the card lands.
- **Live card preview**: open the style, position or distance dropdown for the capture card and the real card appears on screen - walking the list walks the card through the options, so you see each one where your next capture's card will actually sit before you pick it.
- **Pick the capture card's buttons** (Settings › Notifications): switch off the actions you never use and the rest spread out over the freed room.
- **Trim straight from the notification**: a finished recording's card now offers Trim, next to the buttons a screenshot gets - no detour through History.
- **Edit** is a page of its own now: open a picture you already have in the editor, or a video in the trim window. Unisic edits your own files, not just its own captures.
- Command line: `--delay SECONDS`, `--output <path>` (`-` for stdout), `--format png|jpg|webp`, and `--measure`.

**Fixed**
- On GNOME, reordering pinned Ubuntu Dock icons while recording a region now works. The record frame is drawn as four thin edges around the region instead of one full-screen surface, so nothing covers the rest of the desktop (and drags inside the recorded region work too). Thanks to the user report that pinned this down.
- **Date subfolders** now bucket recordings too, not just screenshots.
- On GNOME, the capture card no longer lands under the top bar or a dock: it is now placed inside the desktop's work area instead of the raw screen rectangle. (Panels already pushed the card aside everywhere layer-shell is available - KDE, wlroots, COSMIC.)
- The screenshot sound cue is now a notification event sound, so it no longer disrupts other audio (such as a Discord screen-share capture).
- Settings help **“?”** badges no longer overlap wide fields, and the duplicate question-mark mouse cursor is gone.
- On desktops with no ScreenCast portal backend (Cinnamon, MATE, XFCE), the recording pages claimed Unisic "was built without PipeWire support" - wrong, and misleading when a PipeWire process is plainly running. They now name the actual missing piece: the portal that asks for permission and opens the stream.

**Removed**
- **Smart pick** (the experimental click-to-pick-an-object option in Settings › Capture) is gone. Detection was purely visual and never recognized windows and elements reliably enough to keep. Region selection by dragging is unchanged.
- The editor's **Remove background** action and the U-2-Net model settings are gone, along with the optional onnxruntime dependency. The smart eraser is unaffected.
- The **0x0.st** built-in upload host is gone - it rejected the uploads, so it only ever produced errors. If it was your selected server, Unisic switches you back to catbox.moe; your own destinations are untouched.

### Polski
**Nowości**
- **Przytnij nagranie** bez wychodzenia z Unisic: Historia › Przytnij nagranie otwiera podgląd z przesuwanymi uchwytami początku i końca, a przycięta kopia zapisuje się obok oryginału.
- **Powtórka na żądanie** - Unisic trzyma w pamięci ostatnie sekundy ekranu, a Ty zapisujesz je już po fakcie jednym skrótem (domyślnie **Meta + Shift + I**). Długość ustawisz w Ustawieniach.
- **Dźwięk pojedynczej aplikacji**: nagraj dźwięk jednego wybranego programu, osobno albo w miksie z mikrofonem.
- **Sprzętowy koder wideo** (VAAPI / NVENC) dla MP4, z automatycznym powrotem do programowego, gdy karta sobie nie poradzi.
- **Znak wodny** na każdym zrzucie - własny tekst albo logo, sześć pozycji, regulowana przezroczystość.
- **Uruchom program po przechwyceniu** - przekaż zrzut dowolnemu poleceniu przez `$input` / `$output` (na przykład `oxipng -o 4 $input --out $output`).
- **Nie przeszkadzać podczas przechwytywania** wstrzymuje powiadomienia pulpitu, żeby nie wpadły na zrzut (KDE Plasma).
- **Presety zadań per skrót**: każdy skrót zrzutu może mieć własny zestaw akcji po przechwyceniu i własny serwer docelowy.
- **Szybkie zadanie** (domyślnie **Meta + Shift + Spacja**) - jeden skrót, by wybrać co przechwycić i co zrobić z tym jednym wynikiem.
- Nowe narzędzia adnotacji: **Miarka** (linijka odległości i kąta zachowana w eksporcie) oraz **Dymek**.
- **Groty strzałek** mogą być teraz wypełnione, otwarte lub dwustronne, a **tryb zakreślacza** (odręczny, prostokąt, pisak tekstu) sam wybierasz i jest zapamiętywany.
- **Zaznaczanie tekstu (OCR)** obrysowuje teraz każdą rozpoznaną linię, więc dokładnie widać, co można zaznaczyć, a zaznaczenie można od razu zakreślić lub zamazać.
- **Wklej obraz** ze schowka prosto na płótno edytora.
- **Kopiuj jako** Markdown, HTML, data URI lub ścieżkę pliku - plus **Pokaż kod QR** dla wysłanego linku.
- **Przeciągnij zrzut** z historii lub z miniatury powiadomienia prosto do innej aplikacji - menedżera plików, czatu czy edytora.
- Historia dostaje dzielony przycisk kopiowania (obraz z lewej, link z prawej) i menu **Więcej** przy każdej pozycji.
- Na GNOME powiadomienie o zrzucie to teraz ta sama stylowana karta co wszędzie, z działającymi przyciskami akcji.
- Kliknij etykietę wersji, aby przeczytać te informacje o wydaniu.
- **Ctrl + W** zamyka też okno edytora i okno przycinania, nie tylko okno główne.
- **Okno przycinania** ma teraz ten sam stylowany pasek tytułu co reszta Unisic (i słucha ustawienia dekoracji systemowych w Wyglądzie).
- **Odległość od krawędzi ekranu** dla karty przechwytywania (Ustawienia › Powiadomienia) - zwiększ ją, gdy dok lub panel stoi tam, gdzie ląduje karta.
- **Podgląd karty na żywo**: rozwiń listę stylu, pozycji lub odległości karty przechwytywania, a prawdziwa karta pojawi się na ekranie - przechodzenie po opcjach przeprowadza kartę przez kolejne warianty, więc każdy widzisz dokładnie tam, gdzie stanie karta następnego zrzutu, zanim go wybierzesz.
- **Wybierz przyciski karty przechwytywania** (Ustawienia › Powiadomienia): wyłącz akcje, których nie używasz, a reszta rozłoży się na zwolnionym miejscu.
- **Przytnij wprost z powiadomienia**: karta gotowego nagrania ma teraz przycisk Przytnij, obok tych, które dostaje zrzut - bez wycieczki przez Historię.
- **Edytuj** to teraz osobna zakładka: otwórz zdjęcie, które już masz, w edytorze, albo film w oknie przycinania. Unisic edytuje Twoje własne pliki, nie tylko własne zrzuty.
- Wiersz poleceń: `--delay SEKUNDY`, `--output <ścieżka>` (`-` dla stdout), `--format png|jpg|webp` oraz `--measure`.

**Naprawiono**
- Na GNOME zmiana kolejności przypiętych ikon w Ubuntu Dock podczas nagrywania obszaru już działa. Ramka nagrywania jest rysowana jako cztery cienkie krawędzie wokół obszaru zamiast jednej pełnoekranowej powierzchni, więc nic nie zasłania reszty pulpitu (a przeciąganie wewnątrz nagrywanego obszaru też działa). Dzięki zgłoszeniu użytkownika, które to namierzyło.
- **Podfoldery z datą** grupują teraz także nagrania, nie tylko zrzuty.
- Na GNOME karta przechwytywania nie ląduje już pod górnym paskiem ani pod dokiem: jest teraz umieszczana w obszarze roboczym pulpitu, a nie w surowym prostokącie ekranu. (Tam, gdzie jest layer-shell - KDE, wlroots, COSMIC - panele i tak odsuwały kartę.)
- Dźwięk zrzutu jest teraz dźwiękiem powiadomienia, więc nie zakłóca innego audio (np. przechwytywania udostępniania ekranu na Discordzie).
- Znaczniki pomocy **„?”** w Ustawieniach nie nachodzą już na szerokie pola, a zduplikowany kursor ze znakiem zapytania zniknął.
- Na pulpitach bez backendu portalu ScreenCast (Cinnamon, MATE, XFCE) strony nagrywania twierdziły, że Unisic „zbudowano bez obsługi PipeWire” - nieprawda, i mylące, gdy proces PipeWire jawnie działa. Teraz nazywają to, czego naprawdę brakuje: portalu, który pyta o zgodę i otwiera strumień.

**Usunięto**
- **Inteligentny wybór** (eksperymentalna opcja klikania w obiekt w Ustawieniach › Przechwytywanie) zniknął. Wykrywanie było czysto wizualne i nigdy nie rozpoznawało okien ani elementów na tyle pewnie, by je zostawić. Zaznaczanie obszaru przeciąganiem działa bez zmian.
- Akcja **Usuń tło** w edytorze i ustawienia modelu U-2-Net zniknęły wraz z opcjonalną zależnością onnxruntime. Inteligentna gumka działa bez zmian.
- Wbudowany serwer **0x0.st** zniknął - odrzucał wysyłki, więc dawał wyłącznie błędy. Jeśli był Twoim wybranym serwerem, Unisic przełącza z powrotem na catbox.moe; Twoje własne serwery zostają nietknięte.

## 0.7b

### English
**New**
- Window keyboard shortcuts, with a **Ctrl + /** cheat sheet listing them.
- Configurable capture and recording **sound cues**: new presets, a separate recording-start sound, and a playback-volume control.
- **Update-channel** selection in Settings, with automatic background updates and pre-releases offered on the beta channel.
- Smarter **save routing** for screenshots: a record countdown, ask-where-to-save, date subfolders, strip-metadata, and a save-as dialog.
- **Select text (OCR)** drag-selection is character-precise - like the Windows Snipping Tool.

### Polski
**Nowości**
- Skróty klawiszowe okna, ze ściągawką **Ctrl + /**.
- Konfigurowalne **dźwięki** przechwytywania i nagrywania: nowe warianty, osobny dźwięk startu nagrywania i regulacja głośności.
- Wybór **kanału aktualizacji** w Ustawieniach, z automatycznymi aktualizacjami w tle i wydaniami wstępnymi na kanale beta.
- Inteligentniejsze **kierowanie zapisu** zrzutów: odliczanie przed nagraniem, pytanie gdzie zapisać, podfoldery z datą, usuwanie metadanych i okno zapisu jako.
- **Zaznaczanie tekstu (OCR)** przeciąganiem jest precyzyjne co do znaku - jak w narzędziu Wycinanie w Windows.

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
