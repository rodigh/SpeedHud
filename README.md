For details, please refer to the description in the source code of the original (forked-from) repository.

If the SpeedHud.asi file is quarantined by Windows Defender during download, please allow the file in Windows Defender.

The main focus is compatibility with the new version; there are no additional features.

2026/09/20:v43 — v43-compatible version that supports the speedometer and time manipulation(Debug build)

2026/09/21:v43 Release build — replaced with the release build due to reports that it was not working

2026/09/21:v43 Release build 2 — Due to my mistake, I uploaded a personal build file, which prevents time operations from being performed with F1 and F2 under normal use. I am therefore replacing the file again

Currently, I have to modify the source code and release a new version for every patch.
Ideally, I should redesign it to be patch-independent, but I lack the technical skills to do so.
I also don't know how long I'll be able to maintain it or how long I'll remain hooked on *SnowRunner*.
Therefore, I am considering a change where the memory addresses are specified in an INI file, allowing anyone to apply the necessary updates.
