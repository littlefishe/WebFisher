CFLAGS += -pthread
DLFLAGS += -lpthread
TARGET = test_iomanager
CC = g++
LIBS = fisher.a
OBJECT = log.o util.o fiber.o scheduler.o timer.o iomanager.o
SRC_OBJECT = ../log.cpp ../util.cpp ../fiber.cpp ../scheduler.cpp ../timer.cpp ../iomanager.cpp
H_OBJECT = ../log.h ../util.h ../fiber.h ../scheduler.h ../timer.h ../iomanager.h ../format.h ../singleton.h 
TEST = ../test/test_iomanager.cpp
AR = ar rc

all: $(TARGET)

$(TARGET): $(TEST) $(LIBS)
	$(CC) -o $@ $< $(CFLAGS) $(DLFLAGS) -static $(LIBS) -g

$(LIBS): $(OBJECT)
	$(AR) $(LIBS) $(OBJECT)

$(OBJECT): $(SRC_OBJECT) $(H_OBJECT)
	$(CC) -c $(SRC_OBJECT) $(CFLAGS) $(DLFLAGS) -Wall

clean:
	rm *.o *.a $(TARGET)