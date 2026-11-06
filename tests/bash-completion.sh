#!/usr/bin/env bash
# Runs resources/completions/unisic.bash against the real `unisic --help`.
# Usage: bash-completion.sh <unisic binary> <unisic.bash>
set -u
binary=$1
source "$2"
# compopt only works inside a real completion; the PATH case calls it.
compopt() { :; }

fails=0
check()
{
    local expected=$1
    shift
    COMP_WORDS=(unisic "$@")
    COMP_CWORD=$#
    COMPREPLY=()
    _unisic "$binary"
    local got="${COMPREPLY[*]}"
    if [[ $got != "$expected" ]]; then
        printf 'FAIL: unisic %s\n  expected: %s\n  got:      %s\n' "$*" "$expected" "$got"
        fails=$((fails + 1))
    fi
}

all=$("$binary" --help | grep -oE '^ +--[a-z][a-z-]*' | tr -d ' ' | tr '\n' ' ')
all=${all% }
[[ $all == *--fullscreen*--version* ]] || { echo "FAIL: --help lists no options"; exit 1; }

check "$all" ''
check '--region --recapture' --re
check "$all" --region ''
check 'png jpg webp gif' --format ''
check 'webp' --format w
check 'png jpg webp gif' --format =
check 'webp' --format = w
check '' --delay ''
check '' --output ''
check '' --output = ''
check '' --hotkey ''
check '' --export-settings ''

((fails == 0)) && echo "bash completion: all checks passed"
exit $((fails > 0))
