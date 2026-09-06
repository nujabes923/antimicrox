# Local AntiMicroX configuration

This directory contains the local configuration used with the deployed random-turbo build at
`dist/AntiMicroX-RandomTurbo/antimicrox.exe`.

- `antimicrox_settings.ini`: application and controller profile selection settings.
- `mxdJoystick.gamecontroller.amgp`: Xbox 360 Controller mappings.
- `maple-max.gamecontroller.amgp`: the currently selected Maple Max/Xbox 360 Controller mappings.

The profile and application settings are snapshots of the files currently used on the
local machine. The deployed executable is committed separately under `dist` so the
same tested build can be downloaded without rebuilding it.

The application settings snapshot also includes the current three-suite rotation:

- Suite 1: `button/10`, random 8–11 seconds.
- Suite 2: `button/2`, fixed 1 second.
- Suite 3: `vdpad/0/2`, fixed 2.2 seconds.

Before restoring these files on another machine, update the absolute profile paths in `antimicrox_settings.ini` to match that machine.
