# ttydesktop

[![license: GPL v3](https://img.shields.io/badge/license-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![build status](https://img.shields.io/github/actions/workflow/status/bytetilde/ttydesktop/ci.yml?branch=master)](https://github.com/bytetilde/ttydesktop/actions)
[![made with C](https://img.shields.io/badge/made%20with-C-blue.svg)](https://en.wikipedia.org/wiki/C_(programming_language))

a desktop environment / window manager thing that runs entirely in the terminal

load dynamic apps, manage windows, and extend functionality with hooks - all still within a terminal

## table of contents
- [features](#features)
- [screenshots](#screenshots)
- [building](#building)
- [usage](#usage)
- [autostart](#autostart)
- [app search paths](#app-search-paths)
- [included apps](#included-apps)
- [hookman](#hookman)
- [license](#license)

---

## features

- **dynamic app loading** using `libdl` - load `.so` apps at runtime
- **full window management**: open, close, move, resize, focus, and bring windows to front (shocker ik)
- **event system** - windows can respond to events
- **status bar** - a bar
- **automatic startup of apps** via `autostart.conf`
- **hookman** - an app that allows other apps to hook into:
  - window draws
  - desktop updates
  - status bar content
  - custom hook points
- **cross-app function calls** - apps can export functions for others to call. via hookman. obviously
- **multithreading** which will always be half-baked no matter what

---

## screenshots

[todo ig](screenshots/)

---

## building

### prerequisites
- `clang` + `lld` (change in the Makefile if you need to)
- `pthreads`
- `libdl`

### build
```bash
make  # or make all
```
the resulting binaries and `.so` apps will be placed in `bin/`

to build without apps:
```bash
make all-desktop
```

---

## usage

run the desktop environment with:
```bash
./bin/ttydesktop [app1.so] [app2.so] ...
```
(the apps are completely optional)

### key bindings

ttydesktop operates in several states:

#### normal mode (launch default, or when no window is focused / not typing a command)
| key | action |
|-----|--------|
| `q` | quit ttydesktop |
| `o` | open a new window - prompts for path to a `.so` app |
| `f` | focus a window by index |
| `b` | bring a window to front by index |
| `m` | move a window by index (use arrow keys or `hjkl`) |
| `r` | resize a window by index (use arrow keys or `hjkl`) |
| `c` | close a window by index |

#### command mode (typing a command)
| key | action |
|-----|--------|
| `Enter` | submit the command |
| `Esc` | cancel and return to normal mode |
| `<-` / `->` | move cursor within the buffer |
| `Backspace` | delete character before cursor |

#### focused mode (when a window is focused)
- all key events are sent to the focused window
- `Esc` unfocuses the window and returns to normal mode
  - cancellable

---

## autostart

to load apps automatically on startup, create an `autostart.conf` file.
ttydesktop checks these locations in this order:

1. `~/.config/ttydesktop/autostart.conf`
2. `/etc/ttydesktop/autostart.conf`
3. `./autostart.conf`

the file should contain one path to a `.so` file per line. lines starting with `#` are ignored (comments omg!!!!)

example:
```conf
# please load hookman first
hookman.so
# your other stuff goes after
clock.so
bouncy.so
```

---

## app search paths

if an app path does **not** start with `/` or `.`, ttydesktop searches in the following directories (in order):

1. `./` (current directory - libdl doesnt like filename.so it needs ./filename.so)
2. `./bin/`
3. `$TTYDESKTOP_PATH` (environment variable)
4. `~/.local/lib/ttydesktop/`
5. `/usr/local/lib/ttydesktop/`
6. `/usr/lib/ttydesktop/`

---

## included apps

the following example apps are built and placed in `bin/`:

| app | description |
|-----|-------------|
| `bouncy.so` | so you know the dvd logo right? well its a ball (supports `frames.so`) |
| `clock.so` | digital clock displayed in the status bar (requires `hookman.so`) |
| `example.so` | minimal template for creating new apps, on itself does NOTHING |
| `frames.so` | frame timing controller (fps, timing info), quite literally controls the speed of ttydesktop. use `+`/`-` or `q`/`e` to adjust target fps (+-5 or +-1). requires `hookman.so` |
| `hookman.so` | hooking everything since 2026 |
| `mandelbrot.so` | interactive mandelbrot set renderer |
| `shadows.so` | adds simple drop shadows to windows (requires `hookman.so`) |
| `terminal.so` | a terminal emulator. very very rough but it can run ttydesktop |

---

## hookman

hookman is a cool app that lets apps do stuff to the desktop.
when loaded, it provides:

- **hooks** - register callbacks `before`, `after`, or as overrides for:
  - window draws
  - desktop updates
  - status bar updates
  - other stuff
  - any custom hook points defined by apps if they use the api correctly
- **function exports** - apps can `export` functions, and other apps can `call` them by name

to use hooks, ensure `hookman.so` is loaded **before** any apps that depend on it

---

## license

ttydesktop is licensed under the **GNU General Public License v3.0**,
see the [LICENSE](LICENSE) file for details.
