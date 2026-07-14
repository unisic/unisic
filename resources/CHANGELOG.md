# Unisic — Release notes

Per-version notes shown in-app when the version label is clicked. The section
whose `##` heading matches the running version (see `UNISIC_VERSION`) is shown;
within it, the `### English` / `### Polski` block for the toggled language is
displayed. Keep the newest version at the top; each version is translated as a
whole per release (not per individual change).

## 0.7.1b

### English
**Fixed**
- On GNOME, reordering pinned Ubuntu Dock icons while recording a region now works. The record frame is drawn as four thin edges around the region instead of one full-screen surface, so nothing covers the rest of the desktop (and drags inside the recorded region work too). Thanks to the user report that pinned this down.

### Polski
**Naprawiono**
- Na GNOME zmiana kolejności przypiętych ikon w Ubuntu Dock podczas nagrywania obszaru już działa. Ramka nagrywania jest rysowana jako cztery cienkie krawędzie wokół obszaru zamiast jednej pełnoekranowej powierzchni, więc nic nie zasłania reszty pulpitu (a przeciąganie wewnątrz nagrywanego obszaru też działa). Dzięki zgłoszeniu użytkownika, które to namierzyło.

## 0.7b

### English
**New**
- Window keyboard shortcuts, with a **Ctrl + /** cheat sheet listing them.
- Configurable capture and recording **sound cues**.
- **Update-channel** selection in Settings.
- Smarter **save routing** for screenshots and recordings.
- The highlighter now has three modes: freehand marker, rectangle, and a **text pen** that snaps to detected text.
- **Select text (OCR)** now boxes every recognized line so you can see exactly what is selectable, and drag-selection is character-precise — like the Windows Snipping Tool.
- **Drag a capture** straight out of its notification thumbnail into another app — a file manager, chat, or editor.
- The **per-application audio** picker is now on the Record page too, not just in Settings.
- Click the version label to read these release notes.

**Fixed**
- Background removal no longer keeps its large model loaded after use, so memory returns to normal when idle.
- The screenshot sound cue is now a notification event sound, so it no longer disrupts other audio (such as a Discord screen-share capture).
- Settings help **“?”** badges no longer overlap wide fields, and the duplicate question-mark mouse cursor is gone.

### Polski
**Nowości**
- Skróty klawiszowe okna, ze ściągawką **Ctrl + /**.
- Konfigurowalne **dźwięki** przechwytywania i nagrywania.
- Wybór **kanału aktualizacji** w Ustawieniach.
- Inteligentniejsze **kierowanie zapisu** zrzutów i nagrań.
- Zakreślacz ma teraz trzy tryby: pisak odręczny, prostokąt oraz **pisak tekstu**, który przyczepia się do wykrytego tekstu.
- **Zaznaczanie tekstu (OCR)** obrysowuje teraz każdą rozpoznaną linię, więc dokładnie widać, co można zaznaczyć, a zaznaczanie przeciąganiem jest precyzyjne co do znaku — jak w narzędziu Wycinanie w Windows.
- **Przeciągnij zrzut** z miniatury powiadomienia prosto do innej aplikacji — menedżera plików, czatu czy edytora.
- Wybór **dźwięku aplikacji** jest teraz także na stronie Nagrywania, nie tylko w Ustawieniach.
- Kliknij etykietę wersji, aby przeczytać te informacje o wydaniu.

**Poprawki**
- Usuwanie tła nie trzyma już dużego modelu po użyciu — pamięć wraca do normy w bezczynności.
- Dźwięk zrzutu jest teraz dźwiękiem powiadomienia, więc nie zakłóca innego audio (np. przechwytywania udostępniania ekranu na Discordzie).
- Znaczniki pomocy **„?”** w Ustawieniach nie nachodzą już na szerokie pola, a zduplikowany kursor ze znakiem zapytania zniknął.

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
