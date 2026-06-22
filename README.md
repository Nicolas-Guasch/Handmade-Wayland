# HANDMADE WAYLAND

A small proof of concept for creating and managing a [Wayland](https://wayland.freedesktop.org/) client in C.
Based in the Handmade Hero project by Casey Muratori.
Some minimal C++ features might be used.

The idea and implementation for handling the communication with the composer
without **libwayland**, and the marshalling and unmarshalling functions, is
directly taken from Philippe Gaultier's [blog](https://gaultier.github.io/blog/wayland_from_scratch.html). Distributed under the BSD-3 License. Thanks for the guidance!

While this should work on any compositor, I'm aiming for the minimal tiling compositor dwl. While I'm not doing anything compatibility breaking, might not test this anywhere else.

## Progress
- Established connection with the compositor.
- Created a shared memory file where to send a bitmap to the compositor.
- Created a registry object to manage global objects.
- Handle initial chat with the compositor.
- Binded all the needed global objects.
- Get a wl_surface ready to attach a buffer.

## Current goals
- Request from wl_shm a wl_shm_pool for our shared memory. Create a wl_buffer from it.
- Write some bitmap to the shared memory file.
- Attach the buffer to the surface and commit it.
- Get anything visible on screen.
- Handle resizing events.
