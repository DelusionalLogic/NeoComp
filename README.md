# NeoComp (Compton)

__NeoComp__ is a fork of __Compton__, a compositor for X11

NeoComp is a (hopefully) fast and (hopefully) simple compositor for X11,
focused on delivering frames from the window to the framebuffer as
quickly as possible.

It's currently very much in development (Compton is extremely crufty and
rigid), so I don't expect it to be stable.

## Building

To build, make sure you have the dependencies (yeah I know) then run:

```bash
# Make the main program
$ make
# Make the man pages
$ make docs
# Install
$ make install
```

## Usage

The man pages are completely out of date, but still your best bet. Some
options from the man pages have been removed, and some added. The code
is your best bet.

## License

I don't know of the lineage behind Compton. All contributions made by me
are GPL. If any previous contributor would like to claim ownership over
some part and dispute the license, please open an issue.

NeoComp is licensed under GPLv3
