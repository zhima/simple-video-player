Simple-Video-Player
===============
## Introduction

This program mostly learned form [dranger's An ffmpeg and SDL Tutorial](http://dranger.com/ffmpeg/). The tutorial's code use `SDL1.x` APIs to display video, Some part of tutorial's code is modified to adjust `SDL2` library, as some APIs in `SDL2` is diffrent form `SDL1.x`.

## Requirements


Before compiling and running this program, you must have installed `ffmpeg2.x` and `SDL2` from source code in your system, otherwise it may occurs some unexpected errors.

## Get Started

Building command:

	make

Running command:

	./tutorial-sdl2-player <videoPath>
	
Cleaning command:

	make clean
	

## Feature
### Seeking
use the `↑` `↓` `←` `→` buttons to seek the video.

- ↑: + 60s
- ↓: - 60s
- ←: - 10s
- →: + 10s



## Todo
- sync the video&audio to external clock
- play/stop buttons and its functions

