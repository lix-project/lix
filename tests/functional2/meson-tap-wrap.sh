set -euo pipefail

# our tap plugin can't print a version header because that'd end up duplicated on stdout
printf "TAP version 13\n"

$@ | while read line; do
    case "$line" in
        "TAP version"* | "1.."* | "ok "* | "not ok "*" # TODO expected failure")
            printf "%s\n" "$line"
            ;;

        "not ok "*)
            printf "%s\n" "$line"
            printf "\n\e[1;31m%s\e[0m\n" "$line" >&2
            ;;

        # "  " is yaml test info, we should probably pass that through even though we don't advertise it
        "# "* | "  "*)
            printf "%s\n" "$line" >&2
            ;;

        *)
            # probably xdist garbage -sigh- keep it to at least make this obvious on inspection
            printf "%s\n" "$line" >&2
            ;;
    esac
done
