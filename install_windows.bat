@echo off
setlocal
chcp 65001 >nul

echo.
echo ==========================================================
echo               Sphere AI - Windows Setup                   
echo ==========================================================
echo.

:: 1. Check C Compiler (GCC / MinGW)
echo [1/5] Checking C Compiler (GCC)...
where gcc >nul 2>&1
if %ERRORLEVEL% equ 0 (
    for /f "tokens=*" %%v in ('gcc --version 2^>^&1') do (
        echo [OK] Found GCC: %%v
        goto :gcc_found
    )
) else (
    if exist "C:\TDM-GCC-64\bin\gcc.exe" (
        set "PATH=C:\TDM-GCC-64\bin;%PATH%"
        echo [OK] Found GCC in C:\TDM-GCC-64\bin
        goto :gcc_found
    )
    if exist "C:\msys64\mingw64\bin\gcc.exe" (
        set "PATH=C:\msys64\mingw64\bin;%PATH%"
        echo [OK] Found GCC in C:\msys64\mingw64\bin
        goto :gcc_found
    )
    if exist "C:\msys64\ucrt64\bin\gcc.exe" (
        set "PATH=C:\msys64\ucrt64\bin;%PATH%"
        echo [OK] Found GCC in C:\msys64\ucrt64\bin
        goto :gcc_found
    )
    if exist "C:\MinGW\bin\gcc.exe" (
        set "PATH=C:\MinGW\bin;%PATH%"
        echo [OK] Found GCC in C:\MinGW\bin
        goto :gcc_found
    )
    echo [ERROR] GCC compiler not found in PATH!
    echo Please install MinGW-w64 or run: winget install -e --id BrechtSanders.winlibs.POSIX.UCRT
    exit /b 1
)

:gcc_found

:: 2. Check C# Compiler (CSC)
echo [2/5] Locating .NET C# Compiler (csc.exe)...
set "CSC="
if exist "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe" (
    set "CSC=C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
) else if exist "C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe" (
    set "CSC=C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe"
)

if "%CSC%"=="" (
    echo [ERROR] .NET Framework 4.0 csc.exe not found!
    exit /b 1
)
echo [OK] Using C# Compiler: %CSC%

:: 3. Check Python and Install Required Libraries
echo [3/5] Checking Python and Installing Python Dependencies...
where python >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo [OK] Python is installed.
    echo Installing/verifying requests, yt-dlp, and speechrecognition...
    python -m pip install --quiet --upgrade pip requests yt-dlp speechrecognition 2>nul
) else (
    echo [WARNING] Python not detected in PATH.
    echo Note: Live AI model routing and music streaming require Python 3.8+
)

:: 4. Check Optional Ollama Small AI Model
echo [4/5] Checking Small AI Model Engine...
set "OLLAMA_EXE="
where ollama >nul 2>&1
if %ERRORLEVEL% equ 0 (
    set "OLLAMA_EXE=ollama"
) else if exist "%LOCALAPPDATA%\Programs\Ollama\ollama.exe" (
    set "OLLAMA_EXE=%LOCALAPPDATA%\Programs\Ollama\ollama.exe"
)

if not "%OLLAMA_EXE%"=="" (
    echo [OK] Found Ollama at %OLLAMA_EXE%
    echo Checking/pulling ultra-fast small AI model qwen2.5:0.5b...
    "%OLLAMA_EXE%" pull qwen2.5:0.5b 2>nul
) else (
    echo [INFO] Ollama not found. SphereAI will use native offline reasoning and live web fetching.
    echo To install Ollama later for local AI model support: winget install Ollama.Ollama
)

:: 5. Compile Everything and Save Compiled Executables
echo [5/5] Compiling SphereAI and Saving Binaries...

taskkill /F /IM sphere_ai.exe /IM sphere_voice_service.exe >nul 2>&1
ping 127.0.0.1 -n 2 >nul

set "SPEECH_DLL=C:\Windows\Microsoft.NET\assembly\GAC_MSIL\System.Speech\v4.0_4.0.0.0__31bf3856ad364e35\System.Speech.dll"
if exist "%SPEECH_DLL%" (
    "%CSC%" /optimize+ /reference:"%SPEECH_DLL%" /out:sphere_voice_service.exe sphere_voice_service.cs >nul 2>&1
) else (
    "%CSC%" /optimize+ /reference:System.Speech.dll /out:sphere_voice_service.exe sphere_voice_service.cs >nul 2>&1
)

if not exist "sphere_voice_service.exe" (
    echo [ERROR] Failed to compile sphere_voice_service.exe!
    exit /b 1
)
echo [OK] Compiled: sphere_voice_service.exe

gcc -O3 -Wall -Wextra main.c renderer.c audio.c search.c agent.c brain.c -o sphere_ai.exe -lwinmm -lwininet -lole32 -lm
if not exist "sphere_ai.exe" (
    echo [ERROR] Failed to compile sphere_ai.exe!
    exit /b 1
)
echo [OK] Compiled: sphere_ai.exe

(
echo @echo off
echo chcp 65001 ^>nul
echo .\sphere_ai.exe
) > run_sphere.bat

echo.
echo ==========================================================
echo        Sphere AI Installation Complete Successfully!       
echo ==========================================================
echo.
echo Executable saved at: %CD%\sphere_ai.exe
echo.
echo To run SphereAI in PowerShell:
echo   [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
echo   .\sphere_ai.exe
echo.
echo Or run the batch launcher:
echo   .\run_sphere.bat
echo.
