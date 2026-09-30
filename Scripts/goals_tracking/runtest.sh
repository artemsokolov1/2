#!/bin/bash
# usage: runtest.sh <tag> <autodrive> [frames]
TAG=$1; DRIVE=$2; N=${3:-2500}
LOG=/f/MiniFootball/Saved/Logs/MF_$TAG.log
rm -f $LOG
powershell -NoProfile -Command "Get-Process UnrealEditor -ErrorAction SilentlyContinue | Where-Object { \$_.MainWindowTitle -notlike '*Unreal Editor*' } | Stop-Process -Force"
powershell -NoProfile -Command "Start-Process 'F:\UnrealEngine\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' -ArgumentList '\"F:\MiniFootball\MiniFootball.uproject\" -game -windowed -ResX=1280 -ResY=720 -MFAutoTraining -log=MF_$TAG.log -ExecCmds=\"mf.AutoDrive $DRIVE, mf.Trace 1\"'"
for i in $(seq 1 400); do [ -f $LOG ] && [ $(grep -c MFTRACE $LOG) -gt $N ] && break; sleep 2; done
powershell -NoProfile -Command "Get-Process UnrealEditor -ErrorAction SilentlyContinue | Where-Object { \$_.MainWindowTitle -notlike '*Unreal Editor*' } | Stop-Process -Force"
python /f/Temp/User/claude/F--MiniFootball/596f43c9-f1dd-4f64-bcd5-2c53a5b782f8/scratchpad/anal.py $LOG
