<img align="left" style="width:260px" src="https:
**raylib is a simple and easy-to-use library to enjoy videogames programming.**

raylib is highly inspired by Borland BGI graphics lib and by XNA framework and it's especially well suited for prototyping, tooling, graphical applications, embedded systems and education.

*NOTE for ADVENTURERS: raylib is a programming library to enjoy videogames programming; no fancy interface, no visual helpers, no debug button... just coding in the most pure spartan-programmers way.*

Ready to learn? Jump to [code examples!](https:
---

<br>

[![GitHub Releases Downloads](https:[![GitHub Stars](https:[![GitHub commits since tagged version](https:[![GitHub Sponsors](https:[![Packaging Status](https:[![License](https:
[![Discord Members](https:[![Reddit Static Badge](https:[![Youtube Subscribers](https:[![Twitch Status](https:
[![Build Windows](https:[![Build Linux](https:[![Build macOS](https:[![Build WebAssembly](https:
[![Build CMake](https:[![Build examples Windows](https:[![Build examples Linux](https:
features
--------
  - **NO external dependencies**, all required libraries are [included into raylib](https:  - Multiple platforms supported: **Windows, Linux, MacOS, RPI, Android, HTML5... and more!**
  - Written in plain C code (C99) using PascalCase/camelCase notation
  - Hardware accelerated with OpenGL: **1.1, 2.1, 3.3, 4.3, ES 2.0, ES 3.0**
  - **Unique OpenGL abstraction layer** (usable as standalone module): [rlgl](https:  - **Software Renderer** backend (no OpenGL required!): [rlsw](https:  - Multiple **Fonts** formats supported (TTF, OTF, FNT, BDF, sprite fonts)
  - Multiple texture formats supported, including **compressed formats** (DXT, ETC, ASTC)
  - **Full 3D support**, including 3D Shapes, Models, Billboards, Heightmaps and more!
  - Flexible Materials system, supporting classic maps and **PBR maps**
  - **Animated 3D models** supported (skeletal bones animation) (IQM, M3D, glTF)
  - Shaders support, including model shaders and **postprocessing** shaders
  - **Powerful math module** for Vector, Matrix and Quaternion operations: [raymath](https:  - Audio loading and playing with streaming support (WAV, QOA, OGG, MP3, FLAC, XM, MOD)
  - **VR stereo rendering** support with configurable HMD device parameters
  - Huge examples collection with [+140 code examples](https:  - Bindings to [+70 programming languages](https:  - **Free and open source**

basic example
--------------
This is a basic raylib example, it creates a window and draws the text `"Congrats! You created your first window!"` in the middle of the screen. Check this example [running live on web here](https:```c
#include "raylib.h"

int main(void)
{
    InitWindow(800, 450, "raylib example - basic window");

    while (!WindowShouldClose())
    {
        BeginDrawing();
            ClearBackground(RAYWHITE);
            DrawText("Congrats! You created your first window!", 190, 200, 20, LIGHTGRAY);
        EndDrawing();
    }

    CloseWindow();

    return 0;
}
```

build and installation
----------------------

raylib binary releases for Windows, Linux, macOS, Android and HTML5 are available at the [Github Releases page](https:
raylib is also available via multiple package managers on multiple OS distributions.

#### Installing and building raylib on multiple platforms

[raylib Wiki](https:
 - [Working on Windows](https: - [Working on macOS](https: - [Working on GNU Linux](https: - [Working on Chrome OS](https: - [Working on FreeBSD](https: - [Working on Raspberry Pi](https: - [Working for Android](https: - [Working for Web (HTML5)](https: - [Working anywhere with CMake](https:
*Note that the Wiki is open for edit, if you find some issues while building raylib for your target platform, feel free to edit the Wiki or open an issue related to it.*

#### Setup raylib with multiple IDEs

raylib has been developed on Windows platform using [Notepad++](https:
[Projects directory](https:
*Note that there are lots of IDEs supported, some of the provided templates could require some review, so please, if you find some issue with a template or you think they could be improved, feel free to send a PR or open a related issue.*

learning and docs
------------------

raylib is designed to be learned using [the examples](https:
Some additional documentation about raylib design can be found in [raylib GitHub Wiki](https:
 - [raylib cheatsheet](https: - [raylib architecture](https: - [raylib library design](https: - [raylib examples collection](https: - [raylib games collection](https:

contact and networks
---------------------

raylib is present in several networks and raylib community is growing everyday. If you are using raylib and enjoying it, feel free to join us in any of these networks. The most active network is our [Discord server](https:
 - Webpage: [https: - Discord: [https: - X: [https: - BlueSky: [https: - Twitch:  [https: - Reddit:  [https: - Patreon: [https: - YouTube: [https:
contributors
------------

<a href="https:  <img src="https:</a>

license
-------

raylib is licensed under an unmodified zlib/libpng license, which is an OSI-certified, BSD-like license that allows static linking with closed source software. Check [LICENSE](LICENSE) for further details.

raylib uses internally some libraries for window/graphics/inputs management and also to support different file formats loading, all those libraries are embedded with and are available in [src/external](https: