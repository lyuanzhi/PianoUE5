# PianoUE5
A piano game created with Unreal Engine 5 for piano teaching. This piano can play any piece based on the key information from MIDI files, including dynamics, duration, and more.

## Tech Stack
1. Unreal Engine 5
2. C++
3. Python

## Usage
1. clone the repo
2. unzip ```WindowsApp.zip```
3. run ```Piano_v2.exe```
##### Note: Because I used ```GitHub LFS (Large File Storage)``` for ```WindowsApp.zip```, downloading the zip directly will not include ```WindowsApp.zip```.

## Dependencies
1. Unreal Engine 5.1.1

## Design
### Load MIDI File
![](imgs/loadMIDI.png)
##### Here, the MIDI file will be parsed by ```convert.py```, and then be read by UE5.

### Play Sound
![](imgs/PlaySound.png)

### Press Key Animation
![](imgs/keyPress.png)
![](imgs/keyOff.png)

### Play / Pause Songs
![](imgs/PlayPause.png)

### Switch Songs
![](imgs/SwitchSong.png)

### Select Songs From File Manager
![](imgs/ChooseSong.png)

## Package Project
![](imgs/package.png)

## Demo Screenshots
![](imgs/demo.gif)
