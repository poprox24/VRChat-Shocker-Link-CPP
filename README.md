<a id="readme-top"></a>
[![Contributors][contributors-shield]][contributors-url]
[![Forks][forks-shield]][forks-url]
[![Stargazers][stars-shield]][stars-url]
[![Issues][issues-shield]][issues-url]

<br />
<div align="center">
  <a href="https://private-user-images.githubusercontent.com/48130451/591418965-b465d8db-b01a-450f-acfc-ab1f20126a9a.png?jwt=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJnaXRodWIuY29tIiwiYXVkIjoicmF3LmdpdGh1YnVzZXJjb250ZW50LmNvbSIsImtleSI6ImtleTUiLCJleHAiOjE3ODAyNjgzNTYsIm5iZiI6MTc4MDI2ODA1NiwicGF0aCI6Ii80ODEzMDQ1MS81OTE0MTg5NjUtYjQ2NWQ4ZGItYjAxYS00NTBmLWFjZmMtYWIxZjIwMTI2YTlhLnBuZz9YLUFtei1BbGdvcml0aG09QVdTNC1ITUFDLVNIQTI1NiZYLUFtei1DcmVkZW50aWFsPUFLSUFWQ09EWUxTQTUzUFFLNFpBJTJGMjAyNjA1MzElMkZ1cy1lYXN0LTElMkZzMyUyRmF3czRfcmVxdWVzdCZYLUFtei1EYXRlPTIwMjYwNTMxVDIyNTQxNlomWC1BbXotRXhwaXJlcz0zMDAmWC1BbXotU2lnbmF0dXJlPTcyOGNhMzZkM2Y5ZTFiODczOTNkMGFhNjlmNTY2MDc3YmY1ZDJkNDZlMmViNDY2MzkzYzY4NjhiOTUzNTZjYTYmWC1BbXotU2lnbmVkSGVhZGVycz1ob3N0JnJlc3BvbnNlLWNvbnRlbnQtdHlwZT1pbWFnZSUyRnBuZyJ9.LRQ5Ugor99l7QXOxLrgYntDDC41vMwwsjd6vLU_pwHA">
    <img width="1734" height="695" alt="image" src="https://github.com/user-attachments/assets/b465d8db-b01a-450f-acfc-ab1f20126a9a" />
  </a>

  <h3 align="center">ShockerLink</h3>

  <p align="center">
    Connects VRChat avatar parameters to PiShock or OpenShock devices.<br />
    Supports serial and API modes, bezier intensity curves, a dynamic cooldown system, chatbox output, VR notifications, and more.
    <br /><br />
    <a href="https://github.com/poprox24/VRChat-Shocker-Link-CPP/issues/new?labels=bug">Report Bug or Request Feature</a>
  </p>

https://github.com/user-attachments/assets/beff6062-4739-47cd-b56c-7f491de81a68

</div>

---

## Table of Contents

- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Installation](#installation)
  - [Usage](#usage)
- [Features](#features)
- [Visual Parameters](#visual-parameters)
- [Building](#building-from-source)
- [Roadmap](#roadmap)
- [Contributing](#contributing)
- [License](#license)
- [Contact](#contact)

---

## Getting Started

### Prerequisites

- [.NET Framework](https://dotnet.microsoft.com/en-us/download/dotnet-framework)

### Installation

1. Go to [Releases](https://github.com/poprox24/VRChat-Shocker-Link-CPP/releases/latest) and download `Shocker_Link.exe` (`Shocker_Link` on Linux)
2. Place it in any folder and run it
3. Open Settings (bottom left)
4. Add a parameter under **OSC / Avatar** and set its name to match a parameter on your VRChat avatar
5. Choose your connection mode: `PiShock` or `OpenShock`, then `Serial` or `API`
   - Serial (USB hub): connect the hub and let the app scan, or enter the port manually
   - API: enter your credentials in Settings
   - OpenShock serial users: add shocker IDs manually - they can't be auto-detected over serial
6. Click `Save` or `Save and Restart` if prompted

### Usage

- The **intensity curve** controls the probability distribution of shock strength.
- **Curves** are named tabs above the plot. You can add, rename, clone, delete, or copy curves from saved presets via right-click.
- **Presets** store a full set of curves. Left-click to load, right-click to rename, middle-click to set as default, click the floppy icon to save.
- `Ctrl+S` to save the active preset quickly.
- All inputs support undo/redo with `Ctrl+Z` / `Ctrl+Y`.

- Multiple **OSC parameters** can be configured, each with its own curve, intensity range, shocker list, and shocker selection order.
- The **cooldown system** dynamically increases wait time between shocks based on recent trigger frequency.
- The **panic hotkey** (default `F9`) disables shocks immediately from anywhere, even when the window isn't focused.

---

## Features

- PiShock and OpenShock support via serial or API
- Bezier intensity curve with weighted random sampling
- Dynamic cooldown system
- Presets with per-preset curve and view state
- VRChat chatbox output and VR notifications (XSOverlay / OVRToolkit / WayVR)
- Stats tracking: shock count, duration, intensity, and daily history
- Visual avatar parameters for animator integration

---

## Visual Parameters

ShockerLink automatically sends three float parameters to your avatar after every shock. Add them to your avatar's animator and params file to drive animations, particle effects, or anything else.

| Parameter | Type | Range | Description |
|---|---|---|---|
| `ShockerLink_IntensityPercentage` | Float | 0–1 | How strong the shock was (0 = 0%, 1 = 100%) |
| `ShockerLink_CooldownPercentage` | Float | 0–1 | Starts at 1 right after a shock, drains to 0 as the cooldown expires |
| `ShockerLink_DurationSeconds` | Float | 0–1 | Normalized shock duration (1 = 10 seconds, the software maximum) |

`CooldownPercentage` updates at ~10Hz while the cooldown is active, so it can drive a smooth progress bar or blend tree on your avatar.

---

## Building from Source

See **[BUILD.md](BUILD.md)** for detailed instructions.

---

## Contributing

Fork the repo and open a pull request, or file an issue tagged `enhancement`.

1. Fork the project
2. Create a feature branch (`git checkout -b feature/MyFeature`)
3. Commit your changes (`git commit -m 'Add MyFeature'`)
4. Push to the branch (`git push origin feature/MyFeature`)
5. Open a pull request

### Contributors

<a href="https://github.com/poprox24/VRChat-Shocker-Link-CPP/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=poprox24/VRChat-Shocker-Link-CPP" alt="Contributors" />
</a>

---

## License

Distributed under the DWYW License. See `LICENSE` for details.

---

## Contact

Poprox24 - [@poprox422](https://twitter.com/poprox422) - poprox24.roxy@gmail.com

Project: [VRChat-Shocker-Link-CPP](https://github.com/poprox24/VRChat-Shocker-Link-CPP)

<p align="right">(<a href="#readme-top">Back to top</a>)</p>

[contributors-shield]: https://img.shields.io/github/contributors/poprox24/VRChat-Shocker-Link-CPP.svg?style=for-the-badge
[contributors-url]: https://github.com/poprox24/VRChat-Shocker-Link-CPP/graphs/contributors
[forks-shield]: https://img.shields.io/github/forks/poprox24/VRChat-Shocker-Link-CPP.svg?style=for-the-badge
[forks-url]: https://github.com/poprox24/VRChat-Shocker-Link-CPP/network/members
[stars-shield]: https://img.shields.io/github/stars/poprox24/VRChat-Shocker-Link-CPP.svg?style=for-the-badge
[stars-url]: https://github.com/poprox24/VRChat-Shocker-Link-CPP/stargazers
[issues-shield]: https://img.shields.io/github/issues/poprox24/VRChat-Shocker-Link-CPP.svg?style=for-the-badge
[issues-url]: https://github.com/poprox24/VRChat-Shocker-Link-CPP/issues
[license-shield]: https://img.shields.io/github/license/poprox24/VRChat-Shocker-Link-CPP.svg?style=for-the-badge
[license-url]: https://github.com/poprox24/VRChat-Shocker-Link-CPP/blob/master/LICENSE
