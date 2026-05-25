@echo off
cd /d D:\Desktop\ClaudeCodexCoding\Unleashed
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" > nul
cl.exe /nologo /EHsc /std:c++20 /I vendor/leechcore src/key2_probe.cpp /link vendor/leechcore/leechcore.lib vendor/leechcore/vmm.lib ws2_32 /OUT:build/Release/Key2Probe.exe
echo Exit code: %ERRORLEVEL%
