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

_ag_cam_tools_calib_sessions() {
    local -a sessions
    sessions=(${(f)"$(find . calibration -maxdepth 2 -type d -name 'calibration_*' 2>/dev/null | \
               while read -r d; do [ -d "$d/calib_result" ] && echo "$d"; done)"})
    compadd -a sessions
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
        '(-g --gain)'{-g,--gain}'=[sensor gain in dB]:gain:' \
        '(-A --auto-expose)'{-A,--auto-expose}'[auto-expose then lock]' \
        '(-b --binning)'{-b,--binning}'=[sensor binning factor]:factor:(1 2)' \
        '(-p --packet-size)'{-p,--packet-size}'=[GigE packet size]:bytes:' \
        '(-r --rectify)'{-r,--rectify}'=[rectify using calibration session]:session:_ag_cam_tools_calib_sessions' \
        '(-t --tag-size)'{-t,--tag-size}'=[AprilTag size in meters]:meters:' \
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
        '(-q --quiet-audio)'{-q,--quiet-audio}'[disable focus audio feedback]' \
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
        '(-q --quiet-audio)'{-q,--quiet-audio}'[disable capture confirmation audio]' \
        '(-h --help)'{-h,--help}'[print this help]'
}

_ag_cam_tools_depth_preview() {
    _arguments \
        '(-a --address)-s[match by serial number]:serial:_ag_cam_tools_cameras_serial' \
        '(-a --address)--serial=[match by serial number]:serial:_ag_cam_tools_cameras_serial' \
        '(-s --serial)-a[connect by camera IP]:address:_ag_cam_tools_cameras_address' \
        '(-s --serial)--address=[connect by camera IP]:address:_ag_cam_tools_cameras_address' \
        '(-i --interface)'{-i,--interface}'=[force NIC selection]:interface:_net_interfaces' \
        '(-f --fps)'{-f,--fps}'=[trigger rate in Hz]:rate:' \
        '(-x --exposure)'{-x,--exposure}'=[exposure time in microseconds]:microseconds:' \
        '(-g --gain)'{-g,--gain}'=[sensor gain in dB]:gain:' \
        '(-A --auto-expose)'{-A,--auto-expose}'[auto-expose then lock]' \
        '(-b --binning)'{-b,--binning}'=[sensor binning factor]:factor:(1 2)' \
        '(-p --packet-size)'{-p,--packet-size}'=[GigE packet size]:bytes:' \
        '(-r --rectify)'{-r,--rectify}'=[calibration session folder]:session:_ag_cam_tools_calib_sessions' \
        '--stereo-backend=[stereo disparity backend]:backend:(sgbm onnx igev rt-igev foundation)' \
        '--model-path=[path to ONNX model file]:file:_files' \
        '--min-disparity=[override calibration min_disparity]:disparity:' \
        '--num-disparities=[override calibration num_disparities]:disparities:' \
        '--block-size=[SGBM block size]:size:' \
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
        'depth-preview-classical:Live depth map with classical backend controls'
        'depth-preview-neural:Live depth map with neural backend controls'
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
            depth-preview-classical) _ag_cam_tools_depth_preview ;;
            depth-preview-neural) _ag_cam_tools_depth_preview ;;
        esac
    fi
}

_ag_cam_tools "$@"
