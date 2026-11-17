<img width="1672" height="941" alt="Neon Geist_ Recompiled PC Port" src="https://github.com/user-attachments/assets/c5321b54-3f21-4d28-ae8d-fe02e7c839ec" />



## GEIST RECOMPILED  v1.0.7
![Geist: Recompiled PC Port](https://github.com/user-attachments/assets/c5321b54-3f21-4d28-ae8d-fe02e7c839ec)

# GEIST: RECOMPILED
### Version 1.0.7 | Native Windows PC Port

**Experience the GameCube cult classic Geist on PC!**

Originally released for the Nintendo GameCube in 2005, *Geist* is a unique first-person action-adventure game that combines shooting, exploration, puzzles, and supernatural possession.

**Geist: Recompiled** is an unofficial, fan-made PC port designed to bring the original experience to modern Windows computers with improved display options, customizable controls, modern controller support, and a dedicated launcher.

This project aims to preserve the original gameplay while making *Geist* more accessible and enjoyable on modern PC hardware.

---

## 🎮 FEATURES

### Native PC Experience
- Play Geist on Windows 10 and Windows 11.
- Native PC executable generated from your own game disc.
- No traditional GameCube emulator required during gameplay.
- Dedicated launcher for game setup and configuration.
- Automatic game compilation during first-time setup.
- Faster startup after the initial build.

### Enhanced Graphics & Display
- Windowed, borderless fullscreen, and exclusive fullscreen modes.
- Multiple monitor support.
- Adjustable fullscreen and window resolutions.
- Internal rendering resolution up to 8× the original GameCube resolution.
- Original 4:3 aspect ratio support.
- Optional stretched widescreen display.
- Smooth or sharp image scaling.
- Vertical synchronization (VSync).
- Optional FPS counter.

### Modern Controls
- Keyboard and mouse support.
- Xbox controllers.
- PlayStation DualSense and DualShock 4 controllers.
- Nintendo Switch Pro Controllers and Joy-Con.
- GameCube controllers through compatible adapters.
- Customizable button mappings.

### Easy-to-Use Launcher
- Simple PLAY button to start the game.
- Select or drag and drop your game disc image.
- Automatic disc verification.
- First-time compilation handled through the launcher.
- Built-in access to controls and game folders.
- Build logs for troubleshooting.
- Custom launcher appearance.

---

## 💻 SYSTEM REQUIREMENTS

| Component | Requirement |
|---|---|
| Operating System | Windows 10 / Windows 11 (64-bit) |
| Graphics | OpenGL 3.3 compatible GPU |
| NVIDIA | GeForce GTX / RTX series |
| AMD | Radeon HD 5000 series or newer |
| Intel | Intel HD Graphics 4000 or newer |
| Storage | Approximately 2 GB free |
| Game | Legally obtained Geist (USA) disc dump |
| Disc ID | GITE01 |
| Revision | Revision 0 |

**Note:** Actual performance depends on your hardware. The graphics cards listed above are compatibility examples, not guaranteed performance targets.

---

## 📀 REQUIRED GAME FILES

**IMPORTANT: GEIST: RECOMPILED DOES NOT INCLUDE THE ORIGINAL GAME.**

You must provide your own legally dumped copy of *Geist* for Nintendo GameCube.

Supported formats:
- `.iso`
- `.gcm`
- `.ciso`

The required game version is:

**Geist (USA) — GITE01 — Revision 0**

### Using RVZ, GCZ, or NKit Files?

If your game is stored in `.rvz`, `.gcz`, or an NKit format, you must first convert it to a standard ISO using Dolphin Emulator.

1. Open Dolphin Emulator.
2. Locate Geist in your game library.
3. Right-click the game.
4. Select **Convert File**.
5. Choose the ISO format.
6. Save the converted file.
7. Select the ISO in Geist: Recompiled.

Your original game image is read during setup and is not modified.

---

## 🚀 INSTALLATION & FIRST-TIME SETUP

Getting started is simple!

**Step 1 — Extract the Download**

Extract the complete Geist: Recompiled folder to a location of your choice.

Example:

`C:\Games\GeistRecompiled`

Keep all included folders and files together.

**Step 2 — Open the Launcher**

Double-click:

`GeistLauncher.exe`

**Step 3 — Select Your Game**

Click **PLAY** and select your legally dumped Geist disc image.

You can also drag and drop your ISO onto the launcher window.

**Step 4 — First-Time Game Setup**

The launcher will automatically:

- Verify your game image.
- Prepare the game's original program.
- Recompile the program for Windows.
- Compile and generate your playable `Geist.exe`.

This process may take several minutes depending on your CPU.

**Step 5 — Start Playing!**

Once setup is complete, your game is ready.

Future launches will use the generated executable, so you won't need to repeat the initial compilation unless a rebuild is required.

**No manual programming or compiler setup is required.** The necessary build tools are included in the download.

---

## ⚙️ PC GRAPHICS SETTINGS

While playing, press **F1** to open the PC settings menu.

Controller users can open the menu using the Home, Guide, or PlayStation button, or by pressing both analog sticks simultaneously.

The settings menu opens over the running game. Pause your game first if necessary.

### Display Settings

| Setting | Description |
|---|---|
| Display Mode | Windowed, Borderless, or Exclusive Fullscreen |
| Monitor | Choose your preferred display |
| Fullscreen Resolution | Select a supported monitor resolution |
| Window Size | Choose from 640×480 up to 2880×2160 |
| Internal Resolution | Auto or 1× through 8× |
| Aspect Ratio | Original 4:3 or Stretch to Fill |
| Scaling Filter | Smooth or Sharp |
| VSync | Enable or disable vertical synchronization |
| Show FPS | Display the current frame rate |
| Quit Game | Exit the game |

### Internal Resolution Options

Increase the internal rendering resolution for a sharper image.

| Scale | Rendering Resolution |
|---|---|
| 1× | 640×480 (Original) |
| 2× | 1280×960 |
| 3× | 1920×1440 |
| 4× | 2560×1920 |
| 6× | 3840×2880 |
| 8× | 5120×3840 |

**Recommended:** Start with Auto and adjust based on your computer's performance.

### Aspect Ratio

**Original 4:3 (Default)**

Preserves the original GameCube presentation without stretching the image. Black bars may appear on widescreen monitors.

**Stretch to Fill**

Expands the picture to fill your display. This may distort the original image proportions.

All graphics preferences are saved automatically to:

`game\settings.ini`

---

## 🕹️ CONTROLS

Geist: Recompiled supports modern PC input devices.

### Keyboard & Mouse

- Mouse: Look around.
- Left-click inside the game window: Capture the mouse.
- F2: Release the mouse.
- Esc: Start / Pause.
- F1: Open PC settings.
- Alt+F4: Exit the game.

### Controller Support

Supported controller types include Xbox, PlayStation, Nintendo Switch, and compatible GameCube controllers.

Controller mappings can be customized through:

`controls.txt`

To edit your controls, open the launcher and select **Controls**.

The file includes a list of available buttons and their configurable mappings.

---

## 🛠️ LAUNCHER OPTIONS

The Geist Launcher provides several tools to help manage your installation.

**Game Disc**

Choose or change the original Geist game image.

**Rebuild Geist.exe**

Rebuild your playable executable when necessary, such as after an update that requires recompilation.

**Controls**

Open the control configuration file to customize your input settings.

**Game Folder**

Access the location where your playable game executable is generated.

**Build Log**

View compilation details and error messages if the initial setup fails.

---

## 📁 GAME FILES & FOLDERS

The download includes everything needed to prepare the PC port, except the original game.

| File / Folder | Purpose |
|---|---|
| `GeistLauncher.exe` | Main game launcher |
| `sdk\` | Recompiler and runtime components |
| `toolchain\` | Included compilation tools |
| `assets\` | Launcher graphics, fonts, and themes |
| `licenses\` | Open-source licenses |
| `work\` | Generated compilation files |
| `game\` | Generated game executable and settings |

The `work` and `game` folders are created during setup.

**Please do not redistribute generated game files.** These are produced using your personal game disc image.

Only share the original, unmodified Geist: Recompiled distribution.

---

## ❓ TROUBLESHOOTING

### The launcher doesn't recognize my game.

Make sure your disc image is the correct version:

**Geist (USA) — GITE01 — Revision 0**

Other regions and revisions are not currently listed as supported.

### My game is in RVZ or GCZ format.

Convert it to a standard ISO using Dolphin Emulator before selecting it in the launcher.

### The first launch is taking a long time.

The initial setup recompiles and builds the game for your PC.

This may take several minutes. Computers with more CPU cores may finish faster.

### My game isn't displaying correctly.

Open the PC settings menu with F1 and try:

- Setting Internal Resolution to Auto.
- Switching between Windowed and Borderless Fullscreen.
- Using the original 4:3 aspect ratio.
- Enabling or disabling VSync.

### My controller isn't working correctly.

Open `controls.txt` through the launcher's Controls option and review your button mappings.

### The game won't build.

Open the **Build Log** from the launcher and review the reported error.

When requesting help, include your Windows version, game image format, and the relevant error message.

---

## 📋 VERSION 1.0.7

**Current Release: v1.0.7**

This release provides the native PC launcher, first-time game compilation, customizable controls, and PC graphics configuration described above.

Future updates may introduce additional improvements, compatibility fixes, and refinements.

For detailed changes, see the project's GitHub Releases page.

---

## 👨‍💻 CREDITS & ACKNOWLEDGMENTS

### Project Development

**ThyHeroOfTime**

Geist: Recompiled — PC port development, launcher, and online multiplayer work.

### Recompilation Technology

**Ethan Nicholas**

Creator of `gcn-recomp` (MIT License).

### Open-Source Technologies

Special thanks to the developers and contributors behind:

- SDL2 — zlib License
- LLVM / llvm-mingw — Apache 2.0 with LLVM Exception
- Python — Python Software Foundation License
- stb — Public Domain / MIT
- Orbitron & Rajdhani Fonts — SIL Open Font License

Additional licensing information is available in the included `licenses` folder.

---

## ⚖️ LEGAL DISCLAIMER

**Geist: Recompiled is an unofficial, fan-made project.**

Geist was originally developed by **n-Space** and published by **Nintendo** for the Nintendo GameCube in 2005.

Geist and all associated characters, trademarks, artwork, music, and original game content belong to their respective owners.

This project is not affiliated with, sponsored by, approved by, or endorsed by Nintendo or n-Space.

**No copyrighted game files, disc images, or original game assets are included in this distribution.**

Users must supply their own legally obtained and dumped copy of the supported game.

---

### 👻 Bring Geist Back to Life on PC

**The original GameCube experience. Modern PC flexibility. Powered by recompilation.**

*Geist: Recompiled — A fan-made project by ThyHeroOfTime.*
