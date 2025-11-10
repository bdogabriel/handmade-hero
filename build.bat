@echo off
if not exist build mkdir build
pushd build
cl ..\src\win32_handmade.cpp
popd
