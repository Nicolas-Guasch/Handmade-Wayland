#include <stdint.h> //types
#include <stdlib.h> //getenv
#include <errno.h> //EINVAL
#include <assert.h>
#include <sys/un.h> //sockaddr_un
#include <sys/socket.h> //AF_UNIX SOCK_STREAM socket sockaddr connect
#include "stringView.c"

#define internal static
#define local_persist static
#define global_variable static

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

global_variable uint32 wayland_display_object_id = 1;
global_variable uint16 wayland_wl_registry_event_global = 0;
global_variable uint16 wayland_shm_pool_event_format = 0;
global_variable uint16 wayland_wl_buffer_event_release = 0;
global_variable uint16 wayland_xdg_wm_base_event_ping = 0;
global_variable uint16 wayland_xdg_toplevel_event_configure = 0;
global_variable uint16 wayland_xdg_toplevel_event_close = 1;
global_variable uint16 wayland_sdg_surface_event_configure = 0;
global_variable uint16 wayland_wl_display_get_registry_opcode = 1;
global_variable uint16 wayland_wl_registry_bind_opcode = 0;
global_variable uint16 wayland_wl_compositor_create_surface_opcode = 0;
global_variable uint16 wayland_xdg_wm_base_pong_opcode = 3;
global_variable uint16 wayland_xdg_surface_ack_configure_opcode = 4;
global_variable uint16 wayland_wl_shm_create_pool_opcode = 0;
global_variable uint16 wayland_xdg_wm_base_get_xdg_surface_opcode = 2;
global_variable uint16 wayland_wl_shm_pool_create_buffer_opcode = 0;
global_variable uint16 wayland_wl_surface_attach_opcode = 1;
global_variable uint16 wayland_xdg_surface_get_toplevel_opcode = 1;
global_variable uint16 wayland_wl_surface_commit_opcode=6;
global_variable uint16 wayland_wl_display_error_event = 0;
global_variable uint32 wayland_format_xrgb8888 = 1;
global_variable uint32 wayland_header_size = 8;
global_variable uint32 color_channels = 4;

#define cstring_len(s) (sizeof(s) - 1)
#define roundup_4(n) (((n) + 3) & -4)

/*
 * Unix domain socket specification
struct sockaddr_un {
    sa_family_t sun_family; // AF_UNIX 
    char        sun_path[108]; // Pathname 
};
*/

struct String {
    char *data;
    size_t count;
};

void str_append(String *dest, String_View *src) {
    memcpy(dest->data + dest->count, src->data, src->count);
    dest->count += src->count;
}

internal int wayland_display_connect() {
    String_View xdg_runtime_dir = sv(getenv("XDG_RUNTIME_DIR"));
    if (xdg_runtime_dir.data == NULL) {
        // TODO: Tighten up.
        return EINVAL;
    }

    struct sockaddr_un addr = {
        .sun_family = AF_UNIX,
        .sun_path = {0}
    };
    assert(xdg_runtime_dir.count < cstring_len(addr.sun_path));
    String socket_path = {.data = addr.sun_path, .count = 0};
    
    str_append(&socket_path, &xdg_runtime_dir);
    socket_path.data[socket_path.count++] = '/';

    String_View wayland_display = sv(getenv("WAYLAND_DISPLAY"));
    if (wayland_display.data == NULL) {
        String_View wayland_display_default = sv("wayland-0");
        assert(socket_path.count + wayland_display_default.count 
                <= cstring_len(addr.sun_path));
        str_append(&socket_path, &wayland_display_default);
    } else {
        assert(socket_path.count + wayland_display.count 
                <= cstring_len(addr.sun_path));
        str_append(&socket_path, &wayland_display);
    }
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1){
        // TODO: Tighten up.
        exit(errno);
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        // TODO: Tighten up.
        exit(errno);
    }

    return fd;
}

int main(int argc, char * argv[]) {
    (void)argc;
    (void)argv;

    // Ope
    int fd = wayland_display_connect();
    return 0;
}
