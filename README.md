# HANDMADE WAYLAND

A small proof of concept for creating and managing a [Wayland](https://wayland.freedesktop.org/) client in C.
Based in the Handmade Hero project by Casey Muratori.
Some minimal C++ features might be used.

The idea and implementation for handling the communication with the composer
without **libwayland**, and the marshalling and unmarshalling functions, is
directly taken from Philippe Gaultier's [blog](https://gaultier.github.io/blog/wayland_from_scratch.html). Distributed under the BSD-3 License. Thanks for the guidance!

## Progress
- Established connection with the compositor.
- Created a registry object to manage global objects.

## Current goals
- Initialize and launch a Wayland window.
- Reserve a buffer in memory and write a bitmap to it.
- Pass the buffer to be displayed on screen.
