# ttydesktop
idk man

a terminal-based kinda? desktop environment slash window manager

and. windows. and apps

and written in C i like C

## features
what do people even put here

uhhh

* windows


i suck at this

* dynamic app loading
* window managemtn - move, resize, focus, close. a shockr i know
* fullscreen terminal rendering and ncurses sucks by the way
* is multithreading a feature
* events
* look idk a status bar ok
* example apps????

## bulding
with make

needs clang+lld (tho not necessarily) pthread and libdl

stuff in bin

## usage
./bin/ttydesktop or make run

thats all

### oh wait i need to describe how to use it too
basically, a state machine
* normal - press a command key and youre not normal anymore
* * q - quit (arguably the best command you can run)
* * f - focus a window
* * m - move a window
* * r - resize a window
* * c - close a window
* * this is really boring
* * o - open a window
* moving and resizing
* * hjkl or arrow keys, esc to cancel, enter to confirm
* focused
* * all key events sent to the focused window
* * unless you press escape
* * then unfocus
* quit


ight whats next
## included examples
### example.c
boring
### bouncy.c
less boring its a little thing that bounces around
### shadows.c
this one my fave it hooks into the window rendering stuff and adds shadows

## license
GPLv3
