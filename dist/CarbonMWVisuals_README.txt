CarbonMWVisuals 1.0
===================

Need for Speed: Most Wanted daytime visuals for Need for Speed: Carbon.

- MW style world, road, car and sky shading and MW colour grading
- dynamic sun shadows (Carbon's own shadow map code, switched back on)
- MW time of day: the sun moves across the sky, morning / noon / evening lighting
- optional 24 hour cycle: MW day that fades into Carbon's original night look
- separate look and clock for the menus/garage, optional MW garage reflections
- in-game settings menu (F12)


REQUIREMENTS
------------
- NFS Carbon v1.4 (NFSC.exe). Other versions are detected and the mod then does nothing.
- An ASI loader, e.g. Ultimate ASI Loader. It is already installed if you use the
  NFS Carbon Widescreen Fix.
- Tested on NVIDIA (GeForce RTX) and Intel integrated graphics (UHD). AMD/ATI GPUs use a
  different shadow path that has not been tested yet; if shadows look wrong there, switch
  them off in the menu (Global tab).
- Integrated graphics: shadows at quality 4096 are heavy. Use 1024 or 2048
  (menu, Global tab -> Save -> restart the game) or switch them off.


INSTALLATION
------------
1. Install NFSC HD Reflections by Aero_ first (strongly recommended):
   https://nfsmods.xyz/mod/3363
2. Copy the contents of this archive into the NFS Carbon folder (the one with NFSC.exe):

     NFS Carbon\
       glassreflectshader.fx
       fx\car.fx, carnormalmap.fx, sky.fx, visualtreatment.fx,
          world.fx, worldnormalmap.fx, worldprelit.fx, worldreflect.fx
       scripts\CarbonMWVisuals.asi
       scripts\CarbonMWVisuals.ini
       scripts\CarbonMWVisuals_FEEnv\   (24 .dds files, MW garage reflections)

   No game file is replaced: Carbon itself uses .efx files, these are new .fx files.

3. Start the game. CarbonMWVisuals.log in the game folder shows what was loaded:
   every shader should be listed as [OK].


OTHER MODS
----------
- NFSC HD Reflections by Aero_ (https://nfsmods.xyz/mod/3363): strongly recommended together
  with this mod.
- PS3 Visual Treatment (https://nfsmods.xyz/mod/3473): works with this mod.
- Extended Customization by Archie: set lighttrail=0 in ExtendedCustomization.ini, otherwise
  the light trails also show up in daytime.


UNINSTALL
---------
Delete the files and the folder listed above, or set
[Main] Enabled=0 in scripts\CarbonMWVisuals.ini to switch the whole mod off.


IN-GAME MENU (F12)
------------------
Press F12 to show or hide the settings menu (use the mouse). Changes apply immediately.
"Save to .ini" keeps them for the next start.

- In-game:        time of day while driving (time, speed, mode, weather)
- Menus / garage: the same for the menus and the garage, plus the MW reflection cubemap if needed
- Global:         sync the time between in-game and the menus/garage, shadows on/off and
                  shadow quality (1024 / 2048 / 4096, used after restarting the game)
- Noon sun:       where the sun stands at 12:00

The key can be changed in the .ini ([Menu] Key). [Menu] Enable=0 removes the menu.


MAIN SETTINGS (scripts\CarbonMWVisuals.ini)
-------------------------------------------
Every setting is explained in the .ini itself. The most important ones:

[Main]   Enabled   1 = mod on, 0 = vanilla Carbon
         Mode      1 = MW day / Carbon night (24 hours), 0 = MW daytime only
         SyncTime  1 = in-game and the menus/garage share one time
[Shadows] Enable, Size (shadow quality), Strength, Range
[TimeOfDay] StartHour, HourLength (real seconds per in-game hour), StopClock, Overcast
            HeadlightsOn / HeadlightsOff: when the AI car headlight beams switch on and off
[Lights] world light glows and AI headlight beams in daytime
[FrontEnd] / [FrontEndTimeOfDay] / [FrontEndFx]: the look and clock of the menus/garage


MW GARAGE REFLECTIONS (optional)
--------------------------------
The car reflections in the menus/garage can use NFS Most Wanted's own garage cubemaps
(scripts\CarbonMWVisuals_FEEnv). They are off by default: switch them on in the menu
(Menus / garage tab: "MW reflection cubemap", optionally pick one of the four MW garages, Save)
or set [FrontEnd] MWEnvMap=1 in the .ini.

tools\extract_feenv.py (needs Python 3) re-creates the files from your own NFS Most Wanted:

  python tools\extract_feenv.py "C:\...\NFS Most Wanted\GLOBAL\DYNTEX.BIN" "C:\...\NFS Carbon\scripts\CarbonMWVisuals_FEEnv"

Without the files, Carbon's own reflections are used.


COMPATIBILITY
-------------
- Widescreen Fix, Ultimate ASI Loader: fine.
- Extended Customization: fine (see OTHER MODS for the light trails). Shaders without a
  CarbonMWVisuals .fx file are left to it.
- Other shader packs that load their own world, car, sky or visual treatment .fx files will
  conflict: use one or the other.


TROUBLESHOOTING
---------------
- Nothing changes: check CarbonMWVisuals.log. "not NFSC.exe v1.4" means a different exe
  version; no log at all means the ASI loader is missing.
- A shader shows [FAILED] in the log: the original Carbon shader is used for it and the error
  is written below it. Please include the log when you report it.
- Dark black or missing shadows: try [Shadows] Format=1 or Format=2, or switch shadows off in the menu.
- Low frame rate: lower the shadow quality (Global tab, takes effect after a restart) or switch
  shadows off.
- For bug reports, always attach CarbonMWVisuals.log and your CarbonMWVisuals.ini.


CREDITS
-------
- Dear ImGui (menu): Omar Cornut and contributors, MIT license (see licenses\imgui_LICENSE.txt).
- The MW garage reflection cubemaps are taken from NFS Most Wanted.
- NFS Most Wanted and NFS Carbon are (c) Electronic Arts. This is a fan-made mod, not
  affiliated with EA.
