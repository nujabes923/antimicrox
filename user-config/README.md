# Local AntiMicroX configuration

This directory contains the local configuration used with the random-turbo build.

- `antimicrox_settings.ini`: application and controller profile selection settings.
- `mxdJoystick.gamecontroller.amgp`: Xbox 360 Controller mappings.
- `maple-max.gamecontroller.amgp`: Maple Max Xbox Series X Controller mappings.

The current profile snapshot includes the latest button sequences, toggle mappings,
and random turbo settings. Button 11 toggles `Q` with a newly sampled 120–200 ms
turbo cycle.

The application settings snapshot also includes the current three-suite rotation:

- Suite 1: `button/10`, random 5–8 seconds.
- Suite 2: `axis/5/positive`, random 5–8 seconds.
- Suite 3: `button/3`, fixed 1 second.

Before restoring these files on another machine, update the absolute profile paths in `antimicrox_settings.ini` to match that machine.
