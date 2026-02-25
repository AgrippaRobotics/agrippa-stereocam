#compdef ag-cam-tools
#
# zsh completion for ag-cam-tools
#
# Install: copy to a directory in $fpath, or source directly:
#   source /path/to/completions/ag-cam-tools.zsh

_ag_cam_tools_cameras_serial() {
    local -a serials
    serials=(${(f)"$(ag-cam-tools list --machine-readable 2>/dev/null | cut -f3)"})
    compadd -a serials
}

_ag_cam_tools_cameras_address() {
    local -a addrs
    addrs=(${(f)"$(ag-cam-tools list --machine-readable 2>/dev/null | cut -f1)"})
    compadd -a addrs
}

_ag_cam_tools_connect() {
    _arguments \
        '(-a --address)-s[match by serial number]:serial:_ag_cam_tools_cameras_serial' \
        '(-a --address)--serial=[match by serial number]:serial:_ag_cam_tools_cameras_serial' \
        '(-s --serial)-a[connect by camera IP]:address:_ag_cam_tools_cameras_address' \
        '(-s --serial)--address=[connect by camera IP]:address:_ag_cam_tools_cameras_address' \
        '(-i --interface)'{-i,--interface}'=[restrict to this NIC]:interface:_net_interfaces' \
        '(-h --help)'{-h,--help}'[print this help]'
}

_ag_cam_tools_list() {
    _arguments \
        '(-i --interface)'{-i,--interface}'=[restrict to this NIC]:interface:_net_interfaces' \
        '--machine-readable[tab-separated output for completions]' \
        '(-h --help)'{-h,--help}'[print this help]'
}

_ag_cam_tools_capture() {
    _arguments \
        '(-a --address)-s[match by serial number]:serial:_ag_cam_tools_cameras_serial' \
        '(-a --address)--serial=[match by serial number]:serial:_ag_cam_tools_cameras_serial' \
        '(-s --serial)-a[connect by camera IP]:address:_ag_cam_tools_cameras_address' \
        '(-s --serial)--address=[connect by camera IP]:address:_ag_cam_tools_cameras_address' \
        '(-i --interface)'{-i,--interface}'=[force NIC selection]:interface:_net_interfaces' \
        '(-o --output)'{-o,--output}'=[output directory]:directory:_directories' \
        '(-e --encode)'{-e,--encode}'=[output format]:format:(pgm png jpg)' \
        '(-x --exposure)'{-x,--exposure}'=[exposure time in microseconds]:microseconds:' \
        '(-b --binning)'{-b,--binning}'=[sensor binning factor]:factor:(1 2)' \
        '(-v --verbose)'{-v,--verbose}'[print diagnostic readback]' \
        '(-h --help)'{-h,--help}'[print this help]'
}

_ag_cam_tools_stream() {
    _arguments \
        '(-a --address)-s[match by serial number]:serial:_ag_cam_tools_cameras_serial' \
        '(-a --address)--serial=[match by serial number]:serial:_ag_cam_tools_cameras_serial' \
        '(-s --serial)-a[connect by camera IP]:address:_ag_cam_tools_cameras_address' \
        '(-s --serial)--address=[connect by camera IP]:address:_ag_cam_tools_cameras_address' \
        '(-i --interface)'{-i,--interface}'=[force NIC selection]:interface:_net_interfaces' \
        '(-f --fps)'{-f,--fps}'=[trigger rate in Hz]:rate:' \
        '(-x --exposure)'{-x,--exposure}'=[exposure time in microseconds]:microseconds:' \
        '(-b --binning)'{-b,--binning}'=[sensor binning factor]:factor:(1 2)' \
        '(-h --help)'{-h,--help}'[print this help]'
}

_ag_cam_tools_focus() {
    _arguments \
        '(-a --address)-s[match by serial number]:serial:_ag_cam_tools_cameras_serial' \
        '(-a --address)--serial=[match by serial number]:serial:_ag_cam_tools_cameras_serial' \
        '(-s --serial)-a[connect by camera IP]:address:_ag_cam_tools_cameras_address' \
        '(-s --serial)--address=[connect by camera IP]:address:_ag_cam_tools_cameras_address' \
        '(-i --interface)'{-i,--interface}'=[force NIC selection]:interface:_net_interfaces' \
        '(-f --fps)'{-f,--fps}'=[trigger rate in Hz]:rate:' \
        '(-x --exposure)'{-x,--exposure}'=[exposure time in microseconds]:microseconds:' \
        '(-b --binning)'{-b,--binning}'=[sensor binning factor]:factor:(1 2)' \
        '*--roi=[region of interest x y w h]:roi:' \
        '(-h --help)'{-h,--help}'[print this help]'
}

_ag_cam_tools_calibration_capture() {
    _arguments \
        '(-a --address)-s[match by serial number]:serial:_ag_cam_tools_cameras_serial' \
        '(-a --address)--serial=[match by serial number]:serial:_ag_cam_tools_cameras_serial' \
        '(-s --serial)-a[connect by camera IP]:address:_ag_cam_tools_cameras_address' \
        '(-s --serial)--address=[connect by camera IP]:address:_ag_cam_tools_cameras_address' \
        '(-i --interface)'{-i,--interface}'=[force NIC selection]:interface:_net_interfaces' \
        '(-o --output)'{-o,--output}'=[base output directory]:directory:_directories' \
        '(-n --count)'{-n,--count}'=[target number of pairs]:count:' \
        '(-f --fps)'{-f,--fps}'=[preview rate in Hz]:rate:' \
        '(-x --exposure)'{-x,--exposure}'=[exposure time in microseconds]:microseconds:' \
        '(-g --gain)'{-g,--gain}'=[sensor gain in dB]:gain:' \
        '(-A --auto-expose)'{-A,--auto-expose}'[auto-expose then lock]' \
        '(-b --binning)'{-b,--binning}'=[sensor binning factor]:factor:(1 2)' \
        '(-p --packet-size)'{-p,--packet-size}'=[GigE packet size]:bytes:' \
        '(-h --help)'{-h,--help}'[print this help]'
}

_ag_cam_tools() {
    local -a subcmds
    subcmds=(
        'connect:Connect to a camera and print device info'
        'list:Discover and list GigE cameras'
        'capture:Capture a single stereo frame pair'
        'stream:Real-time stereo preview via SDL2'
        'focus:Real-time focus scoring for lens adjustment'
        'calibration-capture:Interactive stereo pair capture for calibration'
    )

    if (( CURRENT == 2 )); then
        _describe 'command' subcmds
    else
        case "${words[2]}" in
            connect) _ag_cam_tools_connect ;;
            list)    _ag_cam_tools_list ;;
            capture) _ag_cam_tools_capture ;;
            stream)  _ag_cam_tools_stream ;;
            focus)   _ag_cam_tools_focus ;;
            calibration-capture) _ag_cam_tools_calibration_capture ;;
        esac
    fi
}

_ag_cam_tools "$@"
