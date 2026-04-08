<a id="readme-top"></a>
[![Contributors][contributors-shield]][contributors-url]
[![Forks][forks-shield]][forks-url]
[![Stargazers][stars-shield]][stars-url]
[![Issues][issues-shield]][issues-url]

<br />
<div align="center">
  <a href="https://github.com/poprox24/VRChat-Shocker-Link-CPP">
    <img width="1077" height="523" alt="ShockerLink UI" src="https://github.com/user-attachments/assets/67277c49-8b2f-4111-92b1-96d2e794c7e4" />
  </a>

  <h3 align="center">ShockerLink</h3>

  <p align="center">
    Connects a VRChat avatar parameter to a PiShock or OpenShock device.<br />
    Supports serial and API modes, a bezier intensity curve, cooldown system, chatbox output, and VR notifications.
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
- [Roadmap](#roadmap)
- [Contributing](#contributing)
- [License](#license)
- [Contact](#contact)

---

## Getting Started

### Prerequisites

- [.NET Framework](https://dotnet.microsoft.com/en-us/download/dotnet-framework)

### Installation

1. Go to [Releases](https://github.com/poprox24/VRChat-Shocker-Link-CPP/releases/latest) and download `Shocker_Link.exe`
2. Place it in any folder and run it
3. Open Settings (bottom left)
4. Set your `Shock Parameter` to match the parameter name on your VRChat avatar
5. Choose your connection mode: `PiShock` or `OpenShock`
   - Serial (USB hub): connect the hub and let the app scan, or enter the COM port manually
   - API: enter your credentials in Settings
   - OpenShock serial users: add your shocker IDs manually in Settings, they cannot be auto-detected over serial
6. Click `Save` or `Save and Restart` if prompted

### Usage

- The intensity curve controls the probability distribution of shock strength.
- Presets store the curve, duration range, and view state. Left-click to load, right-click to rename, middle-click to reset to default.
- All inputs support undo/redo with `Ctrl+Z` / `Ctrl+Y`.
- A second OSC parameter can optionally trigger shocks biased toward the upper half of the intensity curve.
- The cooldown system dynamically increases the wait time between shocks based on recent trigger frequency.
- The panic hotkey (default `F9`) disables shocks immediately.

---


## Features

- PiShock and OpenShock support via serial or API
- Bezier intensity curve with weighted random sampling
- Dynamic cooldown system
- Presets with per-preset curve and view state
- VRChat chatbox output and VR notifications (XSOverlay / OVRToolkit)
- Stats tracking: shock count, duration, intensity, and daily history

---

## Roadmap

- [x] Undo/redo support
- [x] Auto-updater
- [x] Stats menu
- [x] PiShock and OpenShock API support
- [x] Linux support (tested on Arch, built with XWayland) 

See [open issues](https://github.com/poprox24/VRChat-Shocker-Link-CPP/issues) for proposed features and known issues.

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

<p align="right">(<a href="#readme-top">back to top</a>)</p>

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
