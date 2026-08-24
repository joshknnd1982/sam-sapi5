SAM Voice for SAPI5
===================

SAM (Software Automatic Mouth) is a speech synthesizer first published in 1982.
This package makes it available to any Windows program that speaks through
SAPI5 -- screen readers, document readers, and anything else that uses the
Windows speech interface.


What is installed
-----------------

  SamVoiceSAPI.dll        The 64-bit SAPI5 speech engine.
  x86\SamVoiceSAPI.dll    The 32-bit engine, for 32-bit programs.
  SamVoiceSettings.exe    The settings utility.
  cmudict.txt             Pronunciation dictionary (about 126,000 words).
  sam_render.exe          Command-line renderer, for producing WAV files.


The six voices
--------------

  SAM Sam                 The original voice.
  SAM Elf                 Lighter and brighter.
  SAM Little Robot        Faster, with a harder edge.
  SAM Stuffy Guy          Lower and more nasal.
  SAM Little Old Lady     Higher pitched.
  SAM Extra Terrestrial   Slow and very wide toned.

Each voice is a combination of pitch, speed, mouth and throat settings, and
every one of those is adjustable.


Changing how the voices sound
-----------------------------

Open SAM Voice Settings from the desktop or the Start menu. It offers:

  Pitch        0 to 255. Lower values sound higher, because the number is a
               pitch period rather than a frequency.
  Speed        1 to 255. Lower values are faster.
  Mouth        0 to 255. Scales the first formant.
  Throat       0 to 255. Scales the second formant.
  Inflection   0 to 100. 0 is a monotone, 50 is normal, 100 is dramatic.
  Sing mode    Holds the pitch steady across vowels.
  Volume       0 to 100, applied on top of the volume the program asks for.
  Numbers      Whether digits are read as words ("sixty") or as digits.
  Rate range   The engine speeds used at the slowest and fastest SAPI rates.

Every setting is saved as you change it and takes effect on the next thing
spoken -- there is no need to restart your screen reader or your program.

Settings are kept per voice, so adjusting Elf does not disturb Sam.

All the controls are in the tab order and are labelled for screen readers.
Values are entered in edit boxes with spin buttons rather than sliders, so the
exact number is announced instead of a percentage.


Where things are stored
-----------------------

Settings:  %APPDATA%\SoftwareAutomaticMouth\settings.ini
Logs:      %LOCALAPPDATA%\SoftwareAutomaticMouth\logs\

Nothing about the voices or their parameters is stored in the registry. The
only registry entries are the two COM class registrations and the one voice
enumerator entry that SAPI requires in order to find the engine at all.


Logging
-------

Logging is off by default. Turn it on in SAM Voice Settings with the
"Log detail" list, then use "Open log folder" to reach the files:

  sapi5-x64.log     From 64-bit programs.
  sapi5-x86.log     From 32-bit programs.
  config-x64.log    From the settings utility.

The installer writes its own log to install-log.txt in this folder.


Uninstalling
------------

Use Add or Remove Programs. The uninstaller unregisters both engines before
removing the files. Your settings and logs under your user profile are left in
place; delete those folders by hand if you want them gone.
