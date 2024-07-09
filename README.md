Eigenvalue
==========

A minimalistic matrix cli built using [libcmatrix][]. This is likely not very
useful if you're not debugging the library.

To configure the account to use create a `~/.config/eigenvalue/accounts.cfg`

```
[matrix-00]
username=@youruser:example.org
password=yourpassword
```
Building
--------

To build

```sh
sudo apt install build-essential libglib2.0-dev libedit-dev libjson-glib-dev libsoup-3.0-dev
git clone https://github.com/agx/eigenvalue/
cd eigenvalue
meson setup _build
meson compile -C _build
```

Running
-------

To start the program use

```
_build/run
```

Usage
-----

Try `/help` at the prompt

[libcmatrix]: https://source.puri.sm/Librem5/libcmatrix
