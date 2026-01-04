# CHIRP Audio Trigger
The CHIRP Audio Trigger is a sound file player similar to the trusty Sparkfun/Robertsonics MP3 Trigger board. Functionality is focused on using the CHIRP Audio Trigger as the sound system for an Astromech droid, but it is also very usable as a general sound player for other projects, and is a fun platform to experiment with audio playback and processing programming. The stock firmware can read the contents of the SD card and provide a droids primary microcontroller with a manifest of sounds - allowing sound names to be sent to the operators radio transmitter and displayed as telemetry on the transmitters GUI.

# Initial Audio Trigger Features
- decode, play and mix multiple audio filetypes (MP3 and uncompressed WAV, stereo or mono, 44.1kHz or 22.05kHz sample rates)
- easy to implement in current droid control systems (Padawan/Shadow/Kyber)
- room on the hardware for advanced droid nonsense
- affordable PCB with minimal easy to source components

# Serial Commands
Support for the same serial commands as the MP3 trigger, so this board can be used as a drop-in replacement. We also have some new serial commands to support the advanced functions.

- PLAY
- STOP
- VOL
- STAT
- GMAN (Get Manifest of how many sounds are in each page of each Sound Bank)
- LIST (List dounds stored in Flash
- GNME (Get the name of a particular sound)
- CHRP (play a basic sweep)
- CCRC (clear stored CRC value)
- BAUD (change the serial baud rate)
- BPAGE (change the default page for Bank 1)

# Button Actions
The Prev, Play/Stop and Next buttons act similarly to the navigation buttons of the MP3 Trigger, playing files that are stored in the root of the SD card. They also have additional configuration functions that can be useful for setting up your droid without needed to send specific serial commands or edit the CHIRP.INI file.

 - Hold "Prev" button + press "Play/Stop" button - this will change the serial baud rate (cycling between 115200, 2400 and 9600).
 - Hold "Prev" button + press "Next" button - this will change the Sound Bank 1 from its current value (by default "A"). You must have appropriate folders in your SD card for this change to work (for example, "1A_R2D2" and "1B_R5D4").
