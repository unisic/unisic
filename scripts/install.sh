#!/usr/bin/env bash
#
# Unisic universal installer — INTERACTIVE (a full-screen terminal menu).
#
# Run it in a terminal:
#     bash <(curl -fsSL https://raw.githubusercontent.com/unisic/unisic/main/scripts/install.sh)
# or, once downloaded:
#     bash scripts/install.sh
#
# It opens a btop-style menu (arrow keys + Enter) that can install, update,
# uninstall, install an older version, and turn on automatic updates. There is
# no command-line mode — the menu is the only user interface (the private
# "--self-update" argument is used by the auto-update timer and by Unisic's
# in-app "Install now" button, never typed by a user).
#
# What it installs, auto-detected from /etc/os-release:
#   *.deb                     Debian / Ubuntu        apt install
#   *.fedora.x86_64.rpm       Fedora ONLY            dnf install   (the rpm links
#                             Qt PRIVATE symbols, locked to Fedora's exact Qt minor)
#   *.pkg.tar.zst             Arch                   pacman -U
#   OBS zypper repo           openSUSE (no rpm)      zypper install
#   AppImage / portable .tar.gz  atomic desktops (Silverblue/Bazzite/…) and any
#                             distro with no native package — installed in $HOME,
#                             no password needed, self-updating.
# Native packages self-register Unisic's OBS/COPR update repo, so later versions
# arrive through the system's normal updates; portable installs re-run this to
# update (or turn on the daily auto-update timer in "More options").

set -euo pipefail

REPO="unisic/unisic"
API="https://api.github.com/repos/${REPO}"
OBS_BASE="https://download.opensuse.org/repositories/home:/unisic"
RAW_URL="https://raw.githubusercontent.com/${REPO}/main/scripts/install.sh"
DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/unisic"
UNIT_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"

# --- state --------------------------------------------------------------
ACTION="install"     # install | uninstall | autoupdate-on | autoupdate-off
CHANNEL="auto"       # auto | appimage | tarball | native
REQ_VERSION=""       # a tag chosen in the version picker (NOT named VERSION —
                     # sourcing /etc/os-release would clobber a var of that name)
PREFIX="${HOME}/.local"
ASSUME_YES=0
PURGE=0
PRERELEASE=0
RESOLVED_CHANNEL=""
IS_ATOMIC=0
IN_ALT=0             # 1 while the alternate-screen menu owns the terminal
SELF_UPDATE=0        # 1 in the private timer-driven update mode
MENU_CHOICE=""

# The ONLY non-interactive entry:
#   install.sh --self-update <appimage|tarball|native> <prefix> [pre]
# The auto-update systemd timer uses the portable channels (appimage|tarball),
# which need no password. `native` is the in-app "Install now" path: Unisic runs
# it inside a terminal it spawned, so the sudo password prompt has somewhere to
# go — it reinstalls the matching .deb/.rpm/.pkg for the running distro.
# Anything else ignores its arguments and opens the menu.
if [ "${1:-}" = "--self-update" ]; then
    SELF_UPDATE=1
    CHANNEL="${2:-tarball}"
    PREFIX="${3:-$HOME/.local}"
    if [ "${4:-}" = "pre" ]; then PRERELEASE=1; fi
    ASSUME_YES=1
elif [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    printf 'Unisic installer — an interactive menu. Just run it in a terminal:\n\n    bash %s\n\n' "$0"
    exit 0
fi

# --- helpers ------------------------------------------------------------
say()  { printf '\033[1;35m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33mwarning:\033[0m %s\n' "$*" >&2; }

# Enter/leave the terminal's ALTERNATE screen buffer (like top/less). The whole
# interactive run — menu AND install output — happens in there; leaving it
# restores the terminal to the command the user typed, so only the final
# thank-you remains.
enter_alt() {
    if [ "$IN_ALT" -eq 1 ]; then return 0; fi
    printf '\033[?1049h\033[?25h\033[2J\033[H' >/dev/tty 2>/dev/null || true
    IN_ALT=1
}
leave_alt() {
    if [ "$IN_ALT" -eq 0 ]; then return 0; fi
    printf '\033[?25h\033[?1049l' >/dev/tty 2>/dev/null || true
    IN_ALT=0
}

# die must restore the normal screen FIRST, or the error would vanish with the
# alternate buffer.
die() { leave_alt; printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

# Any exit (success, error, or set -e abort) restores the terminal and cleans up.
_cleanup() {
    leave_alt
    if [ -n "${tmpdir:-}" ]; then rm -rf "$tmpdir" 2>/dev/null || true; fi
}
trap _cleanup EXIT

have() { command -v "$1" >/dev/null 2>&1; }

# --- AUR ---------------------------------------------------------------
# pacman itself can NEVER update an AUR package: the AUR hosts build recipes,
# not built packages, so there is no sync database for `pacman -Syu` to read.
# A helper is the only thing that updates one, by diffing foreign packages
# (`pacman -Qm`) against the AUR by name. yay is the best known; it is nowhere
# near the only one, so probe the field in rough order of current popularity.
AUR_HELPERS='paru yay pikaur trizen aura pacaur yaourt'

aur_helper() {
    local h
    # A helper refuses to run as root (it builds packages), so under sudo
    # there is no helper to speak of even when one is installed.
    [ "$(id -u)" -eq 0 ] && return 1
    for h in $AUR_HELPERS; do have "$h" && { printf '%s' "$h"; return 0; }; done
    return 1
}

# Runs "<helper> install <pkg>". aura is the odd one out: it splits AUR
# operations onto -A, while everything else in the list overloads -S.
aur_install() {
    local helper="$1" pkg="$2"
    case "$helper" in
        aura) "$helper" -A --noconfirm "$pkg" ;;
        *)    "$helper" -S --noconfirm "$pkg" ;;
    esac
}

# True when the installed Unisic belongs to the AUR, so this installer must
# keep its hands off it. Deliberately NOT `pacman -Qm unisic`: a directly
# downloaded .pkg.tar.zst whose repo registration failed is foreign too, and
# that one IS ours to replace.
aur_owned() {
    if [ -r /usr/share/unisic/install-channel ] \
       && grep -qx 'aur' /usr/share/unisic/install-channel 2>/dev/null; then
        return 0
    fi
    have pacman && pacman -Qq unisic-bin >/dev/null 2>&1
}

fetch() {   # curl or wget, to stdout
    if have curl; then curl -fsSL "$1"
    elif have wget; then wget -qO- "$1"
    else die "This installer needs the 'curl' or 'wget' download tool, but neither is installed.
    Install one with your software manager (for example: sudo apt install curl) and try again."; fi
}

download() {   # curl or wget, to a file
    say "Downloading $(basename "$2")"
    if have curl; then curl -fL --progress-bar -o "$2" "$1"
    elif have wget; then wget -q --show-progress -O "$2" "$1"
    else die "This installer needs the 'curl' or 'wget' download tool, but neither is installed.
    Install one with your software manager (for example: sudo apt install curl) and try again."; fi
}

# Run a command that changes system software. This needs elevated permission,
# obtained via `sudo`, which asks for the user's login password — explained in
# plain words the first time.
PRIV_NOTE_SHOWN=0
priv() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"                       # already the computer's owner account
    elif have sudo; then
        if [ "$PRIV_NOTE_SHOWN" -eq 0 ]; then
            printf '\n'
            say "To install the software, your computer needs your permission."
            printf '    It will ask for YOUR login password (the one you use to log in).\n'
            printf '    Nothing appears as you type it — that is normal. Press Enter when done.\n\n'
            PRIV_NOTE_SHOWN=1
        fi
        sudo "$@"
    else
        die "Installing this way needs a password, but the 'sudo' tool isn't available on this system.
    Tip: run this installer again and choose \"Install the portable version (no password)\"."
    fi
}

# Current graphical session: x11 | wayland | other. Prefers the login manager's
# own answer (XDG_SESSION_TYPE); falls back to which display socket is set.
session_kind() {
    case "${XDG_SESSION_TYPE:-}" in
        x11)     echo x11; return ;;
        wayland) echo wayland; return ;;
    esac
    if [ -n "${WAYLAND_DISPLAY:-}" ]; then echo wayland
    elif [ -n "${DISPLAY:-}" ]; then echo x11
    else echo other; fi
}

# On X11 everything works: screenshots via the portal/KWin, screen recording via
# X11's own XShm grab, and global hotkeys (KGlobalAccel on KDE, XGrabKey
# elsewhere). Confirm it so an X11 user is not left guessing.
x11_notice() {
    printf '\n'
    printf '    \033[32m+\033[0m X11 session detected - screenshots, annotation, uploading,\n'
    printf '      screen recording (video and GIF) and global hotkeys all work here.\n'
}

# Extract every browser_download_url from a release JSON blob on stdin, then
# print the first whose filename matches the ERE in $1. Avoids a jq dependency.
asset_url() {
    # Trailing `|| true`: no match must yield empty output + exit 0, so `set -e`
    # doesn't abort before the caller's friendly "no asset" die.
    grep -oE '"browser_download_url": *"[^"]+"' \
        | sed -E 's/.*"(https[^"]+)"/\1/' \
        | grep -E "$1" | head -n1 || true
}

prestate() { if [ "$PRERELEASE" -eq 1 ]; then printf 'ON'; else printf 'OFF'; fi; }

# Best-effort snapshot of what's installed now, for the menu's status line.
# Prints "not-installed" or "Unisic <ver> installed (<kind>)". Local queries
# only (no network) so opening the menu stays instant.
installed_status() {
    local v="" kind="" link tgt p
    if have pacman && { pacman -Qq unisic >/dev/null 2>&1 || pacman -Qq unisic-bin >/dev/null 2>&1; }; then
        p=unisic; pacman -Qq unisic >/dev/null 2>&1 || p=unisic-bin
        v="$(pacman -Q "$p" 2>/dev/null | awk '{print $2}')"
        # Which channel owns it decides what the menu may offer, so name it.
        if aur_owned; then kind="AUR package"; else kind="system package"; fi
    elif have dpkg && dpkg -s unisic 2>/dev/null | grep -q '^Status: install ok installed'; then
        v="$(dpkg-query -W -f='${Version}' unisic 2>/dev/null)"; kind="system package"
    elif have rpm && rpm -q unisic >/dev/null 2>&1; then
        v="$(rpm -q --qf '%{VERSION}' unisic 2>/dev/null)"; kind="system package"
    fi
    if [ -z "$kind" ]; then
        link="${PREFIX}/bin/unisic"
        if [ -L "$link" ] || [ -e "$link" ]; then
            kind="portable"
            tgt="$(readlink -f "$link" 2>/dev/null || true)"
            # Path is .../unisic-<ver>-x86_64/... or .../Unisic-<ver>-x86_64.AppImage
            if [[ "${tgt,,}" =~ unisic[/-]([0-9][a-z0-9._]*)-x86_64 ]]; then v="${BASH_REMATCH[1]}"; fi
        fi
    fi
    if [ -z "$kind" ]; then printf 'not-installed'
    elif [ -n "$v" ]; then printf 'Unisic %s installed (%s)' "$v" "$kind"
    else printf 'Unisic installed (%s)' "$kind"; fi
}

# Map a chosen menu id to the variables the rest of the script reads.
tui_apply() {
    case "$1" in
        native)         ACTION="install";   CHANNEL="auto" ;;
        appimage)       ACTION="install";   CHANNEL="appimage" ;;
        uninstall)      ACTION="uninstall" ;;
        purge)          ACTION="uninstall"; PURGE=1 ;;
        autoupdate-on)  ACTION="autoupdate-on" ;;
        autoupdate-off) ACTION="autoupdate-off" ;;
        quit|"")        MENU_CHOICE=quit ;;
        *)              die "invalid choice" ;;
    esac
}

# ======================================================================
# btop-style bordered menu. ONE window (the alternate screen, entered by the
# caller) that MORPHS between views (main <-> version list). Every action lives
# on the main screen (no "More options" submenu) so nothing is hidden; a status
# line shows what's installed. Full redraw from the top each key — no in-place
# cursor math, robust to changing list length. All output to /dev/tty; content
# is ASCII so the box borders stay aligned. Sets MENU_CHOICE, and for a chosen
# version REQ_VERSION/ACTION/CHANNEL plus MENU_CHOICE=__picked.
# ======================================================================
tui_run() {
    local tty=/dev/tty saved key rest act id n cols BOX_W INSTALLED
    exec 3<"$tty" || { warn "no terminal for the menu"; MENU_CHOICE=quit; return; }
    saved="$(stty -g <&3 2>/dev/null || true)"
    stty -echo -icanon min 1 time 0 <&3 2>/dev/null || true
    printf '\033[?25l' >"$tty"          # hide cursor (alt screen already active)
    trap 'stty "$saved" <&3 2>/dev/null || true; leave_alt; exit 130' INT

    cols="$(stty size <&3 2>/dev/null | awk '{print $2}')"
    if [ -z "${cols:-}" ]; then cols=80; fi
    BOX_W=$(( cols - 4 ))
    if [ "$BOX_W" -gt 74 ]; then BOX_W=74; fi
    if [ "$BOX_W" -lt 48 ]; then BOX_W=48; fi

    local view=main sel=0 status_line status_style=dim qhint=quit au_state=OFF
    local -a ids=() labels=() helps=() hdr=() VER_TAGS=()

    local CB='\033[38;2;99;91;150m'          # border colour
    local CT='\033[1;38;2;200;172;214m'      # title  (lavender)
    local CS='\033[48;2;200;172;214m\033[38;2;23;21;59m'  # selected row (lavender bar)
    local CD='\033[2m'                        # dim
    local CO='\033[38;2;120;200;140m'         # ok / installed (green)
    local R='\033[0m'

    _hr() { local nn="$1" i ss=''; for ((i=0; i<nn; i++)); do ss+='─'; done; printf '%s' "$ss"; }

    # One padded content line inside the box, in the given style.
    _boxline() {
        local txt="$1" style="$2" inner=$(( BOX_W - 2 )) pad c=''
        txt="${txt:0:$inner}"
        printf -v pad '%-*s' "$inner" "$txt"
        case "$style" in
            title) c="$CT" ;;
            sel)   c="$CS" ;;
            dim)   c="$CD" ;;
            ok)    c="$CO" ;;
        esac
        printf "${CB}│${R}%b%s${R}${CB}│${R}\r\n" "$c" "$pad" >"$tty"
    }
    _rule() { printf "${CB}%s%s%s${R}\r\n" "$1" "$(_hr $(( BOX_W - 2 )))" "$2" >"$tty"; }

    _draw() {
        local m=${#ids[@]} i h
        printf '\033[H' >"$tty"                       # home
        _rule '╭' '╮'
        _boxline ' Unisic  installer' title
        _boxline ' a screenshot & screen-recording app for Linux' dim
        _rule '├' '┤'
        _boxline " $status_line" "$status_style"
        _rule '├' '┤'
        _boxline '' plain
        for ((i=0; i<m; i++)); do
            # hdr[i] = "-" inserts a blank line above the item (e.g. before Back).
            if [ "${hdr[$i]:-}" = "-" ]; then _boxline '' plain; fi
            if [ "$i" -eq "$sel" ]; then _boxline "  > ${labels[$i]}" sel
            else _boxline "    ${labels[$i]}" plain; fi
        done
        _boxline '' plain
        _rule '├' '┤'
        _boxline " ${helps[$sel]}" dim
        _boxline " Up/Down  move     Enter  choose     q  ${qhint}" dim
        _rule '╰' '╯'
        printf '\033[J' >"$tty"                        # clear anything below
    }

    # Main menu: four choices, three of them open a submenu. A green line shows
    # what's installed. hdr[i]="-" inserts a blank line above item i.
    _build_main() {
        view=main
        qhint=quit
        au_state=OFF
        if [ -f "${UNIT_DIR}/unisic-update.timer" ]; then au_state=ON; fi
        if [ "$INSTALLED" = not-installed ]; then
            status_line="Not installed yet - choose Install or update to get started."
            status_style=dim
        else
            status_line="${INSTALLED}  -  auto-update ${au_state}"
            status_style=ok
        fi
        ids=(m_install m_settings m_remove quit)
        labels=(
            "Install or update Unisic"
            "Settings"
            "Remove Unisic"
            "Quit"
        )
        helps=(
            "Install Unisic, the portable version, or an older version."
            "Automatic updates and test (pre-release) versions."
            "Uninstall Unisic, optionally with its settings too."
            "Close this installer without changing anything."
        )
        hdr=("" "" "" "-")
    }

    # Submenu: install / update.
    _build_install() {
        view=install
        qhint=back
        status_line="Install or update"
        status_style=dim
        ids=(native appimage pickver __back)
        labels=(
            "Install or update Unisic          (recommended)"
            "Install the portable version      (no password)"
            "Install a specific (older) version"
            "Back"
        )
        helps=(
            "Installs Unisic (or updates it). Asks for your password."
            "Puts Unisic in your home folder. No password needed."
            "Pick an exact, older version from a list."
            "Return to the main menu."
        )
        hdr=("" "" "" "-")
    }

    # Submenu: settings (both toggle in place / on select).
    _build_settings() {
        view=settings
        qhint=back
        au_state=OFF
        if [ -f "${UNIT_DIR}/unisic-update.timer" ]; then au_state=ON; fi
        status_line="Settings"
        status_style=dim
        ids=(autoupdate pre __back)
        labels=(
            "Automatic updates: ${au_state}"
            "Test (pre-release) versions: $(prestate)"
            "Back"
        )
        helps=(
            "Check once a day and update a portable install for you."
            "OFF = stable, tested versions (recommended). ON = also test builds."
            "Return to the main menu."
        )
        hdr=("" "" "-")
    }

    # Submenu: remove.
    _build_remove() {
        view=remove
        qhint=back
        status_line="Remove Unisic"
        status_style=dim
        ids=(uninstall purge __back)
        labels=(
            "Uninstall Unisic"
            "Uninstall and delete my settings too"
            "Back"
        )
        helps=(
            "Removes Unisic. Keeps your settings for next time."
            "Removes Unisic AND its settings and history. Cannot be undone."
            "Return to the main menu."
        )
        hdr=("" "" "-")
    }

    _build_versions() {
        view=versions
        qhint=back
        status_line="Choose a version to install (newest first)"
        status_style=dim
        ids=(); labels=(); helps=(); hdr=()
        local t
        for t in "${VER_TAGS[@]}"; do
            ids+=("$t"); labels+=("Unisic ${t#v}"); helps+=("Install version ${t#v} of Unisic."); hdr+=("")
        done
        ids+=("__back"); labels+=("Back"); helps+=("Go back without installing."); hdr+=("-")
    }

    _go_back() {
        case "$view" in
            versions)                 _build_install; sel=0 ;;
            install|settings|remove)  _build_main;    sel=0 ;;
            *)                        return 1 ;;
        esac
    }

    _load_versions() {   # fills VER_TAGS (max 10); 0 on success
        local json t count=0
        json="$(fetch "${API}/releases?per_page=30" 2>/dev/null)" || return 1
        VER_TAGS=()
        while IFS= read -r t; do
            [ -n "$t" ] || continue
            count=$(( count + 1 )); [ "$count" -le 10 ] || break
            VER_TAGS+=("$t")
        done <<EOF
$(printf '%s' "$json" | grep -oE '"tag_name": *"[^"]+"' | sed -E 's/.*"([^"]+)".*/\1/')
EOF
        [ "${#VER_TAGS[@]}" -gt 0 ]
    }

    _toggle_pre() {
        PRERELEASE=$(( 1 - PRERELEASE ))
        local j
        for ((j=0; j<${#ids[@]}; j++)); do
            if [ "${ids[$j]}" = pre ]; then labels[$j]="Test (pre-release) versions: $(prestate)"; fi
        done
    }

    INSTALLED="$(installed_status)"
    _build_main
    _draw
    while :; do
        IFS= read -rsn1 -u 3 key || key=q
        n=${#ids[@]}
        act=none
        case "$key" in
            $'\033')
                read -rsn2 -t 0.05 -u 3 rest || rest=""
                case "$rest" in '[A') act=up ;; '[B') act=down ;; '') act=cancel ;; esac ;;
            k|K) act=up ;;
            j|J) act=down ;;
            p|P) act=togglepre ;;
            q|Q) act=cancel ;;
            '' | $'\n' | $'\r') act=enter ;;
        esac
        case "$act" in
            up)   sel=$(( (sel - 1 + n) % n )) ;;
            down) sel=$(( (sel + 1) % n )) ;;
            togglepre) _toggle_pre ;;
            cancel)
                if _go_back; then :; else MENU_CHOICE=quit; break; fi ;;
            enter)
                id="${ids[$sel]}"
                case "$id" in
                    m_install)  _build_install;  sel=0 ;;
                    m_settings) _build_settings; sel=0 ;;
                    m_remove)   _build_remove;   sel=0 ;;
                    __back) _go_back || true ;;
                    pre)    _toggle_pre ;;
                    autoupdate)
                        if [ "$au_state" = ON ]; then MENU_CHOICE="autoupdate-off"
                        else MENU_CHOICE="autoupdate-on"; fi
                        break ;;
                    pickver)
                        status_line="Loading versions..."; status_style=dim
                        ids=(loading); labels=("Please wait...") helps=("Fetching the list from the internet."); hdr=(""); sel=0
                        _draw
                        if _load_versions; then _build_versions; sel=0
                        else _build_install; sel=0
                             status_line="Could not load versions - check your internet."; status_style=dim; fi ;;
                    *)
                        if [ "$view" = versions ]; then
                            REQ_VERSION="$id"; ACTION="install"; CHANNEL="auto"; MENU_CHOICE="__picked"
                        else
                            MENU_CHOICE="$id"
                        fi
                        break ;;
                esac ;;
        esac
        _draw
    done

    trap - INT
    stty "$saved" <&3 2>/dev/null || true
    printf '\033[?25h' >"$tty"          # show cursor; STAY in the alt screen for the install
    exec 3<&-
}

# --- run: menu (or the private self-update) -----------------------------
if [ "$SELF_UPDATE" -eq 0 ]; then
    # Group so 2>/dev/null also swallows the redirection error when there is no
    # controlling terminal (otherwise bash prints "/dev/tty: No such device").
    if ! { : >/dev/tty; } 2>/dev/null; then
        die "The Unisic installer is interactive — please run it inside a terminal window."
    fi
    enter_alt
    tui_run
    case "$MENU_CHOICE" in
        quit)     leave_alt; say "No changes made. See you next time!"; exit 0 ;;
        __picked) : ;;                       # ACTION/REQ_VERSION/CHANNEL already set
        *)        tui_apply "$MENU_CHOICE" ;;
    esac
fi

# --- uninstall ----------------------------------------------------------
uninstall_portable() {   # remove a portable/AppImage install; 1 if nothing there
    local removed=0 d link="${PREFIX}/bin/unisic"
    if [ -L "$link" ]; then rm -f "$link"; removed=1; fi
    if [ -d "${PREFIX}/lib/unisic" ]; then rm -rf "${PREFIX}/lib/unisic"; removed=1; fi
    for d in "${PREFIX}"/lib/unisic-*-x86_64; do
        if [ -d "$d" ]; then rm -rf "$d"; removed=1; fi
    done
    return $(( removed == 0 ))
}

do_uninstall() {
    local did=0
    # Native package (each one's own postrm/postun drops its update repo). NB:
    # do_uninstall is invoked via `|| ...`, which disables `set -e` inside it —
    # so every native removal checks its own exit status explicitly.
    if have pacman && pacman -Qq unisic >/dev/null 2>&1; then
        say "Removing Unisic... (this asks for your password)"
        priv pacman -R --noconfirm unisic || die "Couldn't remove Unisic."
        did=1
    elif have dpkg && dpkg -s unisic 2>/dev/null | grep -q '^Status: install ok installed'; then
        say "Removing Unisic... (this asks for your password)"
        priv apt-get purge -y unisic || die "Couldn't remove Unisic."
        did=1
    elif have rpm && rpm -q unisic >/dev/null 2>&1; then
        if have zypper; then
            say "Removing Unisic... (this asks for your password)"
            priv zypper --non-interactive remove unisic || die "Couldn't remove Unisic."
            local a
            for a in $(zypper --non-interactive lr 2>/dev/null \
                       | awk -F'|' 'NR>2 && tolower($0) ~ /unisic/ {gsub(/^[ \t]+|[ \t]+$/,"",$2); print $2}'); do
                priv zypper --non-interactive removerepo "$a" || true
            done
            did=1
        else
            say "Removing Unisic... (this asks for your password)"
            priv dnf remove -y unisic || die "Couldn't remove Unisic."
            did=1
        fi
    fi
    if uninstall_portable; then
        say "Removed the portable copy of Unisic from your home folder."
        did=1
    fi
    if [ -f "${UNIT_DIR}/unisic-update.timer" ]; then remove_autoupdate; did=1; fi
    if [ "$PURGE" -eq 1 ]; then
        local cfg="${XDG_CONFIG_HOME:-$HOME/.config}/unisic"
        if [ -d "$cfg" ]; then rm -rf "$cfg"; say "Also deleted your Unisic settings."; fi
    fi
    # Restore the terminal before reporting, so the result is readable.
    leave_alt
    if [ "$did" -eq 0 ]; then
        warn "Unisic doesn't seem to be installed — nothing to remove."
        return 1
    fi
    say "✓ Unisic has been removed."
}

# --- auto-update (portable / AppImage) ---------------------------------
detect_portable_channel() {
    if ls "${PREFIX}"/lib/unisic/*.AppImage >/dev/null 2>&1; then echo appimage
    elif ls -d "${PREFIX}"/lib/unisic-*-x86_64 >/dev/null 2>&1; then echo tarball
    else echo appimage; fi
}

remove_autoupdate() {
    if have systemctl; then
        systemctl --user disable --now unisic-update.timer >/dev/null 2>&1 || true
    fi
    rm -f "${UNIT_DIR}/unisic-update.service" "${UNIT_DIR}/unisic-update.timer"
    if have systemctl; then systemctl --user daemon-reload >/dev/null 2>&1 || true; fi
    say "Automatic updates are now OFF."
}

# systemd --user timer that re-runs this installer daily via the private
# --self-update entry. The script is copied into DATA_DIR so the timer survives
# the shell that ran it (a piped curl|bash has no on-disk $0).
setup_autoupdate() {
    local ch="$1"
    case "$ch" in
        auto|native|apt|dnf|pacman|zypper)
            say "This install already updates automatically with your system — nothing to set up."
            return 0 ;;
    esac
    if ! have systemctl || [ -z "${XDG_RUNTIME_DIR:-}" ]; then
        warn "Automatic updates need a background helper that isn't available here, so I'll skip it.
    You can update anytime by running this installer again."
        return 0
    fi
    mkdir -p "$DATA_DIR" "$UNIT_DIR"
    if [ -f "$0" ] && grep -q 'Unisic universal installer' "$0" 2>/dev/null; then
        cp "$0" "${DATA_DIR}/install.sh"
    else
        download "$RAW_URL" "${DATA_DIR}/install.sh"
    fi
    chmod +x "${DATA_DIR}/install.sh"
    local pre=""; if [ "$PRERELEASE" -eq 1 ]; then pre=" pre"; fi
    cat > "${UNIT_DIR}/unisic-update.service" <<EOF
[Unit]
Description=Update Unisic (${ch})
After=network-online.target
Wants=network-online.target

[Service]
Type=oneshot
ExecStart=/bin/bash ${DATA_DIR}/install.sh --self-update ${ch} ${PREFIX}${pre}
EOF
    cat > "${UNIT_DIR}/unisic-update.timer" <<EOF
[Unit]
Description=Daily Unisic update check

[Timer]
OnCalendar=daily
Persistent=true
RandomizedDelaySec=1h

[Install]
WantedBy=timers.target
EOF
    systemctl --user daemon-reload
    systemctl --user enable --now unisic-update.timer
    say "Automatic updates are now ON — Unisic will check for a newer version once a day."
    say "  To turn this off later, run this installer and choose \"Automatic updates\" again."
}

case "$ACTION" in
    uninstall)      do_uninstall || exit 1; exit 0 ;;
    autoupdate-off) leave_alt; remove_autoupdate; exit 0 ;;
    autoupdate-on)
        leave_alt
        ch="$CHANNEL"
        if [ "$ch" = auto ]; then ch="$(detect_portable_channel)"; fi
        setup_autoupdate "$ch"; exit 0 ;;
esac

# From here on it is an install/update. Clear the menu box; the install now runs
# full-screen in the same window, and leaving it later reveals the thank-you.
if [ "$IN_ALT" -eq 1 ]; then printf '\033[2J\033[H' >/dev/tty 2>/dev/null || true; fi

# --- resolve release ----------------------------------------------------
arch="$(uname -m)"
case "$arch" in
    x86_64|amd64) : ;;
    *) die "Unisic only has ready-made downloads for regular 64-bit PCs (x86_64); your computer is '$arch'.
    You can still build it yourself — see https://github.com/${REPO}" ;;
esac

if [ -n "$REQ_VERSION" ]; then
    tag="v${REQ_VERSION#v}"
    say "Looking up release $tag"
    RELEASE_JSON="$(fetch "${API}/releases/tags/${tag}")" \
        || die "There is no Unisic release tagged $tag."
elif [ "$PRERELEASE" -eq 1 ]; then
    say "Looking up the newest release (test versions included)"
    RELEASE_JSON="$(fetch "${API}/releases?per_page=1")" \
        || die "Couldn't reach GitHub to find the newest version."
else
    say "Looking up the newest version of Unisic"
    RELEASE_JSON="$(fetch "${API}/releases/latest")" \
        || die "Couldn't find a stable release. Turn on \"Test versions\" in More options to try a test build."
fi
[ -n "$RELEASE_JSON" ] || die "GitHub returned an empty response — please try again."

# Newest tag ("v0.7.5"); used to skip a portable re-install that is up to date.
latest_tag="$(printf '%s' "$RELEASE_JSON" \
    | grep -oE '"tag_name": *"[^"]+"' | head -n1 \
    | sed -E 's/.*"([^"]+)"$/\1/' || true)"
latest_ver="${latest_tag#v}"

# --- detect distro ------------------------------------------------------
# Sourcing os-release also sets NAME/VERSION/PRETTY_NAME/… — harmless as long as
# no state var shares those names (the release tag lives in REQ_VERSION for
# exactly this reason).
ID=""; ID_LIKE=""; VERSION_ID=""; VARIANT_ID=""
if [ -r /etc/os-release ]; then
    # shellcheck disable=SC1091
    . /etc/os-release
fi

# Atomic/immutable desktops (Silverblue/Kinoite, uBlue Bazzite/Bluefin/Aurora,
# openSUSE MicroOS/Aeon): read-only base image; /run/ostree-booted marks them.
if [ -f /run/ostree-booted ] || have rpm-ostree || have transactional-update; then
    IS_ATOMIC=1
fi
case "${VARIANT_ID:-}" in
    silverblue|kinoite|sericea|onyx|cosmic-atomic|*-atomic|base-atomic|microos|aeon) IS_ATOMIC=1 ;;
esac

native_pm=""
if   have apt-get && { [ "$ID" = debian ] || [ "$ID" = ubuntu ] || case " $ID_LIKE " in *" debian "*) true;; *) false;; esac; }; then
    native_pm="apt"
elif have zypper && case "$ID" in opensuse*|sles|sled) true;; *) case " $ID_LIKE " in *"suse"*) true;; *) false;; esac;; esac; then
    native_pm="zypper"
elif have dnf && [ "$ID" = fedora ]; then
    native_pm="dnf"     # Fedora ONLY — the rpm is locked to Fedora's Qt minor.
elif have pacman && { [ "$ID" = arch ] || case " $ID_LIKE " in *" arch "*) true;; *) false;; esac; }; then
    native_pm="pacman"
fi
# Atomic base: no layer to install a native package into → portable AppImage.
if [ "$IS_ATOMIC" -eq 1 ]; then native_pm=""; fi

# --- installers ---------------------------------------------------------
tmpdir="$(mktemp -d)"   # removed by _cleanup on EXIT

# When a native package install fails (a missing dependency, a repo problem),
# point the user at the always-works portable option instead of a dead end.
native_fail() {
    die "Installing the package didn't finish (your system's package manager reported a problem).
    You can install the no-password portable version instead: run this installer again,
    open \"More options\", and choose \"Install the portable version\"."
}

install_deb() {
    local url file
    url="$(printf '%s' "$RELEASE_JSON" | asset_url '\.deb$')"
    [ -n "$url" ] || die "This release has no Debian package."
    file="${tmpdir}/$(basename "$url")"
    download "$url" "$file"
    say "Installing Unisic... (from now on it updates with your system's normal updates)"
    priv apt-get install -y "$file" || native_fail
}

install_rpm() {
    local url file
    url="$(printf '%s' "$RELEASE_JSON" | asset_url '\.rpm$')"
    [ -n "$url" ] || die "This release has no Fedora package."
    file="${tmpdir}/$(basename "$url")"
    download "$url" "$file"
    say "Installing Unisic... (from now on it updates with your system's normal updates)"
    priv dnf install -y "$file" || native_fail
}

install_arch() {
    local url file helper
    helper="$(aur_helper || true)"

    # Already an AUR install: hand it back to the helper that owns it. Running
    # `pacman -U` over it would swap the channel behind the user's back AND
    # let the downloaded package's scriptlet add the OBS repo, leaving two
    # things convinced they manage the same install.
    if aur_owned; then
        if [ -n "$helper" ]; then
            say "Unisic came from the AUR here, so I'll update it with ${helper}."
            aur_install "$helper" unisic-bin || native_fail
            return
        fi
        die "This Unisic was installed from the AUR, and only an AUR helper can update it.
    Run one of these instead (whichever you use):
      paru -S unisic-bin
      yay -S unisic-bin
    Or remove it first (sudo pacman -R unisic-bin) and run this installer again."
    fi

    # Fresh install with a helper present: go through the AUR, because that is
    # the channel Arch users expect to update from afterwards. unisic-bin is
    # the same binary this script would have downloaded - it repacks this very
    # release asset - so nothing is compiled and nothing is slower.
    if [ -n "$helper" ]; then
        say "Installing Unisic from the AUR with ${helper}... (from now on ${helper} updates it)"
        if aur_install "$helper" unisic-bin; then
            return
        fi
        warn "${helper} couldn't install it, so I'll fall back to the direct download."
    fi

    # Anchored on "unisic-<digit>" so the release's unisic-debug-*.pkg.tar.zst
    # can never be the match head -n1 happens to pick.
    url="$(printf '%s' "$RELEASE_JSON" | asset_url 'unisic-[0-9][^/]*\.pkg\.tar\.zst$')"
    [ -n "$url" ] || die "This release has no Arch package."
    file="${tmpdir}/$(basename "$url")"
    download "$url" "$file"
    say "Installing Unisic... (from now on it updates with your system's normal updates)"
    priv pacman -U --noconfirm "$file" || native_fail
}

# openSUSE ships from the OBS zypper repo (no rpm asset).
install_zypper_repo() {
    local target=""
    case "$ID" in
        opensuse-tumbleweed) target="openSUSE_Tumbleweed" ;;
        opensuse-leap) case "${VERSION_ID:-}" in 16.0|16.*) target="16.0" ;; esac ;;
    esac
    if [ -z "$target" ]; then
        say "No ready-made package for ${ID} ${VERSION_ID:-}, so I'll install the portable version (no password)."
        install_tarball; return
    fi
    local repo="${OBS_BASE}/${target}/home:unisic.repo"
    say "Setting up Unisic... (from now on it updates with your system's normal updates)"
    priv zypper --non-interactive addrepo --refresh "$repo" || true
    priv zypper --non-interactive --gpg-auto-import-keys refresh
    priv zypper --non-interactive install unisic || native_fail
}

install_appimage() {
    local url dest bindir
    url="$(printf '%s' "$RELEASE_JSON" | asset_url 'x86_64\.AppImage$')"
    [ -n "$url" ] || die "This release has no portable download."
    dest="${PREFIX}/lib/unisic/$(basename "$url")"
    if [ -z "$REQ_VERSION" ] && [ -e "$dest" ] && [ "$(readlink -f "${PREFIX}/bin/unisic" 2>/dev/null)" = "$(readlink -f "$dest")" ]; then
        say "You already have the newest Unisic (${latest_ver:-current}) — nothing to do."
        return
    fi
    bindir="${PREFIX}/bin"
    mkdir -p "$(dirname "$dest")" "$bindir"
    download "$url" "$dest"
    chmod +x "$dest"
    ln -sf "$dest" "${bindir}/unisic"
}

install_tarball() {
    local url tgz
    url="$(printf '%s' "$RELEASE_JSON" | asset_url 'x86_64\.tar\.gz$')"
    [ -n "$url" ] || die "This release has no portable download."
    local want="unisic-${latest_ver}-x86_64"
    if [ -z "$REQ_VERSION" ] && [ -n "$latest_ver" ] \
       && [ -d "${PREFIX}/lib/${want}" ] \
       && [ "$(readlink -f "${PREFIX}/bin/unisic" 2>/dev/null)" = "$(readlink -f "${PREFIX}/lib/${want}/AppRun")" ]; then
        say "You already have the newest Unisic (${latest_ver}) — nothing to do."
        return
    fi
    tgz="${tmpdir}/$(basename "$url")"
    download "$url" "$tgz"
    mkdir -p "${PREFIX}/lib" "${PREFIX}/bin"
    say "Setting up the files..."
    # Top dir is unisic-<ver>-x86_64 by construction; using the resolved version
    # avoids `tar tzf | head` (which SIGPIPEs under `set -o pipefail`).
    local top="unisic-${latest_ver}-x86_64"
    if [ -z "$latest_ver" ]; then
        top="$(tar tzf "$tgz" 2>/dev/null | head -n1 | cut -d/ -f1 || true)"
    fi
    [ -n "$top" ] || die "The downloaded file looks damaged. Please try again."
    rm -rf "${PREFIX}/lib/${top}"
    tar xzf "$tgz" -C "${PREFIX}/lib"
    ln -sf "${PREFIX}/lib/${top}/AppRun" "${PREFIX}/bin/unisic"
}

# How to start Unisic, in plain terms (portable installs).
start_hint() {
    case ":$PATH:" in
        *":${PREFIX}/bin:"*) say "  Start it by typing:  unisic" ;;
        *) say "  Start it by typing:  ${PREFIX}/bin/unisic"
           printf '     (After you log out and back in once, just  unisic  will work too.)\n' ;;
    esac
}

# --- dispatch -----------------------------------------------------------
eff="$CHANNEL"
if [ "$eff" = auto ]; then
    if   [ -n "$native_pm" ];    then eff="native"
    elif [ "$IS_ATOMIC" -eq 1 ]; then eff="appimage"
    else                              eff="tarball"; fi
fi
RESOLVED_CHANNEL="$eff"

case "$eff" in
    native)
        case "$native_pm" in
            apt)    install_deb ;;
            dnf)    install_rpm ;;
            pacman) install_arch ;;
            zypper) install_zypper_repo ;;
            # `native` was forced (e.g. by the app's "Install now") but this
            # distro has no native package manager we ship for — fall back to the
            # always-works portable tarball rather than silently doing nothing.
            *)      say "No native package for ${ID:-this system}, installing the portable version instead."
                    install_tarball; RESOLVED_CHANNEL="tarball" ;;
        esac ;;
    appimage)
        if [ "$CHANNEL" = auto ] && [ "$IS_ATOMIC" -eq 1 ]; then
            say "Your Linux keeps its system files locked, so I'll install the portable version (no password)."
        fi
        install_appimage ;;
    tarball)
        if [ "$CHANNEL" = auto ]; then
            say "Your Linux (${ID:-unknown}) has no ready-made package, so I'll install the portable version (no password)."
        fi
        install_tarball ;;
esac

# Restore the normal terminal, THEN print the summary so it stays on screen.
leave_alt
printf '\n'
say "✓ Thank you for installing Unisic!"
if [ "$RESOLVED_CHANNEL" = native ]; then
    say "  Open it from your applications menu (search \"Unisic\"), or type:  unisic"
    say "  It updates automatically with the rest of your system."
else
    say "  Open it from your applications menu (search \"Unisic\")."
    start_hint
    if [ -f "${UNIT_DIR}/unisic-update.timer" ]; then
        say "  Automatic updates are on — Unisic checks for a newer version once a day."
    else
        say "  To update later, just run this installer again."
    fi
fi

# X11 users: recording won't work here — say so plainly.
if [ "$(session_kind)" = x11 ]; then x11_notice; fi
