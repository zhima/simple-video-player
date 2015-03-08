CC	= gcc
CFLAGS	= -Wall -g
LFLAGS	= -L/usr/local/lib
LIBS	= -lavcodec -lavformat -lavutil -lswscale -lswresample -lz -lm -lSDL2
OBJS	= main.o videoutils.o

TARGET = tutorial-sdl2-player

all:$(TARGET)

tutorial-sdl2-player: $(OBJS)
	$(CC) -o $@ $^ $(LFLAGS) $(LIBS)

%.o:%.c
	$(CC) -o $@ -c $< $(CLFAGS)

%.d:%.c
	@set -e; rm -f $@; 	$(CC) -MM $(CFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@;  \
	rm -f $@.$$$$ 

sinclude $(SOURCES:.c=.d)

clean:
	rm -rf *.o *.d *.d.* $(TARGET)