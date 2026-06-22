#ifndef _GNU_SOURCE
#define _GNU_SOURCE //memfd_create is Linux only
#endif
#include <stdio.h> //standard I/O
#include <stdint.h> //types
#include <stdlib.h> //getenv
#include <errno.h> //EINVAL
#include <assert.h> //assert
#include <sys/un.h> //sockaddr_un
#include <sys/socket.h> //AF_UNIX SOCK_STREAM socket sockaddr connect
#include <sys/mman.h> // memfd_create mmap
#include <sys/uio.h> // iovec
#include <poll.h> // poll pollfd
#include <fcntl.h> // fcntl F_ADD_SEALS F_SEAL_SEAL F_SEAL_SHRINK
#include <unistd.h> //ftruncate
#include "stringView.c" // String_View sv

#define SV_FMT "%.*s" 
#define SV_Arg(s) (int)(s).count, (s).data

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
    uint32 wl_compositor;
    uint32 wl_surface;
    uint32 xdg_surface;
    uint32 xdg_toplevel;
    shm_buffer *current_buffer;
    shm_buffer *retired_buffers;
    state_state_t state;
};
typedef struct state_t state_t;

global_variable uint32 wayland_current_id = 1;
global_variable bool running = true;

// Wayland protocol numeric values
// wayland.app for clean documentation
// opcodes and events start from 0, in documented order
#define wayland_header_size 8

#define wayland_display_object_id 1
#define wayland_wl_display_sync_opcode 0
#define wayland_wl_display_get_registry_opcode 1
#define wayland_wl_display_error_event 0
#define wayland_wl_display_delete_id_event 1

#define wayland_wl_registry_bind_opcode 0
#define wayland_wl_registry_event_global 0
#define wayland_wl_registry_event_global_remove 1

#define wayland_wl_compositor_create_surface_opcode 0

#define wayland_wl_shm_pool_create_buffer_opcode 0

#define wayland_wl_shm_create_pool_opcode 0
#define wayland_wl_shm_event_format 0

#define wayland_wl_buffer_event_release 0

#define wayland_wl_surface_attach_opcode 1
#define wayland_wl_surface_commit_opcode 6
#define wayland_wl_surface_event_enter 0
#define wayland_wl_surface_event_leave 1
#define wayland_wl_surface_event_preferred_buffer_scale 2

// XDG shell interfaces numeric values
#define wayland_xdg_wm_base_get_xdg_surface_opcode 2
#define wayland_xdg_wm_base_pong_opcode 3
#define wayland_xdg_wm_base_event_ping 0

#define wayland_xdg_surface_get_toplevel_opcode 1
#define wayland_xdg_surface_ack_configure_opcode 4
#define wayland_xdg_surface_event_configure 0

#define wayland_xdg_toplevel_event_configure 0
#define wayland_xdg_toplevel_event_close 1
#define wayland_xdg_toplevel_event_configure_bounds 2
#define wayland_xdg_toplevel_event_wm_capabilities 3


#define wayland_format_xrgb8888 1

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
    dest->data[dest->count]=0;
}

void str_append_n(String_Buffer *dest, String_View *src, size_t n) {
    assert(dest->count + n <= dest->cap);
    assert(src->count >= n);
    memcpy(dest->data + dest->count, src->data, n);
    dest->count += n;
    dest->data[dest->count]=0;
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

internal void set_socket_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

struct Byte_Buffer {
    uint8 *data;
    size_t count;
    size_t cap;
};
typedef struct Byte_Buffer Byte_Buffer;

struct Byte_View {
    const uint8 *data;
    size_t count;
};
typedef struct Byte_View Byte_View;

void buf_append(Byte_Buffer *dest, Byte_View *src) {
    assert(dest->count + src->count <= dest->cap);
    memcpy(dest->data + dest->count, src->data, src->count);
    dest->count += src->count;
}

void buf_append_n(Byte_Buffer *dest, Byte_View *src, size_t n) {
    assert(dest->count + n <= dest->cap);
    assert(src->count >= n);
    memcpy(dest->data + dest->count, src->data, n);
    dest->count += n;
}

internal void buf_write_u32(Byte_Buffer *buf, uint32 x) {
    assert(buf->count + sizeof(x) <= buf->cap);
    assert(((size_t)buf->data + buf->count) % sizeof(x) == 0); // Alignment check.

    *(uint32 *)(buf->data + buf->count) = x;
    buf->count += sizeof(x);
}
internal void buf_write_u16(Byte_Buffer *buf, uint16 x) {
    assert(buf->count + sizeof(x) <= buf->cap);
    assert(((size_t)buf->data + buf->count) % sizeof(x) == 0); // Alignment check.

    *(uint16 *)(buf->data + buf->count) = x;
    buf->count += sizeof(x);
}
internal void buf_write_string(Byte_Buffer *buf, String_View *src) {
    buf_write_u32(buf, src->count+1);
    Byte_View str_bytes = {.data = (uint8 *)src->data, .count = src->count};
    assert(buf->count + roundup_4(str_bytes.count+1) <= buf->cap);
    buf_append(buf, &str_bytes);
    // Add null terminator
    buf->data[buf->count++] = 0;
    // Add word alignement padding
    while (buf->count & 3) buf->data[buf->count++] = 0;
}

internal uint32 buf_read_u32(Byte_View *buf) {
    assert(buf->count >= sizeof(uint32));
    assert((size_t)buf->data % sizeof(uint32) == 0);

    uint32 result = *(uint32 *)buf->data;
    buf->data += sizeof(result);
    buf->count -= sizeof(result);

    return result;
}
internal uint16 buf_read_u16(Byte_View *buf) {
    assert(buf->count >= sizeof(uint16));
    assert((size_t)buf->data % sizeof(uint16) == 0);

    uint16 result = *(uint16 *)buf->data;
    buf->data += sizeof(result);
    buf->count -= sizeof(result);

    return result;
}
internal void buf_read_n(Byte_View *buf, Byte_Buffer *dst, size_t n) {
    buf_append_n(dst, buf, n);
    buf->data += n;
    buf->count -= n;
}

//WAYLAND REQUESTS:
//wl_display
//wl_display.sync 
//pending
//wl_display.get_registry
internal uint32 wayland_wl_display_get_registry(int fd) {
    // Create message buffer
    uint8 msg[128] = "";
    Byte_Buffer msg_buf = {
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
    if ((ssize_t)msg_buf.count != 
            send(fd, msg_buf.data, msg_buf.count, MSG_DONTWAIT)) {
        // TODO: Tighten up.
        exit(errno);
    }
    
    return wayland_current_id;
}
//wl_registry
//wl_registry.bind
internal uint32 wayland_wl_registry_bind(int fd, state_t *state, 
        uint32 name, String_View *interface, uint32 version) {
    assert(state->wl_registry > 0);
    fprintf(stderr, "Binding " SV_FMT "\n", SV_Arg(*interface));
    uint8 msg[128] = "";
    Byte_Buffer msg_buf = {
        .data = msg,
        .count = 0,
        .cap = sizeof(msg)
    };

    buf_write_u32(&msg_buf, state->wl_registry);

    buf_write_u16(&msg_buf, wayland_wl_registry_bind_opcode);

    uint16 msg_announced_size = 
        wayland_header_size + sizeof(name) 
        + sizeof(uint32) + roundup_4(interface->count+1) // string size and padded chars
        + sizeof(version) + sizeof(wayland_current_id);
    assert((msg_announced_size & 3) == 0);
    buf_write_u16(&msg_buf, msg_announced_size);

    buf_write_u32(&msg_buf, name);
    // NOTE: new_id without defined interface
    // requires interface string name and version number
    buf_write_string(&msg_buf, interface);
    buf_write_u32(&msg_buf, version);
    wayland_current_id++;
    buf_write_u32(&msg_buf, wayland_current_id);

    assert((msg_buf.count & 3) == 0);
    if ((ssize_t)msg_buf.count !=
            send(fd, msg_buf.data, msg_buf.count, MSG_DONTWAIT)) {
        // TODO: Tighten up.
        fprintf(stderr, "Binding failed\n");
        exit(errno);
    }
    fprintf(stderr, "Binding successful\n");

    return wayland_current_id;
}
//wl_compositor
//wl_compositor.create_surface
internal uint32 wayland_wl_compositor_create_surface(int fd, state_t *state) {
    assert(state->wl_compositor > 0);
    uint8 msg[128] = "";
    Byte_Buffer msg_buf = {
        .data = msg,
        .count = 0,
        .cap = sizeof(msg)
    };

    buf_write_u32(&msg_buf, state->wl_compositor);

    buf_write_u16(&msg_buf, wayland_wl_compositor_create_surface_opcode);

    uint16 msg_announced_size =
        wayland_header_size + sizeof(wayland_current_id);
    //8+4 is aligned already.
    buf_write_u16(&msg_buf, msg_announced_size);
    wayland_current_id++;
    buf_write_u32(&msg_buf, wayland_current_id);

    if ((ssize_t)msg_buf.count !=
            send(fd, msg_buf.data, msg_buf.count, MSG_DONTWAIT)) {
        // TODO: Tighten up.
        exit(errno);
    }
    return wayland_current_id;
}
//wl_surface
//wl_surface_commit
internal void wayland_wl_surface_commit(int fd, state_t *state) {
    assert(state->wl_surface > 0);
    uint8 msg[128] = "";
    Byte_Buffer msg_buf = {
        .data = msg,
        .count = 0,
        .cap = sizeof(msg)
    };

    buf_write_u32(&msg_buf, state->wl_surface);
    buf_write_u16(&msg_buf, wayland_wl_surface_commit_opcode);
    uint16 msg_announced_size = wayland_header_size;
    buf_write_u16(&msg_buf, msg_announced_size);
    if ((ssize_t)msg_buf.count !=
            send(fd, msg_buf.data, msg_buf.count, MSG_DONTWAIT)) {
        exit(errno);
    }
}


//XDG shell requests

//xdg_wm_base
//xdg_wm_base_get_xdg_surface
internal uint32 wayland_xdg_wm_base_get_xdg_surface(int fd, state_t *state) {
    assert(state->xdg_wm_base > 0);
    assert(state->wl_surface > 0);

    uint8 msg[128] = "";
    Byte_Buffer msg_buf = {
        .data = msg,
        .count = 0,
        .cap = sizeof(msg)
    };

    buf_write_u32(&msg_buf, state->xdg_wm_base);

    buf_write_u16(&msg_buf, wayland_xdg_wm_base_get_xdg_surface_opcode);
    
    uint16 msg_announced_size =
        wayland_header_size + sizeof(wayland_current_id) + 
        sizeof(state->wl_surface);
    //8 + 4 + 4
    buf_write_u16(&msg_buf, msg_announced_size);
    wayland_current_id++;
    buf_write_u32(&msg_buf, wayland_current_id);
    buf_write_u32(&msg_buf, state->wl_surface);

    if ((ssize_t)msg_buf.count !=
            send(fd, msg_buf.data, msg_buf.count, MSG_DONTWAIT)) {
        exit(errno);
    }
    return wayland_current_id;
}
//xdg_wm_base_pong
internal void wayland_xdg_wm_base_pong(int fd, state_t *state, uint32 ping) {
    assert(state->xdg_wm_base > 0);
    fprintf(stderr, "xdg_wm_base ping\n");
    
    uint8 msg[128] = "";
    Byte_Buffer msg_buf = {
        .data = msg,
        .count = 0,
        .cap = sizeof(msg)
    };

    buf_write_u32(&msg_buf, state->xdg_wm_base);

    buf_write_u16(&msg_buf, wayland_xdg_wm_base_pong_opcode);
    
    uint16 msg_announced_size =
        wayland_header_size + sizeof(ping);
    buf_write_u16(&msg_buf, msg_announced_size);
    buf_write_u32(&msg_buf, ping);

    if ((ssize_t)msg_buf.count != 
            send(fd, msg_buf.data, msg_buf.count, MSG_DONTWAIT)) {
        exit(errno);
    }
}
//xdg_surface
//xdg_surface.get_toplevel
internal uint32 wayland_xdg_surface_get_toplevel(int fd, state_t *state) {
    assert(state->xdg_surface > 0);
    
    uint8 msg[128];
    Byte_Buffer msg_buf = {
        .data = msg,
        .count = 0,
        .cap = sizeof(msg)
    };

    buf_write_u32(&msg_buf, state->xdg_surface);
    
    buf_write_u16(&msg_buf, wayland_xdg_surface_get_toplevel_opcode);

    uint16 msg_announced_size =
        wayland_header_size + sizeof(wayland_current_id);
    //8 + 4
    buf_write_u16(&msg_buf, msg_announced_size);
    wayland_current_id++;
    buf_write_u32(&msg_buf, wayland_current_id);

    if ((ssize_t)msg_buf.count !=
            send(fd, msg_buf.data, msg_buf.count, MSG_DONTWAIT)) {
        exit(errno);
    }
    return wayland_current_id;
}
//xdg_surface.ack_configure
internal void wayland_xdg_surface_ack_configure(int fd, state_t *state,
        uint32 serial) {
    assert(state->xdg_surface > 0);

    uint8 msg[128];
    Byte_Buffer msg_buf = {
        .data = msg,
        .count = 0,
        .cap = sizeof(msg)
    };

    buf_write_u32(&msg_buf, state->xdg_surface);

    buf_write_u16(&msg_buf, wayland_xdg_surface_ack_configure_opcode);

    uint16 msg_announced_size =
        wayland_header_size + sizeof(serial);
    buf_write_u16(&msg_buf, msg_announced_size);
    buf_write_u32(&msg_buf, serial);

    if ((ssize_t)msg_buf.count !=
            send(fd, msg_buf.data, msg_buf.count, MSG_DONTWAIT)) {
        exit(errno);
    }
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

#define WAYLAND_MAX_FDS_PER_RECVMSG 60 //libwayland-server harcodes the limit to 28
#define WAYLAND_FD_QUEUE_CAP 512

struct Fd_Queue {
    int fds[WAYLAND_FD_QUEUE_CAP];
    size_t head;
    size_t count;
    size_t cap;
};
typedef struct Fd_Queue Fd_Queue;


internal void init_fd_queue(Fd_Queue *fdq) {
    fdq->head = 0;
    fdq->count = 0;
    fdq->cap = WAYLAND_FD_QUEUE_CAP;
}

internal bool Fd_Queue_Push(int fd, Fd_Queue *fdq) {
    if (fdq->count == fdq->cap) return false;
    int next = (fdq->head+fdq->count) < fdq->cap ?
        (fdq->head+fdq->count) :
        (fdq->head+fdq->count) - fdq->cap;

    fdq->fds[next] = fd;  
    fdq->count++;
    return true;
}

internal int Fd_Queue_Pop(Fd_Queue *fdq) {
    assert(fdq->count > 0);
    int result = fdq->fds[fdq->head];
    fdq->head++;
    if (fdq->head == fdq->cap) {
        fdq->head = 0;
    }
    fdq->count--;
    return result;
}

#define WAYLAND_COMP_MSG_BUF_CAP 4096
struct Compositor_Message_Buf {
    uint8 read_buf[WAYLAND_COMP_MSG_BUF_CAP];
    uint8 *head;
    size_t count;
    size_t cap;
    Fd_Queue fd_queue;
};
typedef struct Compositor_Message_Buf Compositor_Message_Buf;

internal void init_compositor_message_buf(Compositor_Message_Buf *buf) {
    buf->head = buf->read_buf;
    buf->count = 0;
    buf->cap = sizeof(buf->read_buf);
    memset(buf->read_buf, 0, buf->cap);
    init_fd_queue(&buf->fd_queue);
}

enum WAYLAND_INTERFACE {
    UNKNOWN_INTERFACE,
    WL_DISPLAY_INTERFACE,
    WL_REGISTRY_INTERFACE,
    WL_CALLBACK_INTERFACE,
    WL_COMPOSITOR_INTERFACE,
    WL_SHM_POOL_INTERFACE,
    WL_SHM_INTERFACE,
    WL_BUFFER_INTERFACE,
    WL_SURFACE_INTERFACE,
    WL_KEYBOARD_INTERFACE,
    XDG_WM_BASE_INTERFACE
};
typedef enum WAYLAND_INTERFACE WAYLAND_INTERFACE;

internal WAYLAND_INTERFACE identify_interface(String_View *interface) {
    char wl_shm_interface[] = "wl_shm";
    char xdg_wm_base_interface[] = "xdg_wm_base";
    char wl_compositor_interface[] = "wl_compositor";
    if (strncmp(interface->data, wl_shm_interface, interface->count) == 0) return WL_SHM_INTERFACE;
    if (strncmp(interface->data, xdg_wm_base_interface, interface->count) == 0) return XDG_WM_BASE_INTERFACE;
    if (strncmp(interface->data, wl_compositor_interface, interface->count) == 0) return WL_COMPOSITOR_INTERFACE;
    return UNKNOWN_INTERFACE;
}

internal void wayland_handle_message(int fd, state_t *state, 
        Byte_View *msg, Fd_Queue *fdq) {
    assert(msg->count >= 8);

    uint32 object_id = buf_read_u32(msg);
    assert(object_id <= wayland_current_id);

    uint16 opcode = buf_read_u16(msg);

    uint16 announced_size = buf_read_u16(msg);
    assert(roundup_4(announced_size) <= announced_size);

    uint32 header_size =
        sizeof(object_id) + sizeof(opcode) + sizeof(announced_size);
    assert(announced_size <= header_size + msg->count);

    if (object_id == wayland_display_object_id) {
        switch (opcode) {
            case wayland_wl_display_error_event:
                {
                    uint32 target_object_id = buf_read_u32(msg);
                    uint32 code = buf_read_u32(msg);
                    uint8 error_buf[512] = "";
                    Byte_Buffer error_msg = {
                        .data = error_buf,
                        .count = 0,
                        .cap = sizeof(error_buf)
                    };
                    uint32 error_len = roundup_4(buf_read_u32(msg));
                    buf_read_n(msg, &error_msg, error_len);
                    
                    fprintf(stderr, 
                            "fatal error: target_object_id=%u code=%u error="
                            SV_FMT "\n",
                            target_object_id, code, SV_Arg(error_msg));
                    running = false;
                } break;
        }
    } else if (object_id == state->wl_registry) {
        fprintf(stderr,"wl_registry event:");
        switch (opcode) {
            case wayland_wl_registry_event_global: 
                {
                    uint32 name = buf_read_u32(msg);
                    uint32 interface_len = buf_read_u32(msg);
                    uint32 padded_interface_len = roundup_4(interface_len);

                    uint8 interface[512] = "";
                    Byte_Buffer interface_buf = {
                        .data = interface,
                        .count = 0,
                        .cap = sizeof(interface)
                    };
                    assert(padded_interface_len <= interface_buf.cap);

                    buf_read_n(msg, &interface_buf, padded_interface_len);
                    assert(interface[interface_len-1] == 0);
                    uint32 version = buf_read_u32(msg);

                    fprintf(stderr,"global\ninterface (%d): %s\n", interface_len, interface_buf.data);

                    fprintf(stderr, "announced %u - actual %lu\n",announced_size,sizeof(object_id) + sizeof(opcode) + sizeof(announced_size) + sizeof(name) + sizeof(interface_len) + interface_buf.count + sizeof(version));
                    fprintf(stderr,"%u - %u - %lu\n", interface_len, padded_interface_len, interface_buf.count);
                    assert(announced_size == sizeof(object_id) + sizeof(opcode) + sizeof(announced_size) + sizeof(name) + sizeof(interface_len) + interface_buf.count + sizeof(version));

                    String_View interface_sv = {
                        .data = (char *)interface,
                        .count = interface_len-1
                    };
                    switch(identify_interface(&interface_sv)) {
                        case WL_SHM_INTERFACE: 
                            {
                                state->wl_shm = wayland_wl_registry_bind(
                                        fd, state, name,
                                        &interface_sv, version);
                                        
                            } break;
                        case XDG_WM_BASE_INTERFACE:
                            {
                                state->xdg_wm_base = wayland_wl_registry_bind(
                                        fd, state, name,
                                        &interface_sv, version);
                            } break;
                        case WL_COMPOSITOR_INTERFACE:
                            {
                                state->wl_compositor = wayland_wl_registry_bind(
                                        fd, state, name,
                                        &interface_sv, version);
                            } break;
                        default:
                            {
                                fprintf(stderr,"unhandled interface\n");
                            }
                    }
                } break;
            default:
                {
                    fprintf(stderr, "unhandled wl_registry event\n");
                } 
        }
    } else if (object_id == state->wl_shm) { 
        switch (opcode) {
            case wayland_wl_shm_event_format:
                {
                    uint32 format = buf_read_u32(msg);
                    switch (format) {
                        case 0:
                            {
                                fprintf(stderr, "argb8888 available\n");
                            } break;
                        case 1:
                            {
                                fprintf(stderr, "xrgb8888 available\n");
                            } break;
                        default:
                            {
                                fprintf(stderr, "format %x available\n",
                                        format);
                            }
                    }
                } break;
        }
    } else if (object_id == state->wl_surface) {
        switch (opcode) {
            case wayland_wl_surface_event_enter:
                {
                    fprintf(stderr, "IN: wl_surface@%u.enter\n", object_id);
                } break;
            case wayland_wl_surface_event_leave:
                {
                    fprintf(stderr, "IN: wl_surface@%u.leave\n", object_id);
                } break;
            case wayland_wl_surface_event_preferred_buffer_scale:
                {
                    uint32 factor = buf_read_u32(msg); // NOTE: signed but non-negative
                    fprintf(stderr, "wl_surface@%u prefferred_buffer_scale = %u\n",
                            object_id, factor);
                } break;
        }
    } else if (object_id == state->xdg_wm_base) {
        fprintf(stderr, "xdg_wm_base@%u event\n", object_id);
        switch (opcode) {
            case wayland_xdg_wm_base_event_ping:
                {
                    uint32 ping = buf_read_u32(msg);
                    wayland_xdg_wm_base_pong(fd, state, ping);
                } break;
            default:
                {
                    fprintf(stderr, "unhandled xdg_wm_base event\n");
                }
        }
    } else if (object_id == state->xdg_surface) {
        fprintf(stderr, "xdg_surface@%u event\n", object_id);
        switch (opcode) {
            case wayland_xdg_surface_event_configure:
                {
                    fprintf(stderr, "IN: xdg_surface@%u.configure", object_id);
                    uint32 serial = buf_read_u32(msg);
                    wayland_xdg_surface_ack_configure(fd, state, serial);
                    state->state = STATE_SURFACE_ACKED_CONFIGURE;
                } break;
        }
    } else if (object_id == state->xdg_toplevel) {
        fprintf(stderr, "xdg_toplevel@%u event\n", object_id);
        switch (opcode) {
            case wayland_xdg_toplevel_event_configure:
                {
                    fprintf(stderr, "IN: xdg_toplevel@%u.configure", object_id);
                } break;
            case wayland_xdg_toplevel_event_configure_bounds:
                {
                    fprintf(stderr, "IN: xdg_toplevel@%u.configure_bounds", object_id);
                } break;
            case wayland_xdg_toplevel_event_wm_capabilities:
                {
                    fprintf(stderr, "IN: xdg_toplevel@%u.wm_capabilities", object_id);
                } break;
        }
    } else {
        fprintf(stderr,"unhandled event:\nobject: %u\nopcode: %u\n",
                object_id, opcode);
    }

}

enum EVENT_BUFFER_STATE {
    SOCKET_READ_SOME = 1,
    SOCKET_WOULD_BLOCK = 2,
    SOCKET_BUFFER_FULL = 4,
    SOCKET_CLOSED = 8,
    SOCKET_ERROR = 16
};
typedef enum EVENT_BUFFER_STATE EVENT_BUFFER_STATE;

internal int64 read_socket(int fd, uint8 **buffer, size_t *cap, Fd_Queue *fdq,
        EVENT_BUFFER_STATE *response) {
    struct iovec iov = {
        .iov_base = *buffer,
        .iov_len = *cap
    };

    union {
        struct cmsghdr align;
        uint8 bytes[CMSG_SPACE(sizeof(int) * WAYLAND_MAX_FDS_PER_RECVMSG)];
    } control = {};

    struct msghdr mh = {};
    mh.msg_iov = &iov;
    mh.msg_iovlen = 1;
    mh.msg_control = control.bytes;
    mh.msg_controllen = sizeof(control.bytes);

    ssize_t read_bytes = recvmsg(fd, &mh, MSG_CMSG_CLOEXEC);
    if (read_bytes == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // NOTE: Non fatal error. Nothing to read.
            *response = SOCKET_WOULD_BLOCK;
            read_bytes = 0;
        }
        else {
            *response = SOCKET_ERROR;
            read_bytes = 0;
        }
    } else if (read_bytes == 0) {
        *response = SOCKET_CLOSED;
    } else {
        *response = SOCKET_READ_SOME;
    }
    if (mh.msg_flags & MSG_CTRUNC) {
        // TODO: Log that ancillary fds were lost on transmission.
        *response = SOCKET_ERROR;
        read_bytes = 0;
    }
    fprintf(stderr,"READ: %zd - result=%d\n", read_bytes,*response);
    
    if (read_bytes) {
        *buffer += read_bytes;
        *cap -= read_bytes;
        for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&mh);
                cmsg;
                cmsg = CMSG_NXTHDR(&mh, cmsg)) {
            if (cmsg->cmsg_level == SOL_SOCKET &&
                    cmsg->cmsg_type == SCM_RIGHTS) {
                size_t data_len = cmsg->cmsg_len - CMSG_LEN(0);
                size_t fd_count = data_len /sizeof(int);
                int *fds = (int *)CMSG_DATA(cmsg);

                for (size_t i = 0; i < fd_count; i++) {
                    if(!Fd_Queue_Push(fds[i], fdq)) {
                        // TODO: Log queue overflow
                        // TODO: might want to close(fds[i]) if intending to keep running.
                        *response = SOCKET_ERROR;
                        return 0;
                    }
                    fprintf(stderr,"READ ANCILLARY FD: %d\n", fds[i]);
                }
            }
        }
    }

    return read_bytes;
}

internal void shift_buffer(Compositor_Message_Buf *buf) {
    // If low on space, move head to start.
    if ( buf->head > buf->read_buf) {
        // ran out of space or about to run out and there's some free prefix
        // or over half of the buffer is free prefix
        assert(buf->count < buf->cap);
        assert((char *)(buf->head + buf->count) 
                <= (char *)(buf->read_buf + buf->cap));
        memmove(buf->read_buf, buf->head, buf->count);
        buf->head = buf->read_buf;
    }
}

internal bool get_compositor_message(Compositor_Message_Buf *buf, Byte_Buffer *msg) {
    fprintf(stderr,"EVENT BUFFER: %lu\n\n", buf->count);
    if (buf->count < wayland_header_size) {
        // Last header is incomplete, retrieve announced_size
        return false;
    }

    uint16 announced_size = *(uint16 *)(buf->head + sizeof(uint32) + sizeof(uint16));
    assert(announced_size >= wayland_header_size && (announced_size & 3) == 0);
    if (announced_size > buf->count) {
        return false;
    }
    Byte_View buf_view = {.data = buf->head, .count = announced_size};

    buf_append(msg, &buf_view);

    buf->head += announced_size;
    buf->count -= announced_size;
    
    return true;
}

internal EVENT_BUFFER_STATE fetch_from_socket(int fd, Compositor_Message_Buf *buf) {
    EVENT_BUFFER_STATE response = SOCKET_READ_SOME;

    shift_buffer(buf); 

    uint8 *read_end = buf->head + buf->count;
    if (buf->count > 0) {
        if (buf->count < wayland_header_size) {
            // Last header is incomplete, retrieve announced_size
            size_t missing_header = wayland_header_size - buf->count;
            while (missing_header > 0) {
                buf->count += read_socket(fd, &read_end, &missing_header,
                        &buf->fd_queue, &response);
                if (response != SOCKET_READ_SOME) return response;
            }
        }

        uint16 announced_size = *(uint16 *)(buf->head + sizeof(uint32) + sizeof(uint16));
        assert(announced_size >= wayland_header_size && (announced_size & 3) == 0);
        assert(announced_size <= buf->cap);
        if (announced_size > buf->count) {
            // Last message got cut on read.
            size_t missing_arguments = announced_size - buf->count;
            while (missing_arguments > 0) {
                buf->count += read_socket(fd, &read_end, &missing_arguments,
                        &buf->fd_queue, &response);
                if (response != SOCKET_READ_SOME) return response;
            }
            assert(missing_arguments == 0);
        }
    }

    size_t free_count = (uint8 *)(buf->read_buf + buf->cap) - read_end;
    assert(free_count >= 0);
    if (free_count > 0 && buf->count == 0) {
        buf->count += read_socket(fd, &read_end, &free_count,
                &buf->fd_queue, &response);
        if (response != SOCKET_READ_SOME) return response;
    }
    if (free_count == 0) {
        response = SOCKET_BUFFER_FULL;
    }
    return response;
}

int main(int argc, char * argv[]) {
    (void)argc;
    (void)argv;

    // Open a UNIX domain socket.
    int fd = wayland_display_connect();
    set_socket_nonblocking(fd);

    int defaultWidth = 1280;
    int defaultHeight = 720;

    state_t clientState = {
        // Request a registry from wl_display to list and bind global objects.
        // wl_display::get_registry
        .wl_registry = wayland_wl_display_get_registry(fd),
        .wl_shm = 0, //bind global
        .xdg_wm_base = 0, //bind global
        .wl_compositor = 0, //bind global
        .wl_surface = 0, // wl_compositor.create_surface
        .xdg_surface = 0, // xdg_wm_base.get_xdg_surface
        .xdg_toplevel = 0 // xdg_surface.get_toplevel
    };

    resizeWindowBuffer(&clientState, defaultWidth, defaultHeight);

    Compositor_Message_Buf read_buffer;
    init_compositor_message_buf(&read_buffer);
    while (running) {
        uint8 msg_buf[4096] = "";

        Byte_Buffer msg = {
            .data = msg_buf,
            .count = 0,
            .cap = sizeof(msg_buf)
        };

        EVENT_BUFFER_STATE socket_read_status = SOCKET_READ_SOME;
        do {
            socket_read_status = fetch_from_socket(fd, &read_buffer);
            if (socket_read_status & (SOCKET_CLOSED | SOCKET_ERROR)) {
                running = false;
            }
            while (running && get_compositor_message(&read_buffer, &msg)) {
                Byte_View msg_view = {
                    .data = msg.data,
                    .count = msg.count
                };
                wayland_handle_message(fd, &clientState, 
                        &msg_view, &read_buffer.fd_queue);
                msg = {
                    .data = msg_buf,
                    .count = 0,
                    .cap = sizeof(msg_buf)
                };
            }
        } while (running && socket_read_status != SOCKET_WOULD_BLOCK);
        if (!running) {
            // TODO: Log what happened.
            break;
        }

        // If bind phase complete, create surface.
        if (clientState.wl_compositor != 0 && clientState.wl_shm != 0 &&
            clientState.xdg_wm_base != 0 && clientState.wl_surface == 0) {
            assert(clientState.state == STATE_NONE);

            clientState.wl_surface = wayland_wl_compositor_create_surface(fd, &clientState);
            clientState.xdg_surface = wayland_xdg_wm_base_get_xdg_surface(fd, &clientState);
            clientState.xdg_toplevel = wayland_xdg_surface_get_toplevel(fd, &clientState);
            wayland_wl_surface_commit(fd, &clientState);
        }
        // TODO:
        // If ack configure confirmed:
        //     Create wl_shm_pool if there's none.
        //     Create wl_buffer if there's none.
        //     Render frame
        //     wl_surface_attach
        //     wl_surface_commit
        // Cleanup
        int timeout_ms = 16;
        struct pollfd pfd = {
            .fd = fd,
            .events = POLLIN
        };

        poll(&pfd, 1, timeout_ms);

    }

    return 0;
}
