# PianoUE5
A piano game created with Unreal Engine 5 for piano teaching. This piano can play any piece based on the key information from MIDI files, including dynamics, duration, and more.

## Tech Stack
1. Unreal Engine 5
2. C++
3. Python

## Usage
1. clone the repo
2. install the [Dependencies](#dependencies)
3. open ```Piano_v2.uproject``` with Unreal Engine 5.8 (the C++ module is built on first open), or [package the project](#package-project) and run ```Piano_v2.exe```

## Dependencies
1. Unreal Engine 5.8
2. Visual Studio 2022 (17.14+) or Visual Studio 2026, with the ```Game development with C++``` workload, to build the C++ module
3. ```mido``` and ```numpy```, installed into the Python that ships with the engine:
```
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe" -m pip install mido numpy
```
##### Note: ```convert.py``` runs on the engine's bundled Python, both in the editor and in packaged builds, so the packages must be installed there rather than in a system Python.

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
##### The packaged build includes the engine's Python (with ```mido``` and ```numpy```) through the ```Python3``` module dependency, and ```convert.py``` and ```Content/MIDI``` through ```RuntimeDependencies``` in ```Piano_v2.Build.cs```, so no files need to be copied by hand after packaging.

## Demo Screenshots
![](imgs/demo.gif)
