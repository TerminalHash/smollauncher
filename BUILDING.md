## Preparations
### Linux
Requirements is next::
- Qt 6.7+ with modules Сore, Gui, Widgets, Svg, SQL;
- toml++ (only for building).

Arch-based:
`sudo pacman -Sy tomlplusplus qt6-base qt6-svg qt6-tools`

Deb-based:
`sudo apt install qt6-base-dev qt6-svg-dev qt6-sql-dev libtomlplusplus-dev`

Fedora:
`sudo dnf install qt6-qtbase-devel qt6-qtsvg-devel qt6-qtbase-private-devel tomlplusplus`

OpenSUSE:
`sudo zypper install qt6-base-devel qt6-svg-devel qt6-sql-devel libtomlplusplus3 tomlplusplus-devel`

Gentoo:
`sudo emerge --ask dev-qt/qtbase dev-qt/qtsvg dev-cpp/tomlplusplus`

NixOS:
`nix-shell -p qt6.full tomlplusplus`


### Windows
SmolLauncher is possible to build for Windows.

Install Qt 6 via official toolkit installer, toml++ can be installed via vcpkg:
`vcpkg install tomlplusplus`

Don't forget to adapt SmolLauncher.pro to build.

### MacOS
SmolLauncher is possible to build for MacOS.

Requirements can be installed via Homebrew:
`brew install qt@6 tomlplusplus`

Don't forget to adapt SmolLauncher.pro and PATH to build.

### Building
Linux:
```sh
mkdir build
cd build
qmake6 ../SmolLauncher.pro
make -j"$(nproc)"
```

MacOS:
```sh
mkdir build
cd build
qmake ../SmolLauncher.pro
make -j$(sysctl -n hw.ncpu)
```

Windows (Для MSVC):
```sh
mkdir build
cd build
qmake ../SmolLauncher.pro
nmake release
```

Windows (Для MinGW):
```sh
mkdir build
cd build
qmake ../SmolLauncher.pro
mingw32-make release
```