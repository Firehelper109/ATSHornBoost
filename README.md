# ATSHornBoost

ATSHornBoost is a native x64 SCS SDK plugin for **American Truck Simulator** that turns the horn key into a GTA-style acceleration boost when used with a high-torque companion engine.

The current implementation uses the official SCS Input SDK semantic `aforward` input. It does not patch the ATS executable or write directly to vehicle memory.

## Current behavior

By default:

- `W` = reduced normal throttle controlled by the plugin
- `H` = horn + full boost throttle
- `W + H` = full boost throttle

The normal-throttle reduction lets very high-torque engines remain drivable until the horn is pressed.

## Required one-time ATS setup

1. Open **Options > Keys & Buttons** in ATS.
2. Find **Accelerate**.
3. Unbind `W` from ATS's normal Accelerate action.
4. Leave `H` bound to **Horn**.
5. Keep `ATSHornBoost.dll` and `ATSHornBoost.ini` together in:

```text
<American Truck Simulator>\bin\win_x64\plugins\
```

The plugin still reads the physical `W` key even though ATS no longer owns that binding.

## Configuration

The default `ATSHornBoost.ini` contains:

```ini
[input]
drive_virtual_key=87
trigger_virtual_key=72
require_game_foreground=1

[drive]
normal_throttle=0.10

[boost]
semantic_throttle=1.0
strength=25.0

[debug]
verbose_telemetry=0
```

`87` is the Windows virtual-key code for `W`; `72` is `H`.

Suggested `normal_throttle` values for the Cascadia companion engines:

| Engine | Suggested normal throttle |
| --- | ---: |
| 10,000 N-m | 0.25 |
| 25,000 N-m | 0.10 |
| 50,000 N-m | 0.05 |
| 1 trillion hp | Meme/testing only |

## Companion engine mod

The horn boost becomes much more noticeable with the companion Freightliner Cascadia engine pack. The Workshop package includes 10K, 25K and 50K N-m engines plus a deliberately absurd 1 trillion hp test engine.

The Steam Workshop item can distribute the normal ATS definition portion, but Workshop cannot install an SDK DLL into `bin\win_x64\plugins`. The DLL therefore has to be installed separately.

## Build

Requirements:

- Windows x64
- Visual Studio with Desktop development with C++
- SCS Telemetry & Input SDK 1.14

Download/extract the SCS SDK, then copy its `include` folder so the repository contains:

```text
third_party\scs_sdk\include\scssdk.h
```

The SDK headers are intentionally not committed to this repository. `third_party/scs_sdk/README.md` documents the expected location.

Open `ATSHornBoost.sln`, choose **Release | x64**, then build.

The project copies `config\ATSHornBoost.ini` next to the built DLL automatically.

## Expected game log

Search:

```text
Documents\American Truck Simulator\game.log.txt
```

for `ATSHornBoost`. Successful initialization should include lines similar to:

```text
[ATSHornBoost] Input API initialized.
[ATSHornBoost] Telemetry initialization complete.
[ATSHornBoost] Normal drive key pressed.
[ATSHornBoost] Boost trigger pressed.
```

## Technical notes

ATSHornBoost registers an SCS semantic input device and supplies `aforward` as a float. Normal driving outputs the configured reduced throttle, while the boost key outputs the configured boost throttle, normally `1.0`.

Because the current implementation still uses ATS's normal drivetrain, gearing, RPM limits, traction and transmission behavior remain relevant. This is not direct rigid-body force or velocity injection.

## Releases

For public distribution, attach the compiled `ATSHornBoost.dll` and `ATSHornBoost.ini` to a GitHub Release ZIP. Users only need those runtime files, not Visual Studio build output.

## License

No separate license has yet been selected for the original ATSHornBoost source code. Until one is added, GitHub users can view the public source but do not automatically receive broad reuse/redistribution rights to the original code.
