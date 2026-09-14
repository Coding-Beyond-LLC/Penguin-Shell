# Penguin Shell

A simple shell build by me

## Build Instructions
```
mkdir [TARGET BUILD DIRECTORY]
cd [TARGET BUILD DIRECTORY]
cmake [ROOT SOURCE DIRECTORY]
make

```
The penguin executable should now be in the directory created with the TARGET BUILD DIRECTORY name.

Optional: Add TARGET BUILD DIRECTORY to PATH

## Requirements
* OS: Linux
* CMake: 3.10 or greater
* Readline: install via ```sudo apt install libreadline-dev```

## Usage
```
penguin
```
Launches the penguin shell

```
penguin -h
```
Prints out the list of built in commands for the penguin shell

```
penguin -f script.psh
```
Runs every line of `script.psh` the way `~/.penrc` runs at startup (no
history recording), then exits. See [demos/](demos/) for sample scripts to
try this with.

```
==========================
    P  E  N  G  U  I  N
==========================
user@host#\home\user\(•ᴗ•)ゝ COMMAND
```
Penguin utilizes the environment variables from the shell under which it's launched for now.
