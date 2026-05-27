L4D2 NVDA Accessibility Mod
============================
Makes Left 4 Dead 2 menus and in-game events accessible via the NVDA screen reader.
No external companion process required — everything runs inside the game.


REQUIREMENTS
------------
1. NVDA screen reader (must be running while playing)
   https://www.nvaccess.org/download/

2. SourceMod 1.12 or later installed for L4D2
   https://www.sourcemod.net/downloads.php
   (Choose the stable build for Left 4 Dead 2, then follow the SourceMod install guide)

3. Left 4 Dead 2 launched with the -insecure flag (see below)


INSTALLATION
------------
1. Install SourceMod into L4D2 if you have not already.

2. Copy the "left4dead2" folder from this zip into your L4D2 game directory,
   merging with the existing folder. The default path is:

     C:\Program Files (x86)\Steam\steamapps\common\Left 4 Dead 2\left4dead2\

   You should end up with these new files:
     left4dead2\addons\nvda_menu.dll
     left4dead2\addons\nvda_menu.vdf
     left4dead2\addons\nvdaControllerClient32.dll
     left4dead2\addons\sourcemod\extensions\nvda.ext.dll
     left4dead2\addons\sourcemod\extensions\nvdaControllerClient32.dll
     left4dead2\addons\sourcemod\plugins\nvda_menus.smx

3. Add -insecure to your L4D2 launch options:
   - Open Steam and go to your Library
   - Right-click Left 4 Dead 2 and choose Properties
   - In the Launch Options box, type:  -insecure
   - Close the window

4. Start NVDA, then launch L4D2. The mod loads automatically.


KEY BINDINGS
------------
These are set automatically when you load into a map. You do not need to bind them manually.

  H  —  Status announcement
         Speaks your current health, temporary health (from pills/adrenaline),
         active weapon, throwable, medkit, and pills.

  X  —  Lock on to nearest hostile
         Snaps your view to face the nearest enemy and announces its name.
         Prioritises: Tank > Special Infected > Witch > Common Infected.
         Commons that are very close (within ~6 metres) override distant specials.
         Range limit: 2000 units (~38 metres). PVE modes only.


WHAT IS READ ALOUD
------------------
Menus:
  - Main menu buttons (game modes, add-ons, extras, etc.)
  - Game setup screen (campaign, chapter, difficulty, character selection)
    Section headers are announced when you first enter each section,
    e.g. "Campaign: Dead Center", then "Dark Carnival", "Swamp Fever"...
  - Pause menu
  - Options tabs and buttons

In-game events:
  - Player incapacitated / died (survivors only)
  - Special infected grabs: Smoker, Hunter, Jockey, Charger
  - Special infected releases
  - Item aim detection: nearby items on the ground are announced as you look at them
  - Item pickup confirmation
  - Chat messages (player name and text)
  - Teammate revives


TROUBLESHOOTING
---------------
- "NVDA is not speaking anything in menus":
    Make sure L4D2 was launched with -insecure and that NVDA is running.

- "In-game events are not spoken":
    Make sure SourceMod is installed correctly. Load into a map and check that
    the console does not show errors about missing plugins.

- "The H or X key does not work":
    The keys are bound automatically when you load into a map. If they conflict
    with another binding, open the console and type:
      bind h sm_nvda_status
      bind x sm_nvda_lockon

- "Lock-on says No hostiles nearby even though enemies are present":
    Lock-on only works in PVE modes (Campaign, Realism, Survival, Mutations).
    It is disabled in Versus and Scavenge.


CREDITS
-------
Built by Lilian Coghlan.
Uses nvdaControllerClient32.dll from NVDA 2026.1.1 (NV Access, GPL licence).
Uses SourceMod by AlliedModders LLC.
