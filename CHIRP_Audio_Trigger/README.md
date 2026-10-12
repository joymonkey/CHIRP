# CHIRP Audio Trigger
The CHIRP Audio Trigger is a sound file player similar to the trusty Sparkfun/Robertsonics MP3 Trigger board. Functionality is focused on using the CHIRP Audio Trigger as the sound system for an Astromech droid, but it is also very usable as a general sound player for other projects, and is a fun platform to experiment with audio playback and processing programming. The stock firmware can read the contents of the SD card and provide a droids primary microcontroller with a manifest of sounds - allowing sound names to be sent to the operators radio transmitter and displayed as telemetry on the transmitters GUI.

# Initial Audio Trigger Features
- decode, play and mix multiple audio filetypes (MP3 and uncompressed WAV, stereo or mono, 44.1kHz or 22.05kHz sample rates)
- easy to implement in current droid control systems (Padawan/Shadow/Kyber)
- room on the hardware for advanced droid nonsense
- affordable PCB with minimal easy to source components

# Serial Commands
Support for the same serial commands as the legacy SparkFun/Robertsonics MP3 Trigger, so this board can be used as a drop-in replacement, alongside CHIRP's high-level command set.

### Legacy MP3 Trigger Commands
- `t` + `<byte>` : Trigger track by number (binary 1–255). Plays from root of SD or active Bank 1 (`1A_...`).
- `T` + `<char>` : Trigger track by number (ASCII `'1'`–`'9'`).
- `p` + `<byte>` : Play track by index (binary 1–255).
- `v` + `<byte>` : Set volume (binary 0–255; 0 is max volume, 255 is silent).
- `O` : Navigation Center (Play/Stop toggle).
- `F` : Navigation Forward (Next track).
- `R` : Navigation Reverse (Previous track).
- `S` : Stop all streams (line-terminated alias).
- `P` : Toggle play/pause (line-terminated alias).

### CHIRP Extended Commands
- `PLAY:index[,bank[,page[,vol]]]` (plays a sound from a Sound Bank folder)
- `STOP` / `STOP:<stream>` (stop all streams or specified stream)
- `VOL:<vol>` / `VOL:<stream>,<vol>` (set global volume or individual stream volume, 0–99)
- `STAT:<stream>` (get the current status of a specified stream)
- `GMAN` (Get Manifest of how many sounds are in each page of each Sound Bank)
- `LIST` (List sounds stored in Sound Bank 1 as well as sound counts in Sound Banks 2-6) 
- `GNME:bank,page,index` (Get the name of a particular sound in a Sound Bank)
- `CHRP:startHz,endHz,ms[,vol]` (play a basic sweep sound)
- `CCRC` (reset manifest checksum)
- `BAUD:<rate>` (change the serial baud rate - 2400, 9600, 19200, 38400, 57600 or 115200; persists to CHIRP.INI)
- `BPAGE:<page>` (change the default page for Bank 1 - requires reboot after changing)
- `MUSB` / `MUSB:1` / `MUSB:0` (enable/disable Mass Storage Class for USB - to access the SD card over USB)

# Button Actions
The Prev, Play/Stop and Next buttons act similarly to the navigation buttons of the MP3 Trigger, playing files that are stored in the root of the SD card. They also have additional configuration functions that can be useful for setting up your droid without needed to send specific serial commands or edit the CHIRP.INI file.

 - Hold "Prev" button + press "Play/Stop" button - this will change the serial baud rate (cycling between 115200, 9600 and 2400).
 - Hold "Prev" button + press "Next" button - this will change the Sound Bank 1 from its current value (by default "A"). You must have appropriate folders in your SD card for this change to work (for example, "1A_R2D2" and "1B_R5D4").
 - Not a button, but pulling GPIO 7 to ground enables USB Mass Storage Class. 

# SD Card Structure
To simplify browsing of files from the transmitter side of things, sound files should be arranged into several "Sound Banks" on the SD card. 6 Sound Banks are supported, each one can have up to 26 "Pages". Sound Bank 1 is reserved specifically for primary droid vocals. Sound Banks 2-6 can be for whatever custom music or sound effects are desired. All audio streams directly from the SD card into high-speed PSRAM buffers with near-zero latency, allowing immediate playback without flash syncing delays.

## Sound Bank 1
Sound Bank 1 can contain WAV or MP3 files. Sound Bank 1 page folders should be named similarly to...
- 1A_R2D2
- 1B_R5D4
- 1C_K2SO
- etc

One of these pages/folders will be set as the droid's primary sounds. For example, you would set it to A for R2D2 or B for R5D4. This is set in the CHIRP.INI config file, and defaults to A.   
## Sound Banks 2-6
Sound Banks 2-6 have looser rules and should be named similarly to...
- 2A_StarWarsMusic
- 2B_StarWarsClips
- 2C_SWRemixes
- 3A_PopMusic
- etc

Breaking up the file list into folders like this is intended to make sound navigation simpler. The user might have 4 different trigger buttons available on their transmitter, so might want to assign each button to sound banks 1-4. The user could then have a button or switch to cycle through "Pages" of each bank, so hundreds of files are browsable with relative ease.
## Sound Variants
Droid beep-boop files should be organized by name, and followed by a number if there are multiple variants of the same sound type (with an underscore before the number, e.g. `chat_01.wav`). For example, my current R2D2 folder has 61 files, but many are variants of the same sound type, so on my controller it only shows 9 sounds and if a sound is triggered that has multiple variants the CHIRP Audio Trigger will choose one of the variants at random. My R2D2 files are stored something like...
- 1A_R2D2/chat_01.wav
- 1A_R2D2/chat_02.wav
- 1A_R2D2/chat_03.wav
- 1A_R2D2/chat_04.wav
- 1A_R2D2/happy_01.wav
- 1A_R2D2/happy_02.wav
- 1A_R2D2/sad_01.wav
- 1A_R2D2/sad_02.wav
- 1A_R2D2/sad_03.wav

...and my transmitter displays these as CHAT, HAPPY, SAD (these are the names that are sent back to my transmitter as telemetry packets).
