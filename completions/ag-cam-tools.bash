# bash completion for ag-cam-tools
#
# Source this file or add to ~/.bashrc:
#   source /path/to/completions/ag-cam-tools.bash

_ag_cam_tools() {
    local cur prev subcmds
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    subcmds="connect list capture stream focus calibration-capture"

    # Complete subcommand as first argument
    if [[ ${COMP_CWORD} -eq 1 ]]; then
        COMPREPLY=( $(compgen -W "${subcmds}" -- "${cur}") )
        return 0
    fi

    # Complete --serial and --address with discovered cameras
    case "${prev}" in
        -s|--serial)
            local serials
            serials=$(ag-cam-tools list --machine-readable 2>/dev/null | cut -f3)
            COMPREPLY=( $(compgen -W "${serials}" -- "${cur}") )
            return 0
            ;;
        -a|--address)
            local addrs
            addrs=$(ag-cam-tools list --machine-readable 2>/dev/null | cut -f1)
            COMPREPLY=( $(compgen -W "${addrs}" -- "${cur}") )
            return 0
            ;;
        -e|--encode)
            COMPREPLY=( $(compgen -W "pgm png jpg" -- "${cur}") )
            return 0
            ;;
        -b|--binning)
            COMPREPLY=( $(compgen -W "1 2" -- "${cur}") )
            return 0
            ;;
        -r|--rectify)
            # Complete with calibration session folders that contain calib_result/
            local sessions
            sessions=$(find . calibration -maxdepth 2 -type d -name 'calibration_*' 2>/dev/null | \
                       while read -r d; do [ -d "$d/calib_result" ] && echo "$d"; done)
            COMPREPLY=( $(compgen -W "${sessions}" -- "${cur}") )
            return 0
            ;;
        -o|--output)
            COMPREPLY=( $(compgen -d -- "${cur}") )
            return 0
            ;;
    esac

    # Subcommand-specific option completion
    local subcmd="${COMP_WORDS[1]}"
    case "${subcmd}" in
        connect)
            COMPREPLY=( $(compgen -W "-s --serial -a --address -i --interface -h --help" -- "${cur}") )
            ;;
        list)
            COMPREPLY=( $(compgen -W "-i --interface --machine-readable -h --help" -- "${cur}") )
            ;;
        capture)
            COMPREPLY=( $(compgen -W "-s --serial -a --address -i --interface -o --output -e --encode -x --exposure -b --binning -v --verbose -h --help" -- "${cur}") )
            ;;
        stream)
            COMPREPLY=( $(compgen -W "-s --serial -a --address -i --interface -f --fps -x --exposure -g --gain -A --auto-expose -b --binning -p --packet-size -r --rectify -t --tag-size -h --help" -- "${cur}") )
            ;;
        focus)
            COMPREPLY=( $(compgen -W "-s --serial -a --address -i --interface -f --fps -x --exposure -b --binning --roi -h --help" -- "${cur}") )
            ;;
        calibration-capture)
            COMPREPLY=( $(compgen -W "-s --serial -a --address -i --interface -o --output -n --count -f --fps -x --exposure -g --gain -A --auto-expose -b --binning -p --packet-size -h --help" -- "${cur}") )
            ;;
    esac
}

complete -F _ag_cam_tools ag-cam-tools
