CC = gcc
CFLAGS = -O3 -Wall -Wextra
LDFLAGS = -lwinmm -lwininet -lm

SRC = main.c renderer.c audio.c search.c agent.c brain.c
TARGET = sphere_ai.exe
VOICE_SERVICE = sphere_voice_service.exe
CSC = C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe
SPEECH_DLL = C:\Windows\Microsoft.NET\assembly\GAC_MSIL\System.Speech\v4.0_4.0.0.0__31bf3856ad364e35\System.Speech.dll

all: $(VOICE_SERVICE) $(TARGET)

$(VOICE_SERVICE): sphere_voice_service.cs
	$(CSC) /optimize+ /reference:$(SPEECH_DLL) /out:$(VOICE_SERVICE) sphere_voice_service.cs

$(TARGET): $(SRC) config.h renderer.h audio.h search.h agent.h brain.h
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	del /Q $(TARGET) $(VOICE_SERVICE) *.o 2>nul || rm -f $(TARGET) $(VOICE_SERVICE) *.o

run: all
	./$(TARGET)
