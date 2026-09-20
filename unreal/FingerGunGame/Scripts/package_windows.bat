@echo off
rem Builds Red Dead Dimension into a Windows .exe. Must run on Windows: Unreal cannot build Win64 from a Mac.
rem Needs UE 5.8 and Visual Studio 2022 with the C++ game workload. Output: Packaged\Windows\FingerGunGame.exe
set UE=C:\Program Files\Epic Games\UE_5.8
if not exist "%UE%" echo Set UE in this file to your UE_5.8 folder & exit /b 1
call "%UE%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%~dp0..\FingerGunGame.uproject" -noP4 -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive -archivedirectory="%~dp0..\Packaged" -map=/Game/Levels/IronHorse -IgnoreCookErrors -iterate -utf8output
echo.
echo Start the game: Packaged\Windows\FingerGunGame.exe     Start the tracker: tracker\.venv\Scripts\python tracker\run.py --no-window
