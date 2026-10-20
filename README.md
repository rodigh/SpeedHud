please download everything at once by selecting "Download ZIP" from the "Code" menu.


Please refer to the following URL for details.

https://mod.io/g/snowrunner/m/speedometer#description

https://github.com/crzknya/SpeedHud

-----

2026/10/20:v44 Update(SpeedAdressOnly)

Changed to a format that sets and loads addresses via an .ini file.

Added "SpeedAddress.asi" for locating the speed address.

-----

To allow anyone to perform maintenance in the future, the following settings have been added to `speedhud.ini` to enable address configuration.

The addresses for v44 are exported by default.

If the `[ADDRESS]` section does not exist, please launch the game once and press the HOME key on your keyboard.

[ADDRESS]

SpeedAddress=0x2A8FA2C

----

To determine the `SpeedAddress`, please use `SpeedAddress.asi` located in the `SpeedAddress` folder.

Ideally, the `SpeedAddress` should be retrieved automatically, but I lack the technical expertise to implement that.

----

If you wish to manipulate time, please install SnowFlyer2:

https://github.com/NakedDevA/SnowFlyer2

I have implemented support for time manipulation via memory addresses in the .ini file.

However, as the processing method used by SnowFlyer2 has been altered, I have determined that it would be difficult to ensure compatibility with it.

-----

2026/09/21:v43 Release build 2 — Due to my mistake, I uploaded a personal build file, which prevents time operations from being performed with F1 and F2 under normal use. I am therefore replacing the file again

2026/09/21:v43 Release build — replaced with the release build due to reports that it was not working

2026/09/20:v43 — v43-compatible version that supports the speedometer and time manipulation(Debug build)
