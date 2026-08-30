# Local AntiMicroX configuration

This directory contains the local configuration used with the random-turbo build.

- `antimicrox_settings.ini`: application and controller profile selection settings.
- `mxdJoystick.gamecontroller.amgp`: Xbox 360 Controller mappings.

The RB mapping keeps toggle behavior: press once to start and press again to stop. While active, it emits `Q` with a newly sampled 80–120 ms turbo cycle.

Before restoring these files on another machine, update the absolute profile paths in `antimicrox_settings.ini` to match that machine.
