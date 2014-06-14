tutorial-player
===============
This code mostly learned from dranger's An FFmpeg and SDL Tutorial, change a little to adjust SDL2 library, as some APIs in SDL2 is diffrerent from SDL1.x.

Before you compile to generate the program, you must have installed ffmpeg2.x and SDL2 from source code.

The command to generate the executable program:
	
	gcc main.c videoutils.h videoutils.c -lavcodec -lavformat -lavutil -lswscale -lswresample -lz -lm -lSDL2
