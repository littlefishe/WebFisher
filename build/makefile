CFLAGS += -pthread
DLFLAGS += -lpthread
TARGET = test_hook
CC = g++
LIBS = libfisher.so
OBJECT = log.o util.o fiber.o scheduler.o timer.o iomanager.o fdmanager.o hook.o
SRC_OBJECT = ../log.cpp ../util.cpp ../fiber.cpp ../scheduler.cpp ../timer.cpp ../iomanager.cpp ../fdmanager.cpp ../hook.cpp
H_OBJECT = ../log.h ../util.h ../fiber.h ../scheduler.h ../timer.h ../iomanager.h ../fdmanager.h ../hook.h ../format.h ../singleton.h 
TEST = ../test/test_hook.cpp
AR = ar rc

all: $(TARGET)

$(TARGET): $(TEST) $(LIBS)
	$(CC) -o $@ $< -I. -L. -lfisher

$(LIBS): $(OBJECT)
	$(CC) -shared -fPIC -o $@ $^

$(OBJECT): $(SRC_OBJECT) $(H_OBJECT)
	$(CC) -c -fPIC $(SRC_OBJECT) $(CFLAGS) $(DLFLAGS) -Wall


run:
	export LD_LIBRARY_PATH="."; ./$(TARGET); unset LD_LIBRARY_PATH


clean:
	rm *.o *.a *.so $(TARGET)