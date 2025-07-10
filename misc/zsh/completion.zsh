#compdef nix

function _nix() {
  local input=("${(Q)words[@]}")
  local -a suggestions # what value to actually use on completion
  local -a suggestions_display # what to display to the user
  local tpe suggestion description
  local ifs_bk="$IFS"
  IFS=$'\t'
  NIX_GET_COMPLETIONS=$((CURRENT - 1)) "$input[@]" 2>/dev/null | {read tpe; while IFS=$'\t' read -r suggestion description; do
    suggestions+=("$suggestion")
    if [ -n "$description" ]; then
      suggestions_display+=("$suggestion -- $description")
    else
      suggestions_display+=("$suggestion")
    fi
  done}
  IFS="$ifs_bk"
  local -a args
  if [[ "$tpe" == filenames ]]; then
    args+=('-f')
  elif [[ "$tpe" == attrs ]]; then
    args+=('-S' '')
  fi
  compadd -J nix "${args[@]}" -d suggestions_display -a suggestions
}

_nix "$@"
