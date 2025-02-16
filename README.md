# QindieGL
## QindieGL Is Not Driver, It's Emulator

![QindieGL Logo](logo/QIndieGL-Logo-small.png?raw=true)

## What's special about this fork?

Since my modification of Return to Castle Wolfenstein open source to support RTX-remix, I was wondering if other idtech3 games could be enhanced with camera detection and stable geometry.

So far I have released a camera hack for idtech3: I noticed RTCW stores the camera and the modelview matrix (model/world * camera) in two globals;<br/>
I can detect the camera global (by comparing the pointers) and the model matrix can be obtained by multiplying modelview * camera-inverse.<br/>
We'll see if this is sufficient.<br/>
For idtech2 games, I've noticed the game builds up the camera matrix with gl rotate and translate, then saves it for later use;<br/>
I've used this (getvalue) as trigger to make it the camera matrix, and any later scale/rotate/translate would be considered model matrix.

A method for stabilising geometry hashes is in it's inception, stay tuned.

Tested with: American McGee's Alice, CoD 2003, Heavy Metal FAKK2, Star Trek Elite Force 2, Open Jedi Knight, Quake2, Heretic2.
NOTE: RTSS interferes with proper Remix functionality; add NvRemixBridge.exe to the exception list (OJK needs this to be able to launch).

## Included in this project:

- DXErr functions part of DXUT [https://github.com/Microsoft/DXUT] (MIT license)
- mINI library [https://github.com/metayeti/mINI] (MIT license)
- Detours library [https://github.com/microsoft/Detours] (MIT license)

## Introduction

QindieGL is a wrapper library which emulates OpenGL API using Microsoft Direct3D 9.0c. Emulation is not complete - some features are not implemented. Although the existing functionality allows to play some OpenGL-based games using the wrapper. There are several reasons to use QindieGL instead if the native OpenGL renderer:

1. Graphic enhancements are often available for Direct3D API only (e.g. SweetFX); so you may try the wrapper to enable advanced shader effects, stereoscopic vision, etc., in old OpenGL-based games.
2. Some drivers are glitchy (especially for mobile video cards) and lack for a proper OpenGL support, but Direct3D is supported. Then you can use the wrapper to play an OpenGL-based game.
3. People may want to implement multi-renderer in their engine, so it will be capable to render using either OpenGL, or Direct3D. Then this wrapper can be linked statically to such renderers, or loaded dynamically instead of a regular OpenGL system library. Please note that only simple OpenGL features are supported (up to version 1.4); this means there are no shaders, vertex buffer objects, etc. However all these features can be added, since where will be no need for ideal compatibility with already compiled binaries.

[![Quake II with QindieGL](https://cloud.githubusercontent.com/assets/20521208/20456521/d9c1e7ae-aeaa-11e6-99f7-d972bc49dbf0.jpg?raw=true)](https://cloud.githubusercontent.com/assets/20521208/20456526/d9dfa41a-aeaa-11e6-8d30-5973df9cf778.jpg) [![Quake III with QindieGL](https://cloud.githubusercontent.com/assets/20521208/20456522/d9c2621a-aeaa-11e6-8041-dd9af8798389.jpg?raw=true)](https://cloud.githubusercontent.com/assets/20521208/20456523/d9c49ab2-aeaa-11e6-8f9b-a33899f66c3c.jpg)

## Setup

Please perform the following steps to install and enable QindieGL:

1. Copy `QindieGL.ini` to the game directory. Modify in this file any settings particular to your game.
2. Place `opengl32.dll` to the directory where the executable file of the game is located.
3a. Most probably the game will not load the local `opengl32.dll` due to GPU driver app interference: Rename the executable e.g. quake2dx.exe
3. Run the game; make sure the `QindieGL.log` file is created; this means that game has successfully hooked the wrapper.
4. If you want to restore the native OpenGL renderer, delete the `opengl32.dll` file from the game executable's directory.

Please also read **Security Notice** before using QindieGL!

## Supported Games
- Quake 2 (in OpenGL mode)
- Kingpin
- Quake 3
- Return to Castle Wolfenstein
- Half-Life (in OpenGL mode)
- Serious Sam: First Encounter (light glare effects are not supported)
- Serious Sam: Second Encounter* (in OpenGL mode; there is also a D3D renderer)
- Doom 3 (only 2D works properly - menu, PDA, etc; 3D is glitchy but playable)
- GLQuake (Z-trick must be disabled, e.g. type `gl_ztrick 0` in the console)
- Tux Racer
- and maybe some others not tested; simply check!

[![Tux Racer with QindieGL](https://cloud.githubusercontent.com/assets/20521208/20456524/d9dafd16-aeaa-11e6-90d2-2d0dbee4e06a.jpg?raw=true)](https://cloud.githubusercontent.com/assets/20521208/20456525/d9db1940-aeaa-11e6-89c2-bef4e211cc3a.jpg)

## Feature Support
- Accumulator - no
- **Alpha-test - full**
- **Alpha-blend - full**
- **Immediate mode - full**
- **Texture objects - almost full** (glCopyTex(Sub)Image is not supported)
- Display lists - no
- **Culling - full**
- **Clip planes - full**
- **Lighting - partial** (two-side lighting model and spotlights are not supported)
- **Materials - full**
- **Vertex arrays - full**
- Evaluators - no
- **Fog - full**
- **Pixel unpacking - full**
- **Pixel packing - almost full** (rgb and rgba)
- **Point size - full**
- Line width - no
- Stipple - no
- **Stencil - full**
- Index mode - no
- Logic op - no
- **Matrices - full**
- **Attribute push/pop - full**
- **Texture priority - almost full** (residental state is assumed)
- **Read pixels - partial** (can't read depth and stencil)
- Copy/draw pixels - no
- ReadBuffer/DrawBuffer - no
- Selection - no
- Feedback - no
- **Scissor test - full**
- **Polygon mode and offset - partial** (front-and-back only)
- **Texture coordinate generation - full** (in software; optimized with SSE)
- Multiple contexts - no
- wglUseFontBitmaps/wglUseFontOutlines - no
- *Multisample Antialiasing - optional** ("MultiSample" parameter in the reg file; note that FSAA can mess up fonts in some games)

## Troubleshooting

Sometimes QindieGL won't be hooked by the game. There are several workarounds possible:
- First of all, make sure that this is an OpenGL-based game; ask developers or search the Web to be sure.
- NVIDIA drivers sometimes use profiles for the games; they pre-load system's OpenGL library before the game loads it, and the wrapper is ignored. To deal with this, try to rename the executable: e.g. rename `quake2.exe` to `quake2_.exe`, and use it to run the game.

Some screen resolutions available in OpenGL, are not available in Direct3D. Before using a non-standard resolution, make sure your display adapter lists it in the adapter's all modes list.

## Security Notice

Some anti-cheating software refers such wrappers to as cheats, so make sure your anti-cheating software is not active. I.e. don't forget to delete wrapper's `opengl32.dll` before playing multiplayer games: you may get a ban because of Valve Anti-Cheat, and it is nearly impossible to reverse. So, the rule of thumb is: **don't use QindieGL with multiplayer Steam games**.

**NEVER**, you hear, **NEVER** replace system's OpenGL library `opengl32.dll`! This can break the whole enchilada, since QindieGL is NOT a complete replacement for OpenGL.

*Crystice Softworks*
*2016*
