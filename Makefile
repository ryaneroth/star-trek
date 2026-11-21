CL65 ?= cl65

TARGET = star-trek.bin

.PHONY: all clean

all: $(TARGET)

$(TARGET): kim1_io.s star-trek.c kim1.cfg
	$(CL65) -t none -C kim1.cfg -O -m star-trek.map -o $@ kim1_io.s star-trek.c

clean:
	rm -f $(TARGET) star-trek.map kim1_io.o star-trek.o startup.o
