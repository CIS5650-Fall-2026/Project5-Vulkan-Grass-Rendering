@echo off
echo Interactive grass: arrows move, Q/E lower/raise, I contact, P orbit, T sphere reset.
echo Space pauses both sphere and grass. N single-steps. R resets the scene. L toggles LOD.
call "%~dp0run-debug.cmd" --interactive %*
