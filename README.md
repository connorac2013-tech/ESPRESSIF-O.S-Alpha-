NOTE: this O.S is still in alpha phase
stable ver should be available by: 8/16/2026

beginners guide
Note: This guide is for rare or less common CYD (Cheap Yellow Display) models, specifically the 2.8-inch ILI9341 320x240 resolution screen with a capacitive touchscreen.
I have been searching nonstop for an OS for my 2.8-inch CYD. I tried Tactility (but could not figure it out) and MicroPython (it did not seem to work; the LED strobed, and the display just stayed black). I gave up and created my own.


Arduino IDE Setup
1. Open the Arduino IDE.
2. Go to the Board Manager.
3. Install the ESP32 core by Espressif Systems. Wait until the installation is completely finished before continuing to step 4.
4. Reopen the Arduino IDE to ensure the core is fully installed and recognized.
5. Press Ctrl+O (Windows) or Cmd+O (Mac) to open a file.
6. Select the downloaded file: "ESPRESSIF O.S ARDUINO IDE(Alpha).ino".
7. Wait for the new window to open.
8. In the new window, connect your board to your computer via USB.
9. Select your board from the menu (e.g., ESP32-WROOM-DA Module).
10. Press Ctrl+U (Windows) or Cmd+U (Mac), or simply click the Upload button (the right-facing arrow).
11. Enjoy!



Troubleshooting
If the board does not show up in the port menu, it likely uses a CH340 or Silicon Labs USB-to-serial chip, and you will need to download the appropriate driver.
Windows driver guide: https://www.youtube.com/watch?v=tABqVrSs2vA
Mac driver guide: https://www.youtube.com/watch?v=e7oyVC9-9I4
⚠️ Security Note: Be careful to download drivers only from official or trusted sources to avoid malicious programs.
