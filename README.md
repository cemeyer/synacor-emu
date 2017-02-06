synacor-emu
===========

This is an Synacor challenge emulator.  It is complete.

Building
========

`make` (`gmake` on BSD) will build the emulator, `synacor-emu`.

Simple Emulation
================

Invoke `synacor-emu <romfile>`.

Tracing
=======

Use the `-t=TRACE_FILE` option to `synacor-emu` to log a binary trace of all
instructions executed to `TRACE_FILE`.  Use the `-x` flag to dump in hex format
instead of binary.

Save and Restore
================

Send a USR1 signal to the emulator to save the current machine state to
"synacor.save."  Restore a saved machine state with the `-r` flag, like:
`synacor-emu -r foo.save`.

License
=======

cemeyer/synacor-emu is released under the terms of the MIT license.  See
LICENSE.  Basically, do what you will with it.

Hacking
=======

Most of the emulator lives in `main.c`; instruction implementations are in
`instr.c`.  There are instruction emulation unit tests in `check_instr.c`.
