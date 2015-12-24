sidewinderd - Logitech G710+ branch
===================================

Sidewinder daemon - Linux support for Logitech G710+ keyboard


About this branch
=================

sidewinderd was originally a userland daemon for supporting the Microsoft
Sidewinder X4 and X6 gaming keyboards under Linux. Now, I was able to get the
Logitech G710+ to work with this program.

Special thanks to nirenjan, for his extremely well written documentation about
the Logitech G710+ (https://github.com/nirenjan/g710plus-linux).

Support for Sidewinder X4 and X6 is broken in this branch, as I've just hacked
this branch quick & dirty. In the long run, I will do some major code
refactoring and get both keyboards working in master.


How to record macros
====================

1. Choose the profile you want to use, by pressing either M1, M2 or M3.
2. Press MR key. Record LED will light up.
3. Now, press one the G1 - G6 keys you want to use.
4. Your keyboard is in Record Mode now. On the Logitech G710+, there is currently
no visual feedback, when your keyboard is in Record Mode.
5. Every keypress will get recorded, including delays. Excluded keys are: M1 -
M3, MR, G1 - G6, Game Mode, WASD Backlight, Backlight, Play/Pause, Stop,
Previous Song, Next Song.
6. When you're finished, press MR again. The Macro will get saved under
~/.local/share/sidewinderd/profile_[1-3]/s[1-6].xml.


Dependencies
============

- cmake 2.8.8 (make)
- libconfig 1.4.9
- tinyxml2 2.2.0
- libudev 210


Features
========

- Macro recording
- Macro playback, using XML files - similar to Microsoft's solution
- Special keys (S1-S6 / S1-S30 keys) - listening to keys via hidraw
- Profile switch
- Setting LEDs (LED 1-3, Record)
- X6 only: toggle Macropad between Macro-mode and Numpad-mode
- Very basic configuration setup


- Auto profile and Auto LED support
- Documentation
- Mac OS X support
- Code refactoring


Hey, I want to help!
====================

Great! I was looking for help ^_^! Please get in contact with me via my public
E-Mail adress, which can be found on my profile page. You can also E-Mail me
about anything else related to this project, like ideas / suggestions.

This project is sticking to Geosoft's C++ style guidelines and is using
Doxygen-friendly comments.

If you find any bugs, please report by opening up an issue on GitHub.
