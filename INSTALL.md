# Installing Amrvis2

Two options: build from source, or download a prebuilt AppImage.
Tested on Ubuntu 24.04 and macOS.

## Option 1: Build from source

### Linux (Ubuntu)

Install the dependencies:

```bash
sudo apt install g++ cmake ninja-build qt6-base-dev
```

### macOS

Install the dependencies with [Homebrew][]:

```bash
brew install cmake ninja qt6
```

[Homebrew]: https://brew.sh

**Optional:** `ffmpeg` is needed to encode animation exports (MP4).

```bash
# Ubuntu
sudo apt install ffmpeg
# macOS
brew install ffmpeg
```

Build (both platforms):

```bash
git clone https://github.com/WeiqunZhang/Amrvis2.git
cd Amrvis2
cmake --preset default
cmake --build --preset default
```

The executable is `build/src/qt/amrvis2`. Run it with a plotfile:

```bash
./build/src/qt/amrvis2 /path/to/plotfile
```

Or install system-wide:

```bash
sudo cmake --install build --prefix /usr/local
```

## Option 2: Prebuilt AppImage

> **Not yet available.** Prebuilt AppImages will be published on the
> [GitHub Releases][] page once the first release is tagged.

An AppImage is a self-contained executable that runs on any modern Linux
distribution without installing packages. When available:

1. Download `Amrvis2-*.AppImage` from the [GitHub Releases][] page.
2. Make it executable: `chmod +x Amrvis2-*.AppImage`
3. Run it: `./Amrvis2-*.AppImage /path/to/plotfile`

[GitHub Releases]: https://github.com/WeiqunZhang/Amrvis2/releases
