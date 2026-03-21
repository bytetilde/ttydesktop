# ttydesktop
a desktop environment window manager-like thing that runs in the terminal

## features
* dynamic app loading with libdl
* opening, closing, moving, resizing, focusing windows (a shocker)
* windows also have events
* still half-baked multithreading
* a bar at the bottom which is a status bar but also isnt a status bar
* i hate ncurses
* hookman: a meta-app that lets other apps hook into almost anything
* hookman also has function exports so one app can call functions of other app and. yeah

## building
prerequisites:
* by default clang+lld, can change in makefile
* pthreads
* libdl
then, to build everything: make (or make all)

resulting binaries and .so's (apps) go in bin/

## usage
run with ./bin/ttydesktop or ./ttydesktop if youre already in bin/

### usage 2
this is kinda a state machine

* normal - state on launch and when youre not focused or typing a command
* * q - quit
* * o - open a new window, prompts for path to .so
* * f - focus a window by index
* * m - move a window by index with arrow keys or hjkl
* * r - resize a window by index also with arrow keys or hjkl
* * c - close a window by index
* command - state when typing a command, enter to submit, esc to cancel
* focused - when a window is focused
* * all key events are sent to the focused window
* * except esc, that one unfocuses

## hookman
this one is new

hook points for window updates, draws, events, etc. (before, after, and override)
also has export/unexport/call for cross-app function calling

## license
GPLv3