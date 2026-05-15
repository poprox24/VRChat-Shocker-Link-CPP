# ShockerLink Build Instructions

This document explains how to build **ShockerLink** from source on **Windows** and **Linux**.

> **Important:** ShockerLink supports **native builds only**. Build on the target platform itself. Cross-compilation is not supported.

## Prerequisites

### Windows

Install the following:

* **Visual Studio 2022** (Community is fine) with these workloads:

  * Desktop development with C++
  * C++ CMake tools for Windows
* **Git**

### Linux

Install the required build tools and libraries for your distribution.

#### Ubuntu / Debian / Pop!_OS

```sh
sudo apt update
sudo apt install build-essential cmake ninja-build git \
    libglfw3-dev libgl1-mesa-dev libx11-dev libxrandr-dev libxi-dev \
    libxcursor-dev libxinerama-dev libxxf86vm-dev libpng-dev \
    libcurl4-openssl-dev
```

#### Arch Linux / Manjaro / EndeavourOS

```sh
sudo pacman -S base-devel cmake ninja git glfw-x11 \
    libx11 libxrandr libxi libxcursor libxinerama libxxf86vm \
    libpng curl
```

#### Fedora

```sh
sudo dnf install cmake ninja-build gcc-c++ git glfw-devel \
    libX11-devel libXrandr-devel libXi-devel libXcursor-devel \
    libXinerama-devel libXxf86vm-devel libpng-devel libcurl-devel
```

## Building

### 1. Clone the repository

```sh
git clone https://github.com/poprox24/VRChat-Shocker-Link-CPP.git
cd VRChat-Shocker-Link-CPP
```

### 2. Configure and build

#### Windows (Visual Studio / MSVC)

**Recommended:** open the repository folder in **Visual Studio 2022**.
Visual Studio should detect `CMakeLists.txt` automatically.

1. Select **Release** at the top.
2. Build with **Ctrl + Shift + B**.

Executable output:

```text
build\Release\Shocker_Link.exe
```

**Command-line alternative:**

```cmd
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

#### Linux (Clang recommended)

Configure with Clang and Ninja:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

#### Linux (GCC alternative)

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Executable output:

```text
./build/Shocker_Link
```

## Running

### Windows

```text
build\Release\Shocker_Link.exe
```

### Linux

```sh
./build/Shocker_Link
```

## Troubleshooting

* **First build is slow** - Normal. CMake needs to download and build dependencies such as ImGui, fmt, yaml-cpp, and others.
* **Windows: CURL not found** - Make sure the required Visual Studio C++ components are installed.
* **Linux: missing libraries** - Recheck the package list for your distribution.
* **Clean rebuild** - Delete the `build` directory:

```sh
rm -rf build
```

On Windows, delete the `build` folder manually.
