@echo off
rem Print the 8.3 short path of the directory or file given as the first
rem argument. CMake has no primitive for this, and the batch-parameter path
rem operator that produces it works only inside a batch file, so this script
rem exists. Used by cmake/BetterCADBuildLocation.cmake.
rem
rem Keep percent-tilde tokens out of these comments: cmd.exe performs
rem batch-parameter substitution on a rem line too, and an unknown modifier
rem there fails the whole script.
for %%I in ("%~1") do @echo %%~sI
