#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdint.h> //types
#include <stdlib.h> //getenv rand
#include <errno.h> //EINVAL
#include <assert.h> //assert
#include <sys/un.h> //sockaddr_un
#include <sys/socket.h> //AF_UNIX SOCK_STREAM socket sockaddr connect
#include <sys/mman.h> // memfd_create mmap
#include <fcntl.h> // fcntl F_ADD_SEALS F_SEAL_SEAL F_SEAL_SHRINK
#include <unistd.h> //ftruncate
#include "stringView.c" // String_View sv

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

global_variable uint32 bytes_per_pixel = 4;
struct shm_buffer {
    uint32 wl_buffer;
    uint32 wl_shm_pool;
    uint32 width;
    uint32 height;
    uint32 pitch;
    size_t size;
    int fd;
    uint8 *data;
    bool busy;
    shm_buffer *next;
};
typedef struct shm_buffer shm_buffer;

enum state_state_t {
    STATE_NONE,
    STATE_SURFACE_ACKED_CONFIGURE,
    STATE_SURFACE_ATTACHED,
};
typedef enum state_state_t state_state_t;

struct state_t {
    uint32 wl_registry;
    uint32 wl_shm;
    uint32 xdg_wm_base;
    uint32 xdg_surface;
    uint32 wl_compositor;
    uint32 wl_surface;
    uint32 xdg_toplevel;
    shm_buffer *current_buffer;
    shm_buffer *retired_buffers;
    state_state_t state;
};
typedef struct state_t state_t;

global_variable uint32 wayland_current_id = 1;
global_variable bool running = true;

// Wayland protocol numeric values
global_variable uint32 wayland_header_size = 8;

global_variable uint32 wayland_display_object_id = 1;
global_variable uint16 wayland_wl_display_get_registry_opcode = 1;
global_variable uint16 wayland_wl_display_error_event = 0;

global_variable uint16 wayland_wl_registry_bind_opcode = 0;
global_variable uint16 wayland_wl_registry_event_global = 0;

global_variable uint16 wayland_wl_buffer_event_release = 0;

global_variable uint16 wayland_wl_surface_attach_opcode = 1;
global_variable uint16 wayland_wl_surface_commit_opcode=6;

global_variable uint16 wayland_wl_shm_create_pool_opcode = 0;
global_variable uint16 wayland_shm_pool_event_format = 0;

global_variable uint16 wayland_wl_shm_pool_create_buffer_opcode = 0;

global_variable uint16 wayland_wl_compositor_create_surface_opcode = 0;

global_variable uint16 wayland_xdg_wm_base_event_ping = 0;
global_variable uint16 wayland_xdg_toplevel_event_configure = 0;
global_variable uint16 wayland_xdg_toplevel_event_close = 1;
global_variable uint16 wayland_xdg_surface_get_toplevel_opcode = 1;
global_variable uint16 wayland_xdg_wm_base_pong_opcode = 3;
global_variable uint16 wayland_xdg_surface_ack_configure_opcode = 4;
global_variable uint16 wayland_xdg_wm_base_get_xdg_surface_opcode = 2;
global_variable uint16 wayland_sdg_surface_event_configure = 0;

global_variable uint32 wayland_format_xrgb8888 = 1;

#define cstring_len(s) (sizeof(s) - 1)
#define roundup_4(n) (((n) + 3) & -4)

/*
 * Unix domain socket specification
struct sockaddr_un {
    sa_family_t sun_family; // AF_UNIX 
    char        sun_path[108]; // Pathname 
};
*/

struct String_Buffer {
    char *data;
    size_t count;
    size_t cap;
};

void str_append(String_Buffer *dest, String_View *src) {
    assert(dest->count + src->count <= dest->cap);
    memcpy(dest->data + dest->count, src->data, src->count);
    dest->count += src->count;
}

void str_append_n(String_Buffer *dest, String_View *src, size_t n) {
    assert(dest->count + n <= dest->cap);
    assert(src->count >= n);
    memcpy(dest->data + dest->count, src->data, n);
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
    String_Buffer socket_path = {
        .data = addr.sun_path, 
        .count = 0, 
        .cap = cstring_len(addr.sun_path)
    };
    assert(xdg_runtime_dir.count < socket_path.cap);
    
    str_append(&socket_path, &xdg_runtime_dir);
    socket_path.data[socket_path.count++] = '/';

    String_View wayland_display = sv(getenv("WAYLAND_DISPLAY"));
    if (wayland_display.data == NULL) {
        String_View wayland_display_default = sv("wayland-0");
        assert(socket_path.count + wayland_display_default.count 
                <= socket_path.cap);
        str_append(&socket_path, &wayland_display_default);
    } else {
        assert(socket_path.count + wayland_display.count 
                <= socket_path.cap);
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

internal void buf_write_u32(String_Buffer *buf, uint32 x) {
    assert(buf->count + sizeof(x) <= buf->cap);
    assert(((size_t)buf->data + buf->count) % sizeof(x) == 0); // Alignment check.

    *(uint32 *)(buf->data + buf->count) = x;
    buf->count += sizeof(x);
}
internal void buf_write_u16(String_Buffer *buf, uint16 x) {
    assert(buf->count + sizeof(x) <= buf->cap);
    assert(((size_t)buf->data + buf->count) % sizeof(x) == 0); // Alignment check.

    *(uint16 *)(buf->data + buf->count) = x;
    buf->count += sizeof(x);
}
internal void buf_write_string(String_Buffer *buf, String_View *src) {
    buf_write_u32(buf, src->count);

    assert(buf->count + src->count <= buf->cap);
    str_append(buf, src);
    // Add word alignement padding
    while (buf->count & 3) buf->data[buf->count++] = 0;
}

internal uint32 buf_read_u32(String_View *buf) {
    assert(buf->count >= sizeof(uint32));
    assert((size_t)buf->data % sizeof(uint32) == 0);

    uint32 result = *(uint32 *)buf->data;
    buf->data += sizeof(result);
    buf->count -= sizeof(result);

    return result;
}
internal uint16 buf_read_u16(String_View *buf) {
    assert(buf->count >= sizeof(uint16));
    assert((size_t)buf->data % sizeof(uint16) == 0);

    uint16 result = *(uint16 *)buf->data;
    buf->data += sizeof(result);
    buf->count -= sizeof(result);

    return result;
}
internal void buf_read_n(String_View *buf, String_Buffer *dst, size_t n) {
    str_append_n(dst, buf, n);
    buf->data += n;
    buf->count -= n;
}

internal uint32 wayland_wl_display_get_registry(int fd) {
    // Create message buffer
    char msg[128] = "";
    String_Buffer msg_buf = {
        .data = msg,
        .count = 0,
        .cap = sizeof(msg)
    };

    // Write wl_display id to buffer
    buf_write_u32(&msg_buf, wayland_display_object_id); 
    
    // Write get_registry opcode to buffer
    buf_write_u16(&msg_buf, wayland_wl_display_get_registry_opcode);

    // Write the size of the message to buffer
    uint16 msg_announced_size =
        wayland_header_size + sizeof(wayland_current_id);
    assert((msg_announced_size & 3) == 0); // Word aligned.
    buf_write_u16(&msg_buf, msg_announced_size);
    
    wayland_current_id++;
    // Write the argument, the id for the new wl_registry, to buffer
    buf_write_u32(&msg_buf, wayland_current_id);

    // Send message to socket
    if ((int64)msg_buf.count != 
            send(fd, msg_buf.data, msg_buf.count, MSG_DONTWAIT)) {
        // TODO: Tighten up.
        exit(errno);
    }
    
    return wayland_current_id;
}

//TODO: Request compositor to destroy wl_shm_pool wl_buffer.
internal void destroy_buffer(shm_buffer *buf) {
    assert(!buf->busy);
    if (munmap(buf->data, buf->size) == -1) {
        // TODO: Tighten up.
        exit(errno);
    }
    if (buf->fd != -1 && close(buf->fd) == -1) {
        // TODO: Tighten up.
        exit(errno);
    }
    if (buf->wl_buffer){}   // TODO: Destroy wl_buffer.
    if (buf->wl_shm_pool){} // TODO: Destroy wl_shm_pool.
                            // When would these happen???
                            // busy is already false.
    free(buf);
}

// TODO: handle this event.
internal void on_wl_buffer_release(state_t *state, uint32 buffer_id) {
    if (state->current_buffer && buffer_id == state->current_buffer->wl_buffer) {
        state->current_buffer->busy = false;
        return;
    }
    shm_buffer *previous = NULL;
    shm_buffer *current = state->retired_buffers;
    while (current && current->wl_buffer != buffer_id) {
        previous = current;
        current = current->next;
    }
    if (current) {
        if (!previous) {
            state->retired_buffers = current->next;
        }
        else {
            previous->next = current->next;
        }
        destroy_buffer(current);
    }
}

internal bool create_shared_memory_file(shm_buffer *new_shm) {
    int fd = memfd_create("handmade-wayland-buffer", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd == -1) {
        // TODO: Tighten up.
        exit(errno);
    }

    if (ftruncate(fd, new_shm->size) == -1) {
        // TODO: Tighten up.
        exit(errno);
    }

    if (fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_SEAL) == -1) {
        // TODO: Process errors.
        exit(errno);
    }

    uint8 *shm_pool_data = (uint8 *)
        mmap(NULL, new_shm->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if ((shm_pool_data != (void*)-1) && (shm_pool_data != NULL)) {
        new_shm->fd = fd;
        new_shm->data = shm_pool_data;
        return true;
    }
    else {
        close(fd);
        return false;
    }
}

internal void resizeWindowBuffer(state_t *state, uint32 width, uint32 height) {
    shm_buffer *current = state->current_buffer;
    uint32 pitch = width * bytes_per_pixel;
    size_t size = pitch * height; // TODO: Clamp pitch, size to [1,max]?
    if (current && 
            current->width == width &&
            current->height == height &&
            current->pitch == pitch) {
        return;
    }
    shm_buffer *buf = (shm_buffer *) malloc(sizeof(shm_buffer));
    if (!buf) return;
    *buf = (shm_buffer) {
        .wl_buffer = 0,
        .wl_shm_pool = 0,
        .width = width,
        .height = height,
        .pitch = pitch,
        .size = size,
        .fd = -1,
        .data = NULL,
        .busy = false, // NOTE: Until commited.
        .next = NULL
    };
    if (create_shared_memory_file(buf)) {
        if (current) {
            if (!current->busy) {
                destroy_buffer(current);
            }
            else {
                current->next = state->retired_buffers;
                state->retired_buffers = current;
            }
        }
        state->current_buffer = buf;
        // TODO: On wl_shm.create_pool, close fd.
    }
    else free(buf);
}

int main(int argc, char * argv[]) {
    (void)argc;
    (void)argv;

    // Open a UNIX domain socket.
    int fd = wayland_display_connect();

    int defaultWidth = 1280;
    int defaultHeight = 720;

    state_t clientState = {
        // Request a registry from wl_display to list and bind global objects.
        // wl_display::get_registry
        .wl_registry = wayland_wl_display_get_registry(fd),
    };

    resizeWindowBuffer(&clientState, defaultWidth, defaultHeight);

    while (running) {
        // TODO:
        // Read message into buffer.
        // While buffer's not empty, handle compositor's message.
        // If bind phase complete, create surface.
        // If ack configure confirmed:
        //     Create wl_shm_pool if there's none.
        //     Create wl_buffer if there's none.
        //     Render frame
        //     wl_surface_attach
        //     wl_surface_commit
        // Cleanup:

    }

    return 0;
}
