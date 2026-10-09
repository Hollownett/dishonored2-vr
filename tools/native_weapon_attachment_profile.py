"""Default-off passive native attachment profile; observations are not outcomes."""
def validate_native_weapon_attachment(args):
    if not getattr(args,'native_weapon_attachment_trace',False):
        return
    if not (args.paired and args.vr_preview and args.native_eye_capture and args.head_tracking and args.controllers and args.dll_name):
        raise ValueError('Native attachment trace requires paired native-eye XR, head/controllers and --dll-name')
    if args.until_stopped or not 10<=args.seconds<=120 or args.saved_settings or args.native_left_aim_experiment or args.simulated_cleanup_exception:
        raise ValueError('Native attachment trace requires timed 10..120 seconds without left aim, cleanup exception or saved settings')
