# bash completion for unisic. The options, and which of them take a value, are
# read from `unisic --help`, which the binary answers before Qt starts, so this
# file never lists them and can never fall behind the real CLI. Unisic has no
# subcommands, so an empty word lists every option, like `rpm <Tab><Tab>`,
# instead of the file names bash's generic --help completion offers there.
_unisic()
{
    local cur=${COMP_WORDS[COMP_CWORD]} prev=${COMP_WORDS[COMP_CWORD-1]}
    # `=` splits words, so `--output=` arrives as `--output` `=` and the value.
    if [[ $cur == = ]]; then
        cur=
    elif [[ $prev == = ]] && ((COMP_CWORD > 1)); then
        prev=${COMP_WORDS[COMP_CWORD-2]}
    fi

    local line opts=() value=
    while read -r line; do
        [[ $line =~ ^(--[a-z][a-z-]*)(\ ([A-Z_]+)(\ |$))? ]] || continue
        opts+=("${BASH_REMATCH[1]}")
        if [[ ${BASH_REMATCH[1]} == "$prev" ]]; then
            value=${BASH_REMATCH[3]}
        fi
    done < <(LC_ALL=C "$1" --help 2>/dev/null)

    case $value in
        '') COMPREPLY=($(compgen -W "${opts[*]}" -- "$cur")) ;;
        PATH) compopt -o default; COMPREPLY=() ;;
        FORMAT) COMPREPLY=($(compgen -W 'png jpg webp gif' -- "$cur")) ;;
        *) COMPREPLY=() ;;
    esac
}
complete -F _unisic unisic
