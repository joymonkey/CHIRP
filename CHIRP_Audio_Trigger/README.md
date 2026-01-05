# CHIRP Audio Trigger
The CHIRP Audio Trigger is a sound file player similar to the trusty Sparkfun/Robertsonics MP3 Trigger board. Functionality is focused on using the CHIRP Audio Trigger as the sound system for an Astromech droid, but it is also very usable as a general sound player for other projects, and is a fun platform to experiment with audio playback and processing programming. The stock firmware can read the contents of the SD card and provide a droids primary microcontroller with a manifest of sounds - allowing sound names to be sent to the operators radio transmitter and displayed as telemetry on the transmitters GUI.

# Initial Audio Trigger Features
- decode, play and mix multiple audio filetypes (MP3 and uncompressed WAV, stereo or mono, 44.1kHz or 22.05kHz sample rates)
- easy to implement in current droid control systems (Padawan/Shadow/Kyber)
- room on the hardware for advanced droid nonsense
- affordable PCB with minimal easy to source components

# Serial Commands
Support for the same serial commands as the MP3 trigger, so this board can be used as a drop-in replacement. We also have some new serial commands to support the advanced functions.

- PLAY (stops a stream. no, only kidding, plays a sound from a Sound Bank folder)
- STOP (stop all streams are specified stream)
- VOL (set global volume or individual stream volume)
- STAT (get the current status of a specified stream)
- GMAN (Get Manifest of how many sounds are in each page of each Sound Bank)
- LIST (List sounds stored in Sound Bank 1 as well as sound counts in Sound Banks 2-6) 
- GNME (Get the name of a particular sound in a Sound Bank)
- CHRP (play a basic sweep sound)
- CCRC (clear stored CRC value to force a re-sync of Sound Bank 1 to flash)
- BAUD (change the serial baud rate - 2400, 9600, 19200, 38400, 57600 or 115200)
- BPAGE (change the default page for Bank 1 - requires reboot after changing)
- MUSB (enable/disable Mass Storage Class for USB - to access the SD card over USB)

# Button Actions
The Prev, Play/Stop and Next buttons act similarly to the navigation buttons of the MP3 Trigger, playing files that are stored in the root of the SD card. They also have additional configuration functions that can be useful for setting up your droid without needed to send specific serial commands or edit the CHIRP.INI file.

 - Hold "Prev" button + press "Play/Stop" button - this will change the serial baud rate (cycling between 115200, 2400 and 9600).
 - Hold "Prev" button + press "Next" button - this will change the Sound Bank 1 from its current value (by default "A"). You must have appropriate folders in your SD card for this change to work (for example, "1A_R2D2" and "1B_R5D4").
 - Not a button, but pulling GPIO 7 to ground enables USB Mass Storage Class. 

# SD Card Structure
To simpify browsing of files from the transmitter side of things, sound files should be arranged into several "Sound Banks" on the SD card. 6 Sound Banks are supported, each one can have up to 26 "Pages". Sound Bank 1 is reserved specificalyl for primary droid vocals. Sound Banks 2-6can be for whatever custom music or sound effects are desired.
## Sound Bank 1
Sound Bank 1 should contain WAV files, and total no more than 14MB (if you want to sync to flash memory, otherwise disregard). Sound Bank 1 page folders should be named similarly to...
- 1A_R2D2
- 1B_R5D4
- 1C_K2SO
- etc

One of these pages/folders will be set as the droids primary sounds. For example, you would set it to A for R2D2 or B for R5D4. Changing this value will force sounds files to be re-synced at the next power cycle. This is set in the CHIRP.INI config file, and defaults to A.   
## Sound Banks 2-6
Sound Banks 2-6 have looser rules and shoudl be named similarly to...
- 2A_StarWarsMusic
- 2B_StarWarsClips
- 2C_SWRemixes
- 3A_PopMusic
- etc

Breaking up the file list into folders like this is intended to make sound navigation simpler. The user might have 4 different trigger buttons available on their transmitter, so might want to assign each button to sound banks 1-4. The user could then have a button or switch to cycle through "Pages" of each bank, so hundreds of files are browsable with relative ease.
## Sound Variants
Droid beep-boop files should be organized by name, and followed by a number if there are multiple variants of the same sound type. For example, my current R2D2 folder has 61 files, but many are variants of the same sound type, so on my controller it only shows 9 sounds and if a sound is triggered that has multiple variants the CHIRP Audio Trigger will choose one of the variants at random. My R2D2 files are stored something like...
1A_R2D2/chat01.wav
1A_R2D2/chat02.wav
1A_R2D2/chat03.wav
1A_R2D2/chat04.wav
1A_R2D2/happy01.wav
1A_R2D2/happy02.wav
1A_R2D2/sad01.wav
1A_R2D2/sad02.wav
1A_R2D2/sad03.wav
...and my transmitter displays these as CHAT, HAPPY, SAD (these are the names that are sent back to my transmitter as telemetry packets).
