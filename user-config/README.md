# Local AntiMicroX configuration

Snapshot updated on 2026-09-28 from the files used by the deployed build.

- `antimicrox_settings.ini`: application settings, selected profiles, alternating suites and potion timer settings.
- `maple-max-flydigi-dinput.joystick.amgp`: current Flydigi DInput profile.
- `maple-max-flydigi-dinput-平台跳跃专用.joystick.amgp`: platform-jumping profile.
- `maple-max.gamecontroller.amgp`: Maple Max/Xbox 360 profile.
- `mxdJoystick.gamecontroller.amgp`: additional Xbox 360 profile.
- `actions/`: four saved custom actions from the local action library.

The active DInput controller's three-suite rotation uses button/8 for 16–20 seconds,
button/1 for 0.5–0.6 seconds, and dpad/0/4 for 2.2 seconds.
The potion timer stores Shift with a 175-second interval and immediate first use.

## Restore

Close AntiMicroX before restoring. Copy the desired profiles to your Documents
folder and update absolute profile paths in `antimicrox_settings.ini` to match
your machine. Copy that settings file to `%LOCALAPPDATA%/antimicrox/`.
Copy the JSON files in `actions/` to `%LOCALAPPDATA%/AntiMicroX-RandomTurbo/actions/`.

Run `dist/AntiMicroX-RandomTurbo/antimicrox.exe` with all its companion files kept
in the same directory. `ActionRecorder.exe` is required for recorded actions.
Historical local backups and input-analysis recordings are not part of this snapshot.
