CFLAGS += $(shell pkg-config --cflags sdl2)
LDLIBS += $(shell pkg-config --libs   sdl2)

CFLAGS += $(shell pkg-config --cflags SDL2_image)
LDLIBS += $(shell pkg-config --libs   SDL2_image)

TARGET = sdlterm
OBJS = sdlterm.o fvemu.o

$(TARGET): $(OBJS)

clean:
	rm -f $(TARGET) $(OBJS)
