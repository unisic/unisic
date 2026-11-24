.pragma library

// "Meta+Shift+Print" -> ["Meta", "Shift", "Print"]. A trailing "+" is the
// plus key itself, not a separator. Only the first binding of a list is
// shown: it is the one a tile names.
function keysOf(hotkey) {
    return hotkey === "" ? [] : (hotkey.split(", ")[0].match(/[^+]+|\+$/g) || [])
}

// The first binding of a list, for a spoken name.
function first(hotkey) {
    return hotkey.split(", ")[0]
}
