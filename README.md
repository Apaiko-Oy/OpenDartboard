<div align="center">
<a href="https://opendartboard.org/" rel="noopener" target="_blank"><img src="https://img.shields.io/badge/NOTICE:-This_project_is_in_early_development_and_not_ready_for_production_use!-orange.svg" alt="Opendartboard Logo"></a>
<p> </p>
</div>

<!-- markdownlint-disable-next-line -->
<p align="center">
  <a href="http://opendartboard.org/" rel="noopener" target="_blank"><img width="192" height="204" src="assets/logo2.png" alt="Opendartboard Logo"></a>
</p>

<h1 align="center">OpenDartboard</h1>

> **This is Apaiko-Oy's fork**, maintained for [Turnaus](https://github.com/Apaiko-Oy/turnaus)
> and forked from upstream [OpenDartboard/OpenDartboard](https://github.com/OpenDartboard/OpenDartboard)
> at commit `c919ef85998f`. The upstream `LICENSE` (GPL-3.0) is carried unmodified. The Windows
> port lands on the `windows-port` branch. A release zip from this fork carries the `.exe`, the
> `LICENSE`, and the commit its source was built from, which is what makes handing somebody the
> binary lawful: the source offer is this repository at that commit.
>
> **Which Turnaus a board talks to.** The two pairing flags — `--pair <code>` and
> `--pair-contest <code>` — and the score push resolve the address in
> this order, and the first one set wins: `--turnaus <url>`, then the `OD_TURNAUS_URL`
> environment variable, then the `base_url` the last successful pairing stored in the credential
> file, then the default, **`https://turnaus.apaiko.fi`** — Turnaus' production. The board logs
> one line at startup naming the address and which of the four chose it. The default is written
> once, in `src/communication/turnaus_address.hpp`, and `testers/check_default_address.sh` fails
> the tree if a second copy appears or if the parked domain an earlier default named comes back.
>
> **Two doors, two flags.** `--pair <code>` exchanges a club's six-digit code for the credential
> that makes this board a Station of an Organisation. `--pair-contest <code>` binds it to one
> Casual Contest for that evening instead — somebody's knockabout, in a pub or in a garage — and
> to nothing else: no Station and no Organisation. A club pairing already on the board is kept,
> so a board that is Station 3 on Tuesday still is on Thursday: the Contest binding wins while it
> lasts, and the club's is underneath it again when the evening ends. Either flag pairs and exits,
> opening no camera. Started in an interactive console with no credential and neither flag, the
> board simply asks for a six-digit code and tries the club's door then a Casual Contest's, which
> is the ordinary way to pair; the flags are for a board set up without somebody standing at it.
>
> **Which cameras a start opens.** `--cams` wins where it is given: those devices, in that order,
> with nothing probed and nothing asked. Without it the board no longer assumes `0,1,2` — on a
> laptop index 0 is the built-in webcam, so the three board cameras are 1, 2 and 3, all three
> defaults open, and until #1318 the detector calibrated the operator's face as camera 1 and never
> opened the third board camera at all. Instead every video device on the machine is looked
> through, one at a time, and the first three that can see a dartboard are the ones that are
> opened. A device that cannot is named and left alone. `--autocams` is unchanged and is a
> different question: it asks what a camera's electronics can do — whether it will negotiate MJPG,
> which matters because three 1280x720 cameras do not fit uncompressed on one USB bus — and most
> laptop webcams answer yes, so it cannot tell where a camera points. `src/utils/board_look.hpp`
> holds what "can see a dartboard" means and the measurements it was set from.

<div align="center">

[![License](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](https://github.com/OpenDartboard/OpenDartboard/blob/main/LICENSE)
[![Versions](https://img.shields.io/badge/versions-v0.1.3-green.svg)](https://github.com/OpenDartboard/OpenDartboard/releases)
[![Platform](https://img.shields.io/badge/platform-arm64-red.svg)](https://github.com/opendartboard/opendartboard)
[![Language](https://img.shields.io/badge/Language-C++-pink.svg)](https://github.com/opendartboard/opendartboard)
[![Libs](https://img.shields.io/badge/Libs-OpenCV_•_httplib_•_json-white.svg)](https://github.com/opendartboard/opendartboard)
[![Discord](https://img.shields.io/badge/Discord-Join-7289da.svg)](https://discord.gg/b8YwrbN2ju)

</div>

**OpenDartboard** is a hobby-friendly, fully FOSS toolkit for building an automatic _steel-tip_ dart–scoring station at home or in a bar.

- **Headless Scorer** — Use a Raspberry Pi Zero 2 W with 3 cameras to detect darts and output scores in standard notation (`T20`, `S5`, `D12`, …) via a easy to consume [WebSocket API](docs/api.md).
- **Computer Vision** — Includes a lightweight OpenCV detector to run efficiently on a Pi Zero 2 W or build your own detector via a [Custom Detector](docs/detectors.md) plugin.

The goal: **drop-in freedom** for home tinkerers or bar owners who want "Automatic darts scoring" without closed hardware, subscriptions, or vendor lock-in.

## Table of Contents

- [Quick Start](#quick-start)
- [Recommended Clients](#recommended-clients)
- [Project Status & Roadmap](#project-status--roadmap)
- [Tech Stack](#tech-stack)
- [Hardware Reference](#hardware-reference)
- [Development Environment](#development-environment)
- [API Documentation](#api-documentation)
- [Custom Detectors](#custom-detectors)
- [Contributing](#contributing)

---

## Quick Start

```shell
# 1 Flash MicroSD with Rasbperi Pi Imager (https://www.raspberrypi.com/software/) | Image: Raspberry Pi OS Lite (64-bit)
# 1.1 - set hostname to `opendartboard`.
# 1.2 - set username as `pi`, and any password.
# 1.3 - set Wi-Fi name and password.
# 1.2 - enable SSH in services.
# 1.4 - Flash MicroSD - once complete power on Raspberry Pi
# 1.5 - Connect to it with terminal/cmd using SSH:
ssh pi@opendartboard.local

# 2. Update the package lists
sudo apt-get update

# 3. Download & install the latest .deb release
wget https://github.com/OpenDartboard/OpenDartboard/releases/download/v0.1.3/opendartboard_0.1.3-1_arm64.deb
sudo apt install -y ./opendartboard_0.1.3-1_arm64.deb

# 4. Run it and Watch the scores
opendartboard --autocams
```

> **Tip**: Need a quick debug dashboard? Run [`debug.opendartboard.org`](http://debug.opendartboard.org) in any modern browser to see the score output, camera feeds, calibrations images, and more.

## Clients

🚧 We are working on a set of official clients to make it easier to use OpenDartboard. These will be available for various platforms, and will allow you to view scores, play, manage settings, and more.

```yml
- **Web**:     [COMMING SOON] - A web client for viewing scores and managing settings.
- **Android**: [COMMING SOON] - A native Android app to connect to your OpenDartboard server.
- **iOS**:     [COMMING SOON] - An iOS app to connect to your OpenDartboard server.
- **Tablet**:  [COMMING SOON] - A tablet client for viewing scores and managing settings.
- **Windows**: [COMMING SOON] - A Windows client for viewing scores and managing settings.
- **Linux**:   [COMMING SOON] - A Linux client for viewing scores and managing settings.
- **macOS**:   [COMMING SOON] - A macOS client for viewing scores and managing settings.
- **TV's**:    [COMMING SOON] - A TV client for viewing scores on your big screen.
```

For now, you can try out the [Debug Dashboard](http://debug.opendartboard.org) or integrate with your own applications using the [WebSocket API](docs/api.md).

## Project Status & Roadmap

| Phase           | Target                                             | Status (ETA) |
| --------------- | -------------------------------------------------- | ------------ |
| **Packaging**   | Debian package (.deb) for easy installation --     | ✅ released  |
| **Development** | Docker‑based dev environment for consistent builds | ✅ ready     |
| **MVP**         | Basic Auto‑calibration via OpenCV                  | ✅ done      |
| **CI/CD**       | Basic CI pipelines & tagged releases               | ✅ live      |
| **MMR**         | Scoring via OpenCV (`geometry_detector`)           | 🚧 WIP       |
| **API**         | WebSocket API for real‑time score streaming        | ✅ v0.1      |
| **Polish**      | Improved accuracy & self‑calibration               | 🗓 T.B.D.     |

---

## Tech Stack

| Layer               | Tech                         | Notes                                |
| ------------------- | ---------------------------- | :----------------------------------- |
| **Computer Vision** | `OpenCV`                     | INT8‑optimised model for ARM         |
| **Runtime**         | C++                          | High‑performance dart detection      |
| **API**             | `WebSocket` · `JSON`         | Real‑time score streaming on `13520` |
| **Infrastructure**  | `systemd` · `udev`           | Robust auto‑start & camera hot‑swap  |
| **Development**     | `Docker` · `Debian Bullseye` | Reproducible builds                  |
| **Distribution**    | `.deb` package               | CI‑checked, one‑command install      |

---

## Hardware Reference

| Item         | Minimum spec                                       | Example                       |
| ------------ | -------------------------------------------------- | ----------------------------- |
| SBC          | Raspberry Pi Zero 2 W (+ self-powered USB 2 hub)   | Waveshare USB HUB HAT (B)     |
| Cameras (×3) | USB 2.0 webcams outputting MJPEG @ 1280x720 30 fps | HBVCAM OV2710 100°            |
| Lighting     | 360° LED ring                                      | DIY SmartLite 12 V LED 6000 K |
| Power        | 5 V / 3 A PSU                                      | Any USB PD brick + adapter    |

---

## Development Environment

```sh
# Clone repository
git clone https://github.com/OpenDartboard/OpenDartboard.git
cd OpenDartboard

# Build the dev image (once)
docker compose build opendartboard

# Open an interactive shell with everything mounted
# Run `$env:PWD = (Get-Location).Path` #FOR WINDOWS
docker compose run --rm --service-ports opendartboard /bin/bash

# Builds and installs binary
make build

# Run the binary with mocks
opendartboard --debug --cams mocks/cam_1.mp4,mocks/cam_2.mp4,mocks/cam_3.mp4 --width 1280 --height 720
```

## Testers

`testers/` holds the harnesses the issues in this repository were carried with. One command
runs all of them and names the ones that failed:

```sh
testers/run_all.sh              # build, then every tester; non-zero if any failed
testers/run_all.sh 1320 1317    # only the testers whose label contains one of these
OD_SKIP_BUILD=1 testers/run_all.sh   # measure the binary already in build/
```

It runs on the host (it drives Docker), not inside the dev container, and it builds
`build/opendartboard` with the dev defines first: several testers assert numbers that
belong to that build, because `DEBUG_SEEK_VIDEO` seeks a file source three seconds in and
a release binary calibrates on a different frame of the same clip.

A tester runs from whatever checkout it is in -- no path in `testers/` names a worktree --
and its run output goes to `runs-<checkout>/` beside the tree.

## API Documentation

See [`docs/api.md`](docs/api.md) for the full WebSocket specification & client examples.

## Custom Detectors

Want to experiment with your own CV pipeline? Check out [`docs/detectors.md`](docs/detectors.md) for implementation guides and examples.

## Contributing

Pull requests are welcome — but please open an issue first so we can discuss design & approach. 🎯
