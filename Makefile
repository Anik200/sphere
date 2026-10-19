ifeq ($(OS),Windows_NT)
    CC = gcc
    TARGET = sphere_ai.exe
    VOICE_SERVICE = sphere_voice_service.exe
    LDFLAGS = -lwinmm -lwininet -lm
    CSC ?= C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe
    SPEECH_DLL ?= C:\Windows\Microsoft.NET\assembly\GAC_MSIL\System.Speech\v4.0_4.0.0.0__31bf3856ad364e35\System.Speech.dll
    CLEAN_CMD = cmd /c "del /Q $(TARGET) $(VOICE_SERVICE) *.o 2>nul || exit 0"

all: $(VOICE_SERVICE) $(TARGET)

$(VOICE_SERVICE): sphere_voice_service.cs
	-$(CSC) /optimize+ /reference:$(SPEECH_DLL) /out:$(VOICE_SERVICE) sphere_voice_service.cs

else
    CC ?= gcc
    TARGET = sphere_ai
    VOICE_SERVICE =
    LDFLAGS = -lm -pthread
    CLEAN_CMD = rm -f $(TARGET) sphere_voice_service.exe *.o

all: $(TARGET)

endif

CFLAGS ?= -O3 -Wall -Wextra
SRC = main.c renderer.c audio.c search.c agent.c brain.c

$(TARGET): $(SRC) config.h renderer.h audio.h search.h agent.h brain.h
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	$(CLEAN_CMD)

run: all
	./$(TARGET)

.PHONY: all clean run


