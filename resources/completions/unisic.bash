# bash completion for unisic. The options are read from `unisic --help`, which
# the binary answers before Qt starts, so this file never lists them and can
# never fall behind the real CLI. bash-completion 2.12 renamed _longopt.
if declare -F _comp_complete_longopt >/dev/null; then
    complete -F _comp_complete_longopt unisic
else
    complete -F _longopt unisic
fi
