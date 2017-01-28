synacor-emu
===========

This is an Synacor challenge emulator. It is a work in progress.

Building
========

You will need `glib` and its development files (specifically, package config
`.pc` files) installed. On Fedora, you can install these with `yum install
glib2 glib2-devel`. On Ubuntu, use `apt-get install libglib2.0-0
libglib2.0-dev`. You will also need OpenSSL for DES instruction support.

`make` will build the emulator, `synacor-emu`.

This is not packaged for installation at this time. Patches welcome.

Simple Emulation
================

Invoke `synacor-emu <romfile>`.

Tracing
=======

Use the `-t=TRACE_FILE` option to `synacor-emu` to log a binary trace of all
instructions executed to `TRACE_FILE`. Use the `-x` flag to dump in hex format
instead of binary.

License
=======

cemeyer/synacor-emu is released under the terms of the MIT license. See
LICENSE. Basically, do what you will with it.

Hacking
=======

Most of the emulator lives in `main.c`; instruction implementations are in
`instr.c`. There are instruction emulation unit tests in `check_instr.c`.
