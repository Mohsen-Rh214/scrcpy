# scrcpy (frameless + multilingual keyboard fork)

> [!IMPORTANT]
> **This is a fork of [scrcpy](https://github.com/genymobile/scrcpy)** by Genymobile,
> not the official project. For downloads, documentation, and releases of the
> original tool, see the [upstream repository](https://github.com/genymobile/scrcpy).
>
> This fork exists for two reasons:
>
> 1. **Keyboard input for non-Latin languages** — type Farsi, Arabic, CJK, etc.
>    from your physical keyboard (upstream could only reliably inject Latin text).
> 2. **A frameless window** — no title bar or borders by default, with rounded
>    corners, movable by **right-click dragging**.

[scrcpy](https://github.com/genymobile/scrcpy) mirrors Android devices (video and
audio) connected via USB or TCP/IP and allows control using the computer's
keyboard and mouse, without root access or any app installed on the device.
Everything described below applies to this fork only.

## What's different from upstream?

The codebase is **kept exactly as upstream**: same language (C for the client,
Java for the on-device server), same frameworks and build system (SDL3, FFmpeg,
Meson). No rewrite, no new dependencies beyond what scrcpy already uses — the
changes are small, targeted patches plus a few new Windows-only files.

### 1. Multilingual keyboard input

Upstream injects typed characters as text events, but Android's text-injection
path silently drops most non-ASCII characters (only Latin works reliably).
This fork detects non-ASCII input and commits it through the device clipboard
followed by an automatic paste instead, so arbitrary Unicode text lands in the
focused field. Text mode is now also the default injection mode for the SDK
keyboard (`--keyboard=sdk`).

### 2. Frameless window (Windows)

The mirror window opens without title bar or borders by default, with rounded
corners where the OS supports them (native on Windows 11, clipped-region
fallback on Windows 10). Pass `--no-window-borderless` to restore the classic
decorated window.

### 3. Move the window with right-click drag

Since there is no title bar anymore, **right-click + drag** moves the window.
A plain right-click (no movement) still performs scrcpy's default right-click
action (BACK), so nothing is lost.

## Building

Build like upstream scrcpy (Meson + Ninja); see the
[upstream documentation](https://github.com/genymobile/scrcpy#build-the-client).
This repository adds nothing to the build prerequisites.

## Credits & original project

All credit for scrcpy goes to **Genymobile** and Romain Vimont and the
contributors of the original project: **<https://github.com/genymobile/scrcpy>**.
This fork only layers the small changes above on top of their work — please
support and refer to the original for everything else.
