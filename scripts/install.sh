#!/usr/bin/env bash
#
# Unisic universal installer - INTERACTIVE (a full-screen terminal menu).
#
# Run it in a terminal:
#     bash <(curl -fsSL https://github.com/unisic/unisic/releases/latest/download/install.sh)
# or, once downloaded:
#     bash scripts/install.sh
#
# That URL is the copy attached to the newest release - frozen at what CI built
# for that tag, and with a sha256 published for it in the API. The branch head
# (RAW_URL below) is the fallback only: it moves under the release it is meant
# to belong to, so nothing can be pinned to it.
#
# It opens a btop-style menu (arrow keys + Enter) that can install, update,
# uninstall, install an older version, and turn on automatic updates. There is
# no command-line mode - the menu is the only user interface (the private
# "--self-update" argument is used by the auto-update timer and by Unisic's
# in-app "Install now" button, never typed by a user).
#
# What it installs, auto-detected from /etc/os-release. Every native route goes
# through Unisic's OWN repository first, because a native package is welded to
# the exact Qt it was built against and only the repo has a build per distro
# release; the GitHub asset is the fallback, and the only way to install an
# older version on purpose:
#   OBS apt repo              Debian 13, Ubuntu 25.10/26.04   apt install unisic
#     *.deb                   other apt distros, or a picked version
#   COPR dnf repo             Fedora ONLY                     dnf install unisic
#     *.fedora.x86_64.rpm     a picked version (the rpm links Qt PRIVATE
#                             symbols, locked to Fedora's exact Qt minor)
#   *.pkg.tar.zst             Arch                   pacman -U (its own scriptlet
#                             adds the OBS pacman repo; an AUR helper wins first
#                             and deliberately does not, see packaging/aur)
#   OBS zypper repo           openSUSE (no rpm)      zypper install
#   AppImage / portable .tar.gz  atomic desktops (Silverblue/Bazzite/...) and any
#                             distro with no native package, installed in $HOME,
#                             no password needed, self-updating.
#   *.flatpak / Flathub       chosen from the menu, or an install that already
#                             is a Flatpak (the sandbox can never update itself,
#                             so this script does it from the host side).
# Either way the update repo ends up registered, so later versions arrive
# through the system's normal updates; portable installs re-run this to update
# (or turn on the daily auto-update timer in "More options").

set -euo pipefail

REPO="unisic/unisic"
API="https://api.github.com/repos/${REPO}"
OBS_BASE="https://download.opensuse.org/repositories/home:/unisic"
RAW_URL="https://raw.githubusercontent.com/${REPO}/main/scripts/install.sh"
SHARE_DIR="${XDG_DATA_HOME:-$HOME/.local/share}"
DATA_DIR="${SHARE_DIR}/unisic"
UNIT_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"

# --- state --------------------------------------------------------------
ACTION="install"     # install | uninstall | autoupdate-on | autoupdate-off
CHANNEL="auto"       # auto | appimage | tarball | native | flatpak
REQ_VERSION=""       # a tag chosen in the version picker (NOT named VERSION -
                     # sourcing /etc/os-release would clobber a var of that name)
PREFIX="${HOME}/.local"
ASSUME_YES=0
PURGE=0
PRERELEASE=0
RESOLVED_CHANNEL=""
INSTALLED_VER=""     # filled by installed_status, read by update_note
INSTALLED_KIND=""
IS_ATOMIC=0
IN_ALT=0             # 1 while the alternate-screen menu owns the terminal
SELF_UPDATE=0        # 1 in the private timer-driven update mode
MENU_CHOICE=""

# The ONLY non-interactive entry:
#   install.sh --self-update <appimage|tarball|native|flatpak> <prefix> [pre]
# The auto-update systemd timer uses the channels that need no password
# (appimage|tarball, and flatpak when it is a --user install). `native` is the
# in-app "Install now" path: Unisic runs
# it inside a terminal it spawned, so the sudo password prompt has somewhere to
# go - it reinstalls the matching .deb/.rpm/.pkg for the running distro.
# Anything else ignores its arguments and opens the menu.
if [ "${1:-}" = "--self-update" ]; then
    SELF_UPDATE=1
    CHANNEL="${2:-tarball}"
    PREFIX="${3:-$HOME/.local}"
    if [ "${4:-}" = "pre" ]; then PRERELEASE=1; fi
    ASSUME_YES=1
elif [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    printf 'Unisic installer - an interactive menu. Just run it in a terminal:\n\n    bash %s\n\n' "$0"
    exit 0
fi

# --- helpers ------------------------------------------------------------
say()  { printf '\033[1;35m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33mwarning:\033[0m %s\n' "$*" >&2; }

# Enter/leave the terminal's ALTERNATE screen buffer (like top/less). The whole
# interactive run - menu AND install output - happens in there; leaving it
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

# --- Flatpak -----------------------------------------------------------
# The sandboxed app can never update itself: doing so needs
# --talk-name=org.freedesktop.Flatpak, which is a sandbox escape and rejected
# by Flathub. This script runs on the host, so it can - and Unisic's Updates
# pane says so instead of offering a button that could not work.
FLATPAK_ID="app.unisic.Unisic"
FLATHUB_REPO="https://dl.flathub.org/repo/flathub.flatpakrepo"
# Overridable so the "Flathub is live" branch can be exercised (with a file://
# URL) while the listing is still a 404.
FLATHUB_API="${UNISIC_FLATHUB_API:-https://flathub.org/api/v2/appstream/${FLATPAK_ID}}"

# Which flatpak installation owns Unisic: "user" or "system". Fails when it is
# not installed as a Flatpak at all.
flatpak_scope() {
    have flatpak || return 1
    if flatpak info --user "$FLATPAK_ID" >/dev/null 2>&1; then printf 'user'; return 0; fi
    if flatpak info --system "$FLATPAK_ID" >/dev/null 2>&1; then printf 'system'; return 0; fi
    return 1
}

# One field of `flatpak info` ("Version", "Origin"), whose output is indented
# "  Field: value" lines.
flatpak_field() {   # <user|system> <field>
    flatpak info "--$1" "$FLATPAK_ID" 2>/dev/null \
        | awk -F': +' -v f="$2" '$1 ~ ("^ *" f "$") { print $2; exit }'
}

# A system-wide flatpak install is root's, so every change to it needs the
# password; a --user one never does.
fp_run() {   # <user|system> <flatpak args...>
    local scope="$1"; shift
    if [ "$scope" = system ]; then priv flatpak "$@"; else flatpak "$@"; fi
}

# Is Unisic published on Flathub yet? Until it is, the release bundle is the
# only source there is; once it is, this is what moves a bundle install over to
# the remote, after which plain `flatpak update` keeps it current.
flathub_has_app() {
    local body
    body="$(fetch "$FLATHUB_API" 2>/dev/null)" || return 1
    printf '%s' "$body" | grep -q "$FLATPAK_ID"
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
    verify_digest "$1" "$2"
}

# Every file this script hands to a package manager, unpacks, or installs as the
# app itself is checked against the sha256 GitHub published for that asset when
# it was uploaded - the digest travels in the same release JSON the download URL
# came from, so no second request and no jq. Both ends are GitHub, so this is no
# defence against GitHub itself; it does catch a truncated or resumed-wrong
# transfer, a caching proxy serving something else, and a file swapped at one
# endpoint but not the other. A URL with no digest in the feed (an old release,
# a raw.githubusercontent copy) has nothing to check against and stands on TLS
# alone, which is what it did before this existed.
verify_digest() {   # <url> <file>
    local want got
    [ -n "${RELEASE_JSON:-}" ] || return 0
    have sha256sum || { warn "The 'sha256sum' tool isn't installed here, so I can't check the
    download against its published checksum. Carrying on."; return 0; }
    # "digest" and "browser_download_url" sit in the same asset object with no
    # nested object between them, so [^{}]* cannot cross into the next asset.
    want="$(printf '%s' "$RELEASE_JSON" | tr -d '\n' \
        | grep -oE '"digest": *"sha256:[0-9a-f]{64}"[^{}]*"browser_download_url": *"[^"]+"' \
        | sed -E 's/.*"sha256:([0-9a-f]{64})".*"(https[^"]+)"/\1 \2/' \
        | awk -v u="$1" '$2 == u { print $1; exit }' || true)"
    [ -n "$want" ] || return 0
    got="$(sha256sum "$2" | cut -d' ' -f1)"
    [ "$got" = "$want" ] && return 0
    rm -f "$2"
    die "The file that arrived isn't the one this release published (its checksum doesn't match),
    so I stopped and deleted it - nothing was installed. This is usually a download that broke
    halfway or a proxy on your network serving an old copy. Please try again."
}

# Run a command that changes system software. This needs elevated permission,
# obtained via `sudo`, which asks for the user's login password - explained in
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
            printf '    Nothing appears as you type it - that is normal. Press Enter when done.\n\n'
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
# Is there a native package of Unisic installed right now? (Distro capability is
# a different question - that one is $native_pm.)
native_installed() {
    if have pacman && { pacman -Qq unisic >/dev/null 2>&1 || pacman -Qq unisic-bin >/dev/null 2>&1; }; then return 0; fi
    if have dpkg && dpkg -s unisic 2>/dev/null | grep -q '^Status: install ok installed'; then return 0; fi
    if have rpm && rpm -q unisic >/dev/null 2>&1; then return 0; fi
    return 1
}

installed_status() {
    local v="" kind="" link tgt p sc
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
    if [ -z "$kind" ] && sc="$(flatpak_scope)"; then
        kind="Flatpak"
        v="$(flatpak_field "$sc" Version)"
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
    # Published for update_note, which needs the same two answers and must not
    # probe every package manager a second time to get them.
    INSTALLED_VER="${v%%-*}"; INSTALLED_KIND="$kind"
    if [ -z "$kind" ]; then printf 'not-installed'
    elif [ -n "$v" ]; then printf 'Unisic %s installed (%s)' "$v" "$kind"
    else printf 'Unisic installed (%s)' "$kind"; fi
}

# Map a chosen menu id to the variables the rest of the script reads.
tui_apply() {
    case "$1" in
        auto)           ACTION="install";   CHANNEL="auto" ;;
        native)         ACTION="install";   CHANNEL="native" ;;
        flatpak)        ACTION="install";   CHANNEL="flatpak" ;;
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
# line shows what's installed. Full redraw from the top each key - no in-place
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
        ids=(auto native)
        labels=(
            "Install or update Unisic          (recommended)"
            "Install the system package        (asks for password)"
        )
        helps=(
            "No password needed, unless Unisic is already installed system-wide."
            "Installs Unisic with your system's package manager."
        )
        hdr=("" "")
        # Only offered where Flatpak exists: there is nothing this installer
        # could do about a missing flatpak command that the user's own software
        # manager does not do better.
        if have flatpak; then
            ids+=(flatpak)
            labels+=("Install the Flatpak version       (sandboxed)")
            helps+=("Installs Unisic as a Flatpak. No password needed.")
            hdr+=("")
        fi
        ids+=(pickver __back)
        labels+=("Install a specific (older) version" "Back")
        helps+=("Pick an exact, older version from a list." "Return to the main menu.")
        hdr+=("" "-")
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
        die "The Unisic installer is interactive - please run it inside a terminal window."
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
    # Only together with the copy they point at: a leftover entry aiming at a
    # deleted binary is worse than none, but the entry alone (from a native or
    # Flatpak install) is not ours to delete.
    if [ "$removed" -eq 1 ]; then
        rm -f "${SHARE_DIR}/applications/app.unisic.Unisic.desktop" \
              "${SHARE_DIR}/icons/hicolor/scalable/apps/app.unisic.Unisic.svg"
    fi
    return $(( removed == 0 ))
}

do_uninstall() {
    local did=0
    # Native package (each one's own postrm/postun drops its update repo). NB:
    # do_uninstall is invoked via `|| ...`, which disables `set -e` inside it -
    # so every native removal checks its own exit status explicitly.
    if have pacman && pacman -Qq unisic >/dev/null 2>&1; then
        say "Removing Unisic... (this asks for your password)"
        priv pacman -R --noconfirm unisic || die "Couldn't remove Unisic."
        did=1
    elif have dpkg && dpkg -s unisic 2>/dev/null | grep -q '^Status: install ok installed'; then
        say "Removing Unisic... (this asks for your password)"
        priv apt-get purge -y unisic || die "Couldn't remove Unisic."
        # The CI .deb's postrm drops the source list on purge, but a package
        # installed FROM the repo has no such scriptlet (on purpose), and the
        # key was written by this installer before any package existed to own
        # it. So both go here, whoever put them there.
        priv rm -f /etc/apt/sources.list.d/unisic.sources \
                   /etc/apt/keyrings/unisic.asc /usr/share/keyrings/unisic.asc || true
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
            # Same reasoning as the apt branch: a COPR-built rpm carries no
            # %postun to drop the repo file, so it is removed here regardless
            # of which side wrote it.
            priv rm -f /etc/yum.repos.d/unisic-copr.repo || true
            did=1
        fi
    fi
    # A Flatpak install can sit next to any of the above, so this is its own
    # check rather than another branch of the chain. --delete-data is what takes
    # ~/.var/app/app.unisic.Unisic (settings and history live in there, not in
    # ~/.config), so it only runs when the user asked to delete their settings.
    local fp_scope fp_args
    if fp_scope="$(flatpak_scope)"; then
        say "Removing the Unisic Flatpak..."
        fp_args=(uninstall -y "--${fp_scope}")
        if [ "$PURGE" -eq 1 ]; then fp_args+=(--delete-data); fi
        fp_run "$fp_scope" "${fp_args[@]}" "$FLATPAK_ID" || die "Couldn't remove the Unisic Flatpak."
        did=1
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
        warn "Unisic doesn't seem to be installed - nothing to remove."
        return 1
    fi
    say "✓ Unisic has been removed."
}

# --- auto-update (portable / AppImage) ---------------------------------
detect_portable_channel() {
    if ls "${PREFIX}"/lib/unisic/*.AppImage >/dev/null 2>&1; then echo appimage
    elif ls -d "${PREFIX}"/lib/unisic-*-x86_64 >/dev/null 2>&1; then echo tarball
    elif flatpak_scope >/dev/null 2>&1; then echo flatpak
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
            say "This install already updates automatically with your system - nothing to set up."
            return 0 ;;
    esac
    # A Flatpak installed for all users belongs to root, so every update asks
    # for the password - which a background timer has no way to answer.
    if [ "$ch" = flatpak ] && [ "$(flatpak_scope || true)" = system ]; then
        say "Unisic is installed as a Flatpak for all users here, and updating that asks for your"
        say "  password every time, so it cannot run in the background. Your software centre"
        say "  updates it instead, or run this installer again whenever you like."
        return 0
    fi
    if ! have systemctl || [ -z "${XDG_RUNTIME_DIR:-}" ]; then
        warn "Automatic updates need a background helper that isn't available here, so I'll skip it.
    You can update anytime by running this installer again."
        return 0
    fi
    mkdir -p "$DATA_DIR" "$UNIT_DIR"
    if [ -f "$0" ] && grep -q 'Unisic universal installer' "$0" 2>/dev/null; then
        cp "$0" "${DATA_DIR}/install.sh"
    else
        # Piped `curl | bash`: there is no on-disk $0 to copy, so this timer's
        # script has to be fetched. Take the release's own copy, which
        # verify_digest can check, and keep the branch head only for a release
        # that predates it. Either way it is run daily with the user's rights
        # from then on, so a wrong file cannot be allowed to install itself.
        local url=""
        : "${RELEASE_JSON:="$(fetch "${API}/releases/latest" 2>/dev/null || true)"}"
        url="$(printf '%s' "$RELEASE_JSON" | asset_url '/install\.sh$')"
        download "${url:-$RAW_URL}" "${DATA_DIR}/install.sh"
        grep -q 'Unisic universal installer' "${DATA_DIR}/install.sh" 2>/dev/null || {
            rm -f "${DATA_DIR}/install.sh"
            die "What came back from GitHub isn't the Unisic installer, so I didn't set up
    automatic updates. Please try again in a moment."
        }
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
    say "Automatic updates are now ON - Unisic will check for a newer version once a day."
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
    You can still build it yourself - see https://github.com/${REPO}" ;;
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
[ -n "$RELEASE_JSON" ] || die "GitHub returned an empty response - please try again."

# Newest tag ("v0.7.5"); used to skip a portable re-install that is up to date.
latest_tag="$(printf '%s' "$RELEASE_JSON" \
    | grep -oE '"tag_name": *"[^"]+"' | head -n1 \
    | sed -E 's/.*"([^"]+)"$/\1/' || true)"
latest_ver="${latest_tag#v}"

# --- detect distro ------------------------------------------------------
# Sourcing os-release also sets NAME/VERSION/PRETTY_NAME/… - harmless as long as
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
    native_pm="dnf"     # Fedora ONLY - the rpm is locked to Fedora's Qt minor.
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

# --- update repositories ------------------------------------------------
# Every native package Unisic ships is tied to the exact Qt it was built
# against: the .deb records `qt6-base-private-abi (= <version>)` (any Qt6 QML
# app does - the QQmlPrivate glue that qt_add_qml_module generates references
# private symbols) and the Fedora rpm links Qt private symbols too. A package
# built for one distro release therefore REFUSES to install on another, which
# is why there is a build per release in Unisic's own repositories, and why
# installing FROM one is the only route that reliably lands a package matching
# the Qt already on the machine. The release asset stays as the fallback: for a
# distro release with no build of its own, and as the only way to install an
# older version on purpose, since a repository only ever offers the newest.
#
# Registering the repo is also what the packages' own scriptlets do
# (packaging/deb/postinst, packaging/rpm/copr-post.sh,
# packaging/arch/unisic.install) - but those only run once the package is
# installed, which is exactly what fails when the package does not fit.

# OBS target for this apt distro, or nothing. Mirrors packaging/deb/postinst:
# when a target is added there, add it here too.
obs_deb_target() {
    case "$ID" in
        debian) case "${VERSION_ID:-}" in 13|13.*) printf 'Debian_13' ;; esac ;;
        ubuntu) case "${VERSION_ID:-}" in
                    26.04) printf 'xUbuntu_26.04' ;;
                    25.10) printf 'xUbuntu_25.10' ;;
                esac ;;
    esac
}

# Staged in $tmpdir and moved into place with one privileged `install` each:
# no `sudo tee` fed by a heredoc, and a half-downloaded key never reaches
# /usr/share/keyrings.
add_apt_repo() {   # <obs target>
    # /etc/apt/keyrings, not /usr/share/keyrings: the latter is for keys a
    # PACKAGE ships, this one is put there by a local admin (this script).
    # `install -D` creates the directory on a system old enough to lack it.
    local key="/etc/apt/keyrings/unisic.asc"
    say "Adding Unisic's software source, so updates arrive with the rest of your system's."
    fetch "${OBS_BASE}/${1}/Release.key" > "${tmpdir}/unisic.asc" || return 1
    grep -q 'BEGIN PGP PUBLIC KEY BLOCK' "${tmpdir}/unisic.asc" || return 1
    printf 'Types: deb\nURIs: %s/%s/\nSuites: ./\nSigned-By: %s\n' \
        "$OBS_BASE" "$1" "$key" > "${tmpdir}/unisic.sources"
    priv install -D -m 0644 "${tmpdir}/unisic.asc" "$key" || return 1
    priv install -D -m 0644 "${tmpdir}/unisic.sources" \
        /etc/apt/sources.list.d/unisic.sources || return 1
}

# Byte-for-byte the file the CI rpm's %post writes (packaging/rpm/copr-post.sh)
# so the two can never disagree about which repo Fedora is on. $releasever and
# $basearch are dnf's own variables and must stay literal in the file, hence
# the quoted heredoc.
add_copr_repo() {
    say "Adding Unisic's software source, so updates arrive with the rest of your system's."
    cat > "${tmpdir}/unisic-copr.repo" <<'EOF'
[copr:copr.fedorainfracloud.org:deandark:Unisic]
name=Copr repo for Unisic owned by deandark
baseurl=https://download.copr.fedorainfracloud.org/results/deandark/Unisic/fedora-$releasever-$basearch/
type=rpm-md
skip_if_unavailable=True
gpgcheck=1
gpgkey=https://download.copr.fedorainfracloud.org/results/deandark/Unisic/pubkey.gpg
repo_gpgcheck=0
enabled=1
enabled_metadata=1
EOF
    priv install -D -m 0644 "${tmpdir}/unisic-copr.repo" \
        /etc/yum.repos.d/unisic-copr.repo || return 1
}

# 0 when $1 is an older version than $2. `sort -V` is what orders 0.8 < 0.8.1
# and 0.7.2b < 0.7.3 correctly; a string compare gets both wrong.
ver_lt() {   # <a> <b>
    [ -n "$1" ] && [ -n "$2" ] && [ "$1" != "$2" ] \
        && [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | head -n1)" = "$1" ]
}

# The newest version the channel just used can actually deliver right now.
# Empty means the channel could not be asked, and the caller then says nothing
# rather than guessing. This is deliberately NOT the installed version: a
# source that has not rebuilt yet offers the old one, and reading what is on
# disk instead is how the note ends up naming a build that does not exist.
channel_offer() {   # <resolved channel>
    case "$1" in
        native)
            case "$native_pm" in
                dnf) dnf repoquery --quiet --latest-limit 1 --qf '%{version}\n' \
                         unisic 2>/dev/null | sort -V | tail -n1 ;;
                apt) apt-cache policy unisic 2>/dev/null \
                         | awk '/Candidate:/ && $2 != "(none)" { print $2; exit }' ;;
                zypper) zypper --non-interactive --quiet info unisic 2>/dev/null \
                         | awk -F': *' '/^Version/ { print $2; exit }' ;;
                # The AUR package builds the release tarball, so it can never be
                # newer than the release itself.
                pacman) printf '%s' "${latest_ver:-}" ;;
            esac ;;
        flatpak)
            if flathub_has_app; then
                flatpak remote-info "--$(flatpak_scope || printf user)" flathub \
                    "$FLATPAK_ID" 2>/dev/null \
                    | awk -F': +' '$1 ~ /^ *Version$/ { print $2; exit }'
            else
                # Not on Flathub yet: the release bundle is the only source
                # there is, and it is never behind the release page.
                printf '%s' "${latest_ver:-}"
            fi ;;
        # Portable installs come straight off the release page.
        *) printf '%s' "${latest_ver:-}" ;;
    esac
}

# True when the channel cannot deliver the newest release yet.
channel_behind() {   # <resolved channel>
    local offer
    offer="$(channel_offer "$1" 2>/dev/null || true)"
    ver_lt "${offer%%-*}" "${latest_ver:-}"
}

# A repository build trails the release by up to a day, and telling someone to
# wait for it when the package is already published is a worse answer than
# fetching it. The release asset is the same build the source will publish, and
# both package managers here reject a package that does not fit BEFORE they
# unpack anything, so a refusal costs the download and nothing else: what the
# source installed a moment ago stays in place and keeps working.
catch_up_from_release() {   # 1 when the newest could not be fetched
    local url="" file
    case "$native_pm" in
        dnf) url="$(printf '%s' "$RELEASE_JSON" | asset_url '\.rpm$')" ;;
        # Built on Debian 13 and welded to its Qt. Ubuntu's build exists only
        # in the OBS repo, so for Ubuntu there is nothing here to fetch.
        apt) [ "${ID:-}" = debian ] \
                 && url="$(printf '%s' "$RELEASE_JSON" | asset_url '\.deb$')" ;;
    esac
    [ -n "$url" ] || return 1
    say "Your software source hasn't built ${latest_ver} yet, so I'll take it from the releases page."
    file="${tmpdir}/$(basename "$url")"
    download "$url" "$file" || return 1
    case "$native_pm" in
        dnf) priv dnf install -y "$file" || return 1 ;;
        apt) priv apt-get install -y "$file" || return 1 ;;
    esac
    say "Installed ${latest_ver}. Your software source keeps updating it from here on."
}

# Three different numbers decide what to say here, and conflating any two of
# them is how this note lies: what is installed now, what the channel can
# deliver, and what the newest release is. A source is only as current as its
# last build there, so "yours is behind" and "you did not get what your source
# has" are separate problems with separate fixes.
update_note() {   # <resolved channel>
    local have_v offer_v where
    # A deliberately picked older version is not something to nag about.
    [ -z "$REQ_VERSION" ] && [ -n "${latest_ver:-}" ] || return 0
    installed_status >/dev/null
    have_v="$INSTALLED_VER"
    offer_v="$(channel_offer "$1" 2>/dev/null || true)"
    offer_v="${offer_v%%-*}"
    case "$1" in
        native)  where="the package built for ${ID:-your system} ${VERSION_ID:-}" ;;
        flatpak) where="the Flatpak on Flathub" ;;
        *)       where="the download for your system" ;;
    esac

    if [ -n "$have_v" ] && [ "$have_v" = "$latest_ver" ]; then
        say "  You have the newest Unisic (${latest_ver})."
    elif ver_lt "$offer_v" "$latest_ver"; then
        # Reaching this means the release package was refused or none exists for
        # this system, so there is nothing left to offer but the wait.
        warn "The newest Unisic is ${latest_ver}, but ${where% } is ${offer_v} so far, and that
    is the one you now have. The build starts by itself when a version is released and
    usually lands within a day; your system will then offer ${latest_ver} as an ordinary update."
    elif ver_lt "$have_v" "$latest_ver"; then
        warn "Version ${latest_ver} is already available to you, but ${have_v} is still installed.
    Install your system's pending updates, or run this installer again."
    fi
}

install_deb() {
    local url file target
    target="$(obs_deb_target)"
    # A picked version can only come from the release page - see above.
    if [ -z "$REQ_VERSION" ] && [ -n "$target" ] && add_apt_repo "$target"; then
        say "Installing Unisic... (from now on it updates with your system's normal updates)"
        priv apt-get update || warn "Refreshing the list of available software reported a problem; carrying on."
        if priv apt-get install -y unisic; then
            # The repo has the build made for THIS distro release, so it goes on
            # first and stays the update channel; only when it is a version
            # behind does the release page get a look in.
            if channel_behind native; then catch_up_from_release || true; fi
            return
        fi
        warn "Installing from Unisic's own software source didn't work, so I'll try the direct download."
    elif [ -z "$target" ]; then
        warn "There is no Unisic built for ${ID} ${VERSION_ID:-} yet, so I'll try the one built for
    Debian 13. If your system has a different version of Qt it will refuse to install, and
    I'll put the portable version in your home folder instead - that one needs no password."
    fi
    url="$(printf '%s' "$RELEASE_JSON" | asset_url '\.deb$')"
    [ -n "$url" ] || die "This release has no Debian package."
    file="${tmpdir}/$(basename "$url")"
    download "$url" "$file"
    say "Installing Unisic... (from now on it updates with your system's normal updates)"
    if priv apt-get install -y "$file"; then return; fi
    # apt refuses an unsatisfiable package before unpacking it, so there is
    # nothing half-installed to clean up here - unlike `dpkg -i`, which is what
    # leaves people with the "dependency problems" state.
    # A distro we DO build for reaching this point means something else broke,
    # so it gets the dead-end message (native_fail dies). Everywhere else the
    # package was a long shot from the start: land on the portable version
    # rather than sending someone back to the menu empty-handed.
    [ -z "$target" ] || native_fail
    warn "That package doesn't fit ${ID} ${VERSION_ID:-}, so I'll install the portable version instead."
    install_tarball
    RESOLVED_CHANNEL="tarball"
}

install_rpm() {
    local url file
    if [ -z "$REQ_VERSION" ] && [ "${ID:-}" = fedora ] && add_copr_repo; then
        say "Installing Unisic... (from now on it updates with your system's normal updates)"
        if priv dnf install -y unisic; then
            if channel_behind native; then catch_up_from_release || true; fi
            return
        fi
        warn "Installing from Unisic's own software source didn't work, so I'll try the direct download."
    fi
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

# Menu entry + icon for the portable copy. The app writes its own on every other
# channel but deliberately not when $APPIMAGE is set (main.cpp): only the
# installer knows the stable path the entry has to point at. Never fatal - a
# missing menu entry is worth a shrug, not a failed install.
install_appimage_desktop() {   # <appimage file>
    local app="$1" ex="${tmpdir}/appdir" src
    local apps="${SHARE_DIR}/applications" icons="${SHARE_DIR}/icons/hicolor/scalable/apps"
    local icon="usr/share/icons/hicolor/scalable/apps/app.unisic.Unisic.svg"
    rm -rf "$ex"; mkdir -p "$ex"
    # Two calls: --appimage-extract takes one pattern, and the icon at the AppDir
    # root is a symlink into usr/ that arrives dangling unless its target is
    # extracted too. Filtered extraction, so neither call unpacks the 60 MB app.
    ( cd "$ex" && "$app" --appimage-extract 'app.unisic.Unisic.desktop' ) >/dev/null 2>&1 || return 0
    ( cd "$ex" && "$app" --appimage-extract "$icon" ) >/dev/null 2>&1 || true
    src="${ex}/squashfs-root/app.unisic.Unisic.desktop"
    [ -f "$src" ] || return 0
    mkdir -p "$apps" "$icons"
    # Absolute Exec, per the rule that applies to every installed .desktop. It
    # does NOT buy the KWin ScreenShot2 fast path here: an AppImage's
    # /proc/<pid>/exe is the transient FUSE mount, never this symlink, so that
    # authorization keeps failing and capture keeps going through the portal
    # (AGENTS.md §3). What this file is for is the applications menu, its icon,
    # and giving the portal a stable app id to remember the permission under.
    sed -E "s|^(Try)?Exec=.*|\\1Exec=${PREFIX}/bin/unisic|" "$src" \
        > "${apps}/app.unisic.Unisic.desktop"
    if [ -f "${ex}/squashfs-root/${icon}" ]; then
        cp -f "${ex}/squashfs-root/${icon}" "${icons}/app.unisic.Unisic.svg"
    fi
    rm -rf "$ex"
}

# The version is part of every portable install's name, so a new one lands BESIDE
# the old one instead of over it: without this, each update quietly left another
# ~62 MB AppImage (or ~185 MB unpacked tarball, measured on 0.7.5) in
# ~/.local/lib. Runs after the
# new copy is in place and the symlink points at it, so a failed install never
# takes the working version with it. Deleting a file that is currently running is
# safe on Linux - the inode stays until that process exits.
prune_portable() {   # <path the current install uses>
    local keep="$1" p
    for p in "${PREFIX}"/lib/unisic/*.AppImage "${PREFIX}"/lib/unisic-*-x86_64; do
        if [ ! -e "$p" ] || [ "$p" = "$keep" ]; then continue; fi
        rm -rf "$p"
    done
    # Left behind when the last AppImage in it was the one just pruned.
    rmdir "${PREFIX}/lib/unisic" 2>/dev/null || true
}

install_appimage() {
    local url dest bindir
    url="$(printf '%s' "$RELEASE_JSON" | asset_url 'x86_64\.AppImage$')"
    [ -n "$url" ] || die "This release has no portable download."
    dest="${PREFIX}/lib/unisic/$(basename "$url")"
    if [ -z "$REQ_VERSION" ] && [ -e "$dest" ] && [ "$(readlink -f "${PREFIX}/bin/unisic" 2>/dev/null)" = "$(readlink -f "$dest")" ]; then
        say "You already have the newest Unisic (${latest_ver:-current}) - nothing to do."
        install_appimage_desktop "$dest"
        prune_portable "$dest"
        return
    fi
    bindir="${PREFIX}/bin"
    mkdir -p "$(dirname "$dest")" "$bindir"
    download "$url" "$dest"
    chmod +x "$dest"
    # An AppImage needs /dev/fuse and a fusermount binary. Where the system has
    # neither (containers, some hardened kernels) the tarball is the same build
    # already unpacked, so fall back to it instead of leaving behind a copy that
    # cannot start.
    if ! "$dest" --appimage-version >/dev/null 2>&1; then
        rm -f "$dest"
        say "This system can't run the portable app directly, so I'll install the unpacked version instead."
        install_tarball
        RESOLVED_CHANNEL="tarball"
        return
    fi
    ln -sf "$dest" "${bindir}/unisic"
    install_appimage_desktop "$dest"
    prune_portable "$dest"
}

install_flatpak() {
    local scope flag origin="" cur="" url file args
    have flatpak || die "This system doesn't have Flatpak installed, so there is nothing to install into.
    Install it with your software manager (for example: sudo apt install flatpak), log out and
    back in once, and run this installer again."

    if scope="$(flatpak_scope)"; then
        origin="$(flatpak_field "$scope" Origin)"
        cur="$(flatpak_field "$scope" Version)"
    else
        scope=user      # a fresh install goes into the user's own installation
    fi
    flag="--${scope}"

    # The app needs org.kde.Platform, which lives on Flathub - so the remote has
    # to be configured even when the app itself arrives as a downloaded bundle.
    fp_run "$scope" remote-add --if-not-exists "$flag" flathub "$FLATHUB_REPO" || true

    # Flathub is the channel as soon as Unisic is published there. An install
    # that already came from it just updates; a bundle install is moved over,
    # and from then on the user's software centre updates Unisic like everything
    # else. A specific older version is never on Flathub, so that one always
    # takes the bundle path below.
    if [ -z "$REQ_VERSION" ] && flathub_has_app; then
        if [ "$origin" = flathub ]; then
            say "Updating Unisic through Flathub..."
            fp_run "$scope" update -y "$flag" "$FLATPAK_ID" \
                || die "Flatpak couldn't update Unisic. Please try again in a moment."
            return
        fi
        args=(install -y "$flag")
        if [ -n "$origin" ]; then
            say "Unisic is on Flathub now, so I'll move this install over to it."
            args+=(--reinstall)
        else
            say "Installing Unisic from Flathub..."
        fi
        fp_run "$scope" "${args[@]}" flathub "$FLATPAK_ID" \
            || die "Flatpak couldn't install Unisic from Flathub. Please try again in a moment."
        return
    fi

    # Up to date before the asset is even looked for: a release that ships no
    # bundle is not a reason to fail when there is nothing to install anyway.
    if [ -z "$REQ_VERSION" ] && [ -n "$cur" ] && [ "$cur" = "$latest_ver" ]; then
        say "You already have the newest Unisic (${latest_ver}) - nothing to do."
        return
    fi
    url="$(printf '%s' "$RELEASE_JSON" | asset_url '\.flatpak$')"
    [ -n "$url" ] || die "This release has no Flatpak download."
    file="${tmpdir}/$(basename "$url")"
    download "$url" "$file"
    say "Installing the Unisic Flatpak..."
    # --reinstall over an existing one: a bundle carries no remote to update
    # from, so replacing the installed copy is the only way a second bundle can
    # land. Settings and history live in ~/.var/app and are left alone by it.
    args=(install -y "$flag")
    if [ -n "$origin" ]; then args+=(--reinstall); fi
    fp_run "$scope" "${args[@]}" --bundle "$file" \
        || die "Flatpak couldn't install the downloaded package. Please try again in a moment."
}

install_tarball() {
    local url tgz
    url="$(printf '%s' "$RELEASE_JSON" | asset_url 'x86_64\.tar\.gz$')"
    [ -n "$url" ] || die "This release has no portable download."
    local want="unisic-${latest_ver}-x86_64"
    if [ -z "$REQ_VERSION" ] && [ -n "$latest_ver" ] \
       && [ -d "${PREFIX}/lib/${want}" ] \
       && [ "$(readlink -f "${PREFIX}/bin/unisic" 2>/dev/null)" = "$(readlink -f "${PREFIX}/lib/${want}/AppRun")" ]; then
        say "You already have the newest Unisic (${latest_ver}) - nothing to do."
        prune_portable "${PREFIX}/lib/${want}"
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
    prune_portable "${PREFIX}/lib/${top}"
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
    # An existing install decides before anything else: a machine that already
    # runs the Flatpak (or a native package, or the extracted portable copy) must
    # get THAT one updated, not a second copy of Unisic installed beside it.
    # With nothing installed the AppImage wins over the native package: it needs
    # no password, and from then on Unisic updates itself in place instead of
    # waiting for this installer to be run again.
    if   [ -n "$native_pm" ] && native_installed;               then eff="native"
    elif flatpak_scope >/dev/null 2>&1;                         then eff="flatpak"
    elif ls -d "${PREFIX}"/lib/unisic-*-x86_64 >/dev/null 2>&1; then eff="tarball"
    else                                                             eff="appimage"; fi
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
            # distro has no native package manager we ship for - fall back to the
            # always-works portable tarball rather than silently doing nothing.
            *)      say "No native package for ${ID:-this system}, installing the portable version instead."
                    install_tarball; RESOLVED_CHANNEL="tarball" ;;
        esac ;;
    appimage)
        if [ "$CHANNEL" = auto ]; then
            if [ "$IS_ATOMIC" -eq 1 ]; then
                say "Your Linux keeps its system files locked, so I'll install the portable version (no password)."
            else
                say "Installing the portable version - no password needed, and it updates itself from then on."
            fi
        fi
        install_appimage ;;
    tarball)
        if [ "$CHANNEL" = auto ]; then
            say "Your Linux (${ID:-unknown}) has no ready-made package, so I'll install the portable version (no password)."
        fi
        install_tarball ;;
    flatpak)
        install_flatpak ;;
esac

# Restore the normal terminal, THEN print the summary so it stays on screen.
leave_alt
printf '\n'
say "✓ Thank you for installing Unisic!"
if [ "$RESOLVED_CHANNEL" = native ]; then
    say "  Open it from your applications menu (search \"Unisic\"), or type:  unisic"
    say "  It updates automatically with the rest of your system."
elif [ "$RESOLVED_CHANNEL" = flatpak ]; then
    say "  Open it from your applications menu (search \"Unisic\")."
    say "  Or type:  flatpak run ${FLATPAK_ID}"
    if [ -f "${UNIT_DIR}/unisic-update.timer" ]; then
        say "  Automatic updates are on - Unisic checks for a newer version once a day."
    else
        say "  To update later, just run this installer again."
    fi
else
    say "  Open it from your applications menu (search \"Unisic\")."
    start_hint
    if [ -f "${UNIT_DIR}/unisic-update.timer" ]; then
        say "  Automatic updates are on - Unisic checks for a newer version once a day."
    else
        say "  To update later, just run this installer again."
    fi
fi
# Every channel, not just the two that go through a repository: whichever route
# was taken, the last word is which version it actually left behind.
update_note "$RESOLVED_CHANNEL"

# X11 users: recording won't work here - say so plainly.
if [ "$(session_kind)" = x11 ]; then x11_notice; fi
