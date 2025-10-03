// https://medium.com/@justup1080/tutorial-creating-a-minimalist-http-server-in-c-2303d140c725
// https://hoad.io/libev-is-neat/

#include "jdf.h"
#include "http_codes.h"

#ifndef jdfhttp_h
#define jdfhttp_h

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <pthread.h>
#include <ev.h>



// Dump s8 in desperation (debugging)
void dumbp(s8 s) {
  printf("%*ti B ✏ ", 5, s.len);
  for (size i = 0; i < s.len; i++) printf("%c", s.buf[i]);
  printf("\n");
  fflush(0); // flush all open output streams
}

typedef struct server Server; // forward decl for Request and Workshop
typedef struct client Client; // forward decl for Request

typedef struct {
  Client *client;
  b32 is_update;
  union {
    struct {
      s8 update; // e.g. message for SSE to send through, next transfer chunk to send through, websocket input (eventually)
      Client *from; // provide for all SSE, and WS (if not initiated by same client)
    };
    struct {
      s8 raw;
      enum http_status error; // in anticipation...
      enum http_method method;
      s8 uri;
      s8 protocol;
      void *params; // optional pointer-to-struct of parsed params
      s8m *headers;
      s8m *cookies;
      s8 body;
    };
  };
} Request;

typedef struct {
  Client *client;
  b32 is_update;
  union {
    s8 update; // e.g. message for SSE to send, next transfer chunk, websocket output (eventually)
    struct {
      enum http_status status;
      enum content_type type;
      s8m *headers; // does not accommodate repeat keys, which are permitted by http spec https://stackoverflow.com/a/4371395/780743
      s8m *cookies;
      s8l *body;
    };
  };
} Response;

// Runs within worker thread with its store and scratch arenas.
typedef Response (*Handler)(arena *store, arena scratch, Request req);
//                 ^^^^^^^

// Returns pointer to appropriate struct of parsed parameters, or 0 if no match.
typedef void *(*UriParser)(arena *store, arena scratch, s8 uri);
//              ^^^^^^^^^

typedef struct {
  s8 uri; // blank for default handler
  UriParser parser; // 0 for exact match
  Handler handler; // TODO 2025-10-01 08:57:59 maybe http method as part of route?
} Route;

typedef struct {
  i32 domain; // PF_INET or PF_UNIX protocol families ~aka address families
  i32 port;
  i32 backlog; // max pending connection queue length
  u32 interface;
  i32 rcvtimeo;
  i32 sndtimeo;
  size server_mem;
  size client_mem;
  size worker_mem;
  i32 clients; // max
  i32 workers; // exact
  size chunk_size; // outgoing
  // i32 chunk_queue_cap; // e.g. 31
  arena store;
  arena scratch;
} Config;

i32 nworkers(void);

// TODO 2025-10-01 08:28:50 could (statically) analyse client_mem
// usage and minimise it (store and scratch) because it multiplies
// with connections, unlike server or worker arenas.

#define DEFAULT_CONFIG .domain = PF_INET,       \
    .backlog = 10,                              \
    .interface = INADDR_ANY,                    \
    .rcvtimeo = 5,                              \
    .sndtimeo = 5,                              \
    .server_mem = MiB(1),                       \
    .client_mem = KiB(256),                     \
    .worker_mem = MiB(1),                       \
    .clients = 1024,                            \
    .workers = nworkers(),                      \
    .chunk_size = KiB(4)

typedef struct product Product; // forward decl

typedef struct { // impl after jdf.h bufout
  u8 *buf;
  size len;
  size cap;
  enum direction then;
  b32 finished; // 1 = end of current message
  Client *dest; // for workshop.pending's benefit
} Chunk;

typedef struct {
  Server *server;
  arena store;
  arena scratch;
  byte *store_reset; // after initialisation, before work; only slightly breaks arena concept
  pthread_t thread;
  ev_io write_io;
  Chunk pending; // under construction, before copy to Product->chunks
} Workshop; // Resources for one worker!

ARRAY(Chunks, Chunk)
ARRAY(Workshops, Workshop)
ARRAY(Requests, Request)
ARRAY(Clientptrs, Client *)
  
typedef struct { // allocated in Server arena
  Requests requests;
  queue q;
} Work; // Concurrent queue (single consumer because mutex)

typedef struct server {
  Config config;
  i32 socket;
  struct sockaddr_in address;
  char ip[INET_ADDRSTRLEN];
  i32 port;
  struct ev_loop *loop;
  arena store;
  arena scratch;
  Handler handler;
  Workshops workshops;
  Work work;
  // https://randu.org/tutorials/threads/
  pthread_cond_t work_waiting;
  pthread_mutex_t work_waiting_lock; // just required for cond
  Clientptrs clients;
  size nclients; // should always match `clients` occpancy
} Server;

typedef struct product { // allocated in Client arena
  Chunks chunks;
  queue q;
} Product; // Concurrent queue (single consumer)

MAYBE(Work)
MAYBE(Product)

enum mode {
  REQUEST_RESPONSE,
  SERVER_SENT_EVENTS,
  // TRANSFER_ENCODING_CHUNKED, // then back to NORMAL when finished?
  //  WEBSOCKET
};
  
typedef struct client {
  Server *server;
  arena store;
  arena scratch;
  byte *store_reset; // after initialisation, before work; only slightly breaks arena concept
  struct sockaddr_in address;
  char ip[INET_ADDRSTRLEN];
  i32 port;
  ev_io read_io;
  ev_io write_io;
  Product deliver;
  enum mode mode;
} Client; // Server's resources for serving one client // TODO 2025-09-29 22:24:48 rename to Connection ?

// ────────────────────────────────────────────────────────────────────── Server
#define ipstr(stem, addr)                                               \
  char stem##ip[INET_ADDRSTRLEN];                                       \
  int stem##port = ntohs(addr.sin_port);                                \
  inet_ntop(PF_INET, &addr.sin_addr, stem##ip, INET_ADDRSTRLEN)

Server make_server_fn(struct args args, Handler h, Config c) {
  if (args) {
    
  }
  Server server = {
    .config = c,
      // learn about SOCK_DGRAM, SOCK_RAW types later
    .socket = socket(c.domain, SOCK_STREAM, 0), // 0 is IP, internet protocol!
    .address = {.sin_family = c.domain,
                .sin_port = htons(c.port), // convert byte order
                .sin_addr = {.s_addr = htonl(c.interface)}},
    .handler = h
  };
  if (server.socket < 0) {
    perror("Socket creation failed");
    exit(1);
  }
  i32 yes = 1; // allow faster relaunch
  if (setsockopt(server.socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
    perror("Socket option setting failed");
    exit(1);
  }
  if (bind(server.socket,
           (struct sockaddr *)&server.address,
           sizeof(server.address)) < 0) {
    perror("Socket binding failed");
    exit(1);
  }
  if (listen(server.socket, c.backlog) < 0) {
    perror("Socket listen failed");
    exit(1);
  }
  return server;
}

// Slightly misleading name because launch does most of resource alloc.
#define make_server(a, h, ...) make_server_fn(a, h, (Config){DEFAULT_CONFIG, __VA_ARGS__})

#ifdef _WIN32
#include <sysinfoapi.h>
i32 nproc(void) {
  SYSTEM_INFO sysinfo;
  GetSystemInfo(&sysinfo);
  return sysinfo.dwNumberOfProcessors;
}
//#elif __APPLE__
//#include <sys/sysctl.h>
//i32 nproc(void) {
  //  i32 v = 0;
  //  usize len = 1;
  //  if (!sysctlbyname("hw.logicalcpu", &v, &len, 0, 0)) // 0 is success return v;
     //    perror("Couldn't get system information");
  //  return v;
  //}
//#elif __linux
#else
i32 nproc(void) { return sysconf(_SC_NPROCESSORS_ONLN); }
#endif

i32 nworkers(void) {
  i32 np = nproc();
  return np==1 ? np : np-1;
}

i32 set_non_blocking(int sockfd) {
  i32 flags = fcntl(sockfd, F_GETFL, 0);
  if (fcntl(sockfd, F_SETFL, (flags < 0 ? 0 : flags) | O_NONBLOCK) == -1) {
    perror("Failed to set nonblocking");
    exit(1);
  }
  return 0;
}

// https://stackoverflow.com/a/16213822/780743
i32 set_nodelay(int sockfd) {
  i32 yes = 1;
  if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (byte *)&yes, sizeof(i32)) < 0) {
    perror("Failed to set nodelay");
    exit(1);
  }
  return 0;
}

// https://stackoverflow.com/a/2939145/780743
i32 set_timeout(int sockfd, int which, int seconds) {
  struct timeval tv;
  tv.tv_sec = seconds;
  tv.tv_usec = 0;
  if (setsockopt(sockfd, SOL_SOCKET, which, (byte *)&tv, sizeof tv) < 0) {
    perror("Failed to set timeout");
    exit(1);
  }
  return 0;
}

#define ReqErr(e) do { req.error = e; return req; } while (0) // macro block semicolon hack

// Store raw request, "parse" into zero-copy s8s.
// TODO 2025-10-03 12:48:25 maybe divert body elsewhere for large requests, deal with separately...
Request parse_request(arena *store, arena scratch, s8 raw) {
  Request req = {.raw = raw};
  // https://developer.mozilla.org/en-US/docs/Web/HTTP/Guides/Messages
  s8pair line = s8cutu8(raw, '\n'); // .head is next line, .tail is rest
  if (!line.ok) ReqErr(BAD_REQUEST); // HTTP request always >1 line
  s8pair seg = s8cutu8(line.head, ' '); // e.g. "POST /path/to/thing HTTP/1.1"
  if (!seg.ok) ReqErr(BAD_REQUEST);

  req.method = parse_http_method(seg.head);
  if (!req.method) ReqErr(BAD_REQUEST);

  seg = s8cutu8(seg.tail, ' ');
  if (!seg.ok) ReqErr(BAD_REQUEST);

  req.uri = seg.head;
  req.protocol = seg.tail;

  s8m *headers = {0};
  while (line.tail.len) {
    s8pair next = s8cutu8(line.tail, '\n');
    if (next.ok) line = next;
    else line = (s8pair){.head = line.tail, .tail = (s8){0}};
    if (s8blank(line.head)) {
      req.body = line.tail;
      break; // blank line indicating end of metadata
    }
    s8pair header = s8cut(line.head, s8(": "));
    if (!header.ok) ReqErr(BAD_REQUEST);
    headers = s8massoc(store, headers, header.head, header.tail);
    if (!headers) {
      fprintf(stderr, "💣 OOM saving headers \n");
      ReqErr(SERVICE_UNAVAILABLE);
    }
    req.headers = headers;
  }

  s8m *cookies = {0};
  // e.g. Cookie: name=value; name2=value2; name3=value3
  s8m *cookiekv = s8mget(headers, spell_header[COOKIE]);
  if (cookiekv) {
    line = (s8pair){.head = (s8){0}, .tail = cookiekv->val};
    while (line.tail.len) {
      s8pair next = s8cut(line.tail, s8("; "));
      if (next.ok) line = next;
      else line = (s8pair){.head = line.tail, .tail = (s8){0}};
      s8pair cookie = s8cutu8(line.head, '=');
      if (!cookie.ok) ReqErr(BAD_REQUEST);
      cookies = s8massoc(store, cookies, cookie.head, cookie.tail);
      if (!cookies) {
        fprintf(stderr, "💣 OOM saving cookies \n");
        ReqErr(SERVICE_UNAVAILABLE);
      }
      req.cookies = cookies;
    }
  }
  return req;
}

// Run on main thread (by Client)
Product_ make_product(arena *a, i32 len, size chunk_size) {
  Product_ nil = (Product_){0};
  i32 cap = queue_capacity(len);
  if (!cap) return nil;
  Chunks_ chunks = make_Chunks(a, len);
  if (!chunks.ok) return nil;
  u8 *buf = new (a, u8, len * chunk_size);
  if (!buf) return nil;
  for (size i = 0; i < len; i++) {
    chunks.v.buf[i] = (Chunk) {
      .buf = &buf[i * chunk_size],
      .cap = chunk_size
    };
  }
  return (Product_) { .v = {.chunks = chunks.v, .q = 0} };
}

// printf contents of arena. Terminates string in situ!
size arena_printf(arena *a, const char *format) {
  if (a->cur < a->end) *a->cur = 0;
  else {
    const char *warning = "❗️(too long for buffer)";
    snprintf(a->end - sizeof(warning), sizeof(warning), "%s", warning);
  }
  return printf(format, a->beg);
}

s8m *s8massoc_clonev(arena *store, s8m *head, s8 k, s8 v) {
  s8m *already = s8mget(head, k);
  if (already && s8equal(already->val, v)) return head;
  s8_ vc = s8clone(store, v, 0);
  if (vc.ok) {
    s8m *ret = s8massoc(store, head, k, vc.v);
    if (ret) return ret;
  }
  fprintf(stderr, "Store usage %td/%td\n", used(store), available(store));
  fprintf(stderr, "Problem setting kv\n");
  dumbp(k); dumbp(v);
  return head;
}

// TODO 2025-10-01 17:49:58 optimal return type?
void add_header(arena *store, Response *res, enum header h, s8 v) {
  if (!res) {
    fprintf(stderr, "Tried to add_header to null Response.\n");
    return;
  }
  res->headers = s8massoc_clonev(store, res->headers, spell_header[h], v);
}

// TODO 2025-10-01 17:49:58 optimal return type?
// Generally headers should be set in handlers.
void add_headers(arena *store, arena scratch, Response *res) {
  if (!res) {
    fprintf(stderr, "Tried to add_headers to null Response.\n");
    return;
  }
  // TODO  2025-09-29 13:40:10 Transfer-Encoding: chunked
  // https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/Content-Length
  // TODO should be conditional on client's invitation
  switch (res->status) {
  case INVALID_HTTP_STATUS:
    res->status = INTERNAL_SERVER_ERROR;
    return;
  case NO_CONTENT:
    return;
  default:
    break;
  }
  
  add_header(store, res, CONNECTION, s8("keep-alive"));
  if (res->type != INVALID_CONTENT_TYPE)
    add_header(store, res, CONTENT_TYPE, describe_content_type[res->type]);
  if (res->type == EVENT_STREAM) return;

  s8_ v = s8sprintf(&scratch, "%ti", res->body ? s8llen(res->body) : 0);
  if (v.ok) add_header(store, res, CONTENT_LENGTH, v.v);
  else fprintf(stderr, "Error setting Content-Length\n");
}

b32 flushc(Chunk *workshop_pending) {
  Client *dest = workshop_pending->dest;
  if (!dest) {
    fprintf(stderr, "💣 Tried to flush to uninitialised destination\n");
    return 0;
  }
  Product *d = &dest->deliver;
  i32 idx = 0; // also see write_qout for queue semantics

  idx = queue_push(&d->q, d->chunks.len); // ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ Queue access
  // printf("%d %s:%d\n", idx, dest->ip, dest->port);
  if (idx < 0) {
    //fprintf(stderr, "⚠ Product queue full for %s:%d, chunk flush failed.\n", dest->ip, dest->port);
    fprintf(stderr, "⚠ Product queue full for %p, chunk flush failed.\n", (void *)dest);
    return 0;
  }
  // workshop_pending->buf is preallocated in Workshop arena by `launch`
  Chunk *client_deliver = &d->chunks.buf[idx];
  u8 *buf = client_deliver->buf; // preallocated in Client arena by `make_product`
  *client_deliver = *workshop_pending; // copy all fields but clobbers buf pointer
  client_deliver->buf = buf; // correct buf pointer
  copy(client_deliver->buf, workshop_pending->buf, workshop_pending->len);
  queue_push_commit(&d->q); //  ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴

  // Reset chunk for reuse! FIXME 2025-09-30 15:15:06 Error-prone
  workshop_pending->len = 0;
  workshop_pending->then = NEITHER;
  workshop_pending->finished = 0;
  // keep buf, cap, dest
  return 1;
}

size s8writec(void *out, s8 s) { // impl after s8write
  Chunk *c = (Chunk *)out; // see Writer
  // Write s to p consecutive p->chunks. Caller's responsibilty to flush at EOM (via finishc).
  if (!c->buf) {
    fprintf(stderr, "💣 Tried to write to uninitialised chunk\n");
    return 0;
  }
  if (!s.buf) {
    fprintf(stderr, "💣 Tried to write uninitialised string\n");
    return 0;
  }
  u8 *buf = s.buf;
  u8 *end = endof(s);
  size total_copied = 0;
  while (buf < end) {
    i32 avail = c->cap - c->len;
    i32 count = (avail < end - buf) ? avail : (i32)(end - buf);
    copy(c->buf + c->len, buf, count);
    buf += count;
    c->len += count;
    total_copied += count;
    if (c->len == c->cap) if (!flushc(c)) break;
  }
  return total_copied;
}

void finishc(Chunk *c, enum direction then) {
  c->finished = 1;
  c->then = then;
  //printf("flushed with %d\n", then);
  flushc(c);
}

/*
  Runs on worker thread. Expect to block when Product queue full,
  until client loop drains it from the main thread.
*/
void serialise_response(Workshop *shop, Response res) {
  arena *store = &shop->store;
  arena scratch = shop->scratch;
  Chunk *out = &shop->pending;
  if (res.is_update) {
    s8writec(out, res.update);
    finishc(out, WRITE); // TODO 2025-09-30 11:41:06 READWRITE if websocket...
#ifndef QUIET
    printf("📡 %td B to %s:%d\n", res.update.len, res.client->ip, res.client->port);
#endif
  } else {
    s8 crlf = s8("\r\n");
    add_headers(store, scratch, &res); // reassigning to pass-by-value parameter
    s8m *header = res.headers;
    s8printf(scratch, s8writec, out, "HTTP/1.1 %i %s\r\n",
             res.status, spell_http_status[res.status]);
    while (header) { // grug approve
      s8writec(out, header->key);
      s8writec(out, s8(": "));
      s8writec(out, header->val);
      s8writec(out, crlf);
      header = header->next;
    }
    s8writec(out, crlf);
    for (s8l *node = res.body; node; node = node->next)
      s8writec(out, node->val);

    // TODO 2025-10-01 18:50:58 BOTH at appropriate point in websocket handshake
    if (res.type == EVENT_STREAM) {
      res.client->mode = SERVER_SENT_EVENTS;
      finishc(out, WRITE);
    }
    else finishc(out, READ);
    printf("📣 %i\n", res.status);
  }
  res.client->store.cur = res.client->store_reset;
}

// TODO 2025-10-01 07:36:32 could work up into general SET_ARRAY macro
b32 add_client(Server *server, Client *client) {
  server->nclients++;
  Client **available = 0; // first zero value (caused by remove_client)
  Client **end = endof(server->clients);
  for (Client **cur = server->clients.buf; cur < end; cur++) {
    if (*cur == client) return 0; // already there
    else if (!*cur && !available) available = cur; // but keep scanning
  }
  if (available) {
    *available = client;
    return 1;
  }
  fprintf(stderr, "Unable to track client!\n");
  return 0;
}

b32 remove_client(Server *server, Client *client) {
  server->nclients--;
  Client **end = endof(server->clients);
  // Always scans whole array.
  for (Client **cur = server->clients.buf; cur < end; cur++) {
    if (*cur != client) continue;
    *cur = 0;
    return 1;
  }
  return 0;
}

i32 count_clients(Server *server) {
  Client **end = endof(server->clients);
  i32 n = 0;
  for (Client **cur = server->clients.buf; cur < end; cur++) 
    if (*cur) n++;
  return n;
}

void client_cleanup_basics(arena *store, arena *scratch, i32 fd) {
  close(fd);
  free_arena(scratch); // needs to be freed first because *client itself is within client->store span
  free_arena(store);
}

void cleanup_client(EV_P_ ev_io *w) {
  Client *client = (Client *)w->data;
  Server *server = client->server;
  remove_client(server, client);
  // https://metacpan.org/dist/EV/view/libev/ev.pod#ev_TYPE_stop-(loop,-ev_TYPE-*watcher)
  ev_io_stop(EV_A_ &client->read_io);
  ev_io_stop(EV_A_ &client->write_io);
  client_cleanup_basics(&client->store, &client->scratch, w->fd);
}

// returns previous state; not enjoyable to implement
enum direction client_set_direction(EV_P_ ev_io *w, enum direction next, char *note) {
  Client *client = (Client *)w->data;
  ev_io *read_io = &client->read_io;
  ev_io *write_io = &client->write_io;
  enum direction previous;
  if (ev_is_active(read_io)) previous = ev_is_active(write_io) ? READWRITE : READ;
  else previous = ev_is_active(write_io) ? WRITE : NEITHER;
  //printf("direction %d", previous);
  if (next!=previous) {
    switch (previous) {
    case NEITHER:
      switch (next) {
      case READ:
        ev_io_start(EV_A_ read_io);
        break;
      case WRITE:
        ev_io_start(EV_A_ write_io);
        break;
      case READWRITE:
        ev_io_start(EV_A_ read_io);
        ev_io_start(EV_A_ write_io);
        break;
      }
    case READ:
      switch (next) {
      case WRITE:
        ev_io_stop(EV_A_ read_io); // fallthrough
      case READWRITE:
        ev_io_start(EV_A_ write_io);
        break;
      }
    case WRITE:
      switch (next) {
      case READ:
        ev_io_stop(EV_A_ write_io); // fallthrough
      case READWRITE:
        ev_io_start(EV_A_ read_io);
        break;
      }
    case READWRITE:
      switch (next) {
      case READ:
        ev_io_stop(EV_A_ write_io);
        break;
      case WRITE:
        ev_io_stop(EV_A_ read_io);
        break;
      }
    }
    //printf(" → %d", next);
  }
  //printf(" %s\n", note ? note : "");
  return previous;
}

const static s8 UNAVAILABLE = s8("HTTP/1.1 503 Service Unavailable\r\n");

// FIXME 2025-05-25 15:28:50 need to close client gracefully when arena
// available below a threshold, or timeout since last read...
// TODO 2025-05-25 15:31:23 rate limitation, or leave it to nginx?
void unavailable(i32 sock, char *msg) {
  fprintf(stderr, "💣 Failed to %s\n", msg);
  write(sock, UNAVAILABLE.buf, UNAVAILABLE.len);
}

/*
  Runs on main thread.
 */
void write_client(EV_P_ ev_io *w, i32 events) {
  Client *client = (Client *)w->data;
  arena scratch = client->scratch; 
  Product *p = &client->deliver;

  i32 idx = queue_pop(&p->q, p->chunks.len); // ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ Queue access
  if (idx < 0) {
    // printf("Queue empty\n"); // e.g. 20x... TODO 2025-10-01 08:36:33 is this wasteful?
    return;
  }
  Chunk c = p->chunks.buf[idx]; // c.dest is irrelevant here
  u8 *buf = new (&scratch, u8, client->server->config.chunk_size);
  if (!buf) {
    unavailable(w->fd, "allocate out buffer");
    cleanup_client(EV_A_ w);
    return;
  }
  copy(buf, c.buf, c.len); // defensive copy, would be hard to debug if winged it
  queue_pop_commit(&p->q); // ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴

  size total_bytes_written = 0;
  size bytes_written = 0;
  while (1) {
    if (c.len == 0) break;
    bytes_written = write(w->fd, buf + total_bytes_written,
                          c.len - total_bytes_written);
    if (bytes_written == 0) {
      printf("Write client wrote nothing\n");
      //cleanup_client(EV_A_ w);
    } else if (bytes_written < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // just try again?
        printf("Should try again?\n");
      } else {
        perror("Error writing to client");
        cleanup_client(EV_A_ w); // TODO 2025-10-03 08:51:04 confirm this fires on send timeout
        return;
      }
    } else {
      total_bytes_written += bytes_written;
    }
    if (total_bytes_written < c.len) {
      printf("Incomplete socket write (%li/%li B), trying to continue.\n", bytes_written, c.len);
    } else break;
  }
  if (c.finished) {
#ifndef QUIET
    //printf("✅ %ti B written to %s:%d\n", total_bytes_written, client->ip, client->port);
#endif
  } else if (c.len) {
#ifndef QUIET
    printf("➡️ Chunk of %td B written, message not finished to %s:%d\n",
           c.len, client->ip, client->port);
#endif
  } else { //  Only legitimate empty is when finished, to set direction.
    fprintf(stderr, "Erroneously wrote no data to %s:%d.\n", client->ip, client->port);
  }

  char note[128];
  snprintf(note, sizeof note, "write_client %s:%d %s", client->ip, client->port,
           client->mode==SERVER_SENT_EVENTS ? "📡" : "📣");
  client_set_direction(EV_A_ w, c.then, note);
}

b32 enqueue_request(Request req) {
  Server *server = req.client->server;
  i32 idx = queue_push(&server->work.q, server->work.requests.len);
  //printf("queue_push position %i\n", idx);
  if (idx < 0) return 0; // queue full
  server->work.requests.buf[idx] = req;
  queue_push_commit(&server->work.q);
  pthread_mutex_lock(&server->work_waiting_lock);
  pthread_cond_signal(&server->work_waiting); // worker can just sleep again if queue already emptied
  pthread_mutex_unlock(&server->work_waiting_lock);
  return 1;
}

void read_client(EV_P_ ev_io *w, i32 events) {
  Client *client = (Client *)w->data;
  // using scratch arena as a buffer here, instead of local array
  // printf("client scratch usage should be 0: %ti\n", used(&client->scratch));
  ssize_t bytes_read = read(w->fd, client->scratch.beg, available(&client->scratch));
  client->scratch.cur = client->scratch.beg + bytes_read;
  if (bytes_read == 0) { // client closed connection
    // printf("Zero bytes read from %s:%d, cleaning up\n", client->ip, client->port);
    cleanup_client(EV_A_ w);
  } else if (bytes_read < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // nothing to read yet
    } else {
      perror("Error reading client");
      cleanup_client(EV_A_ w); // TODO 2025-10-03 08:52:01 confirm this fires on read timeout
    }
  } else {
    // TODO handle large read, e.g. stream to arena until finished or excessive,
    // then handle? For now, store (copy) request in client store arena.
    s8_ raw = s8clone(&client->store, s8bytespan(client->scratch.beg, client->scratch.cur), 0);
    if (!raw.ok) {
      unavailable(w->fd, "store raw request");
      cleanup_client(EV_A_ w);
      return;
    }
    // s8arenaprintf(&client->scratch, "🔔 %s\n");
    client->scratch.cur = client->scratch.beg; // Reset!
    Request req = parse_request(&client->store, client->scratch, raw.v);
    char *uri = s8unwrap(&client->scratch, req.uri);
    if (uri) printf("🔔 %s from %s:%d\n", uri, client->ip, client->port);
    req.client = client;
    if (!enqueue_request(req)) {
      unavailable(w->fd, "enqueue job"); // effectively backpressure
      cleanup_client(EV_A_ w);
      return;
    }
    char note[128];
    snprintf(note, sizeof note, "read_client %s:%d %s", client->ip, client->port,
             client->mode==SERVER_SENT_EVENTS ? "📡" : "📣");
    client_set_direction(EV_A_ w, WRITE, note);
    // not closing socket
  }
}

void accept_client(EV_P_ ev_io *w, i32 events) {
  Server *server = (Server *)w->data;
  i32 addrlen = sizeof(server->address);
  i32 new_socket = accept(w->fd, // should be same as server->socket
                          (struct sockaddr *)&server->address,
                          (socklen_t *)&addrlen);
  if (new_socket < 0) perror("Socket connection failed");
  else {

    // NB server->address seemingly changed from server to client between `bind` and `accept`
    ipstr(client_, server->address);

    if (server->nclients > server->config.clients) {
      printf("⛔ Rejected connection from %s:%d\n", client_ip, client_port);
      write(new_socket, UNAVAILABLE.buf, UNAVAILABLE.len);
      close(new_socket);
      return;
    }
    printf("☎️  %s:%d\n", client_ip, client_port);
    set_non_blocking(new_socket);
    set_nodelay(new_socket);
    set_timeout(new_socket, SO_RCVTIMEO, server->config.rcvtimeo);
    set_timeout(new_socket, SO_SNDTIMEO, server->config.sndtimeo);
    // Allocate arenas TODO 2025-09-30 15:56:50 monitor usage, tune, limit
    arena client_store = alloc_arena(server->config.client_mem);
    arena client_scratch = alloc_arena(server->config.client_mem);
    // https://metacpan.org/dist/EV/view/libev/ev.pod#ASSOCIATING-CUSTOM-DATA-WITH-A-WATCHER
    Client *client = new (&client_store, Client, 1);
    if (!client) {
      unavailable(new_socket, "allocate client");
      client_cleanup_basics(&client_store, &client_scratch, new_socket); 
      return;
    }
    client->server = server;
    client->store = client_store; // for passing by reference
    client->scratch = client_scratch; // for passing by value
    client->address = server->address; // because struct apparently reused
    copy((u8 *)client->ip, (u8 *)client_ip, sizeof client_ip); // conveniences
    client->port = client_port;
    
    ev_io_init(&client->read_io, read_client, new_socket, EV_READ);
    client->read_io.data = client; // I'm a woozie (see libev doc)

    // This is started and stopped conditionally on whether there is data to
    // write, to prevent excessive activation...
    // https://buildmage.com/blog/libev-tutorial-and-wrapper
    ev_io_init(&client->write_io, write_client, new_socket, EV_WRITE);
    client->write_io.data = client;
    // ...so deliberately not starting here.

    char note[128];
    snprintf(note, sizeof note, "accept_client %s:%d %s", client->ip, client->port,
             client->mode==SERVER_SENT_EVENTS ? "📡" : "📣");
    client_set_direction(EV_A_ &client->read_io, READ, note);

    // ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ delivery queue
    // TODO 2025-09-30 09:04:44 make len configurable
    Product_ deliver = make_product(&client->store, 32, server->config.chunk_size);
    if (deliver.ok) client->deliver = deliver.v;
    else {
      unavailable(new_socket, "allocate out queue");
      client_cleanup_basics(&client->store, &client->scratch, new_socket);
      return;
    }
    client->store_reset = client->store.cur;
    add_client(server, client);
  }
}

void *worker(Workshop *workshop) { // ─────────────────────────────────── Worker
  Server *server = workshop->server;
  i32 qi = 0;
  Request req = {0};

  //  Use mutex and single-consumer queue for work allocation rather
  //  than multiple-consumer queue, which needed mutex to handle
  //  waiting anyway in this project.

  while (1) {
    // Make "this" thread the only one waiting for work:
    pthread_mutex_lock(&server->work_waiting_lock); // ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ lock work
    // Loop to cover suprious wakeup.
    while ((qi = queue_pop(&server->work.q, server->work.requests.len)) < 0)
      // Block thread instead of busy-waiting.
      pthread_cond_wait(&server->work_waiting, &server->work_waiting_lock);
    req = server->work.requests.buf[qi];
    queue_pop_commit(&server->work.q);
    pthread_mutex_unlock(&server->work_waiting_lock); // ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴
    Client *client = req.client;
    Response res = {0};
    if (!req.is_update) {
      switch (req.error) {
      case SERVICE_UNAVAILABLE: // mainly being some disaster allocating memory
        unavailable(client->write_io.fd, "parse request");
        cleanup_client(server->loop, &client->write_io);
        goto reset_store;
      case BAD_REQUEST: // TODO 2025-09-29 16:12:55 fall throughs relating only to request parsing
        res = (Response){.status = req.error};
        break;
      default:
        if (req.error) printf("Disregarding Request.error status %d.\n", req.error);
      }
    }
    // NB 2025-09-29 16:13:45 handler is currently also responsible for routing!
     
    // https://developer.mozilla.org/en-US/docs/Web/HTTP/Guides/Connection_management_in_HTTP_1.x
    // Electing not to implement pipelining ("not activated by
    // default in modern browsers"!), or HTTP/2 or /3. Client can
    // open multiple connections (resulting in multiple jdfhttp
    // Clients, probably served by different workers/threads).

    // Multiple workers would therefore not serialise to the same
    // client->deliver queue simultaneously. Pipelining is prevented
    // by half-duplex `client_set_direction`. Writer is on main thread
    // so libev can deal with delays writing.
    res = server->handler(&workshop->store, workshop->scratch, req);
    if (!res.client) res.client = client;
    workshop->pending.dest = client;
    serialise_response(workshop, res);
  reset_store:
    workshop->store.cur = workshop->store_reset;
  }
}

typedef void *(*Worker)(void *);

Work_ make_work(arena *a, i32 len) {
  Work_ nil = (Work_){0};
  i32 cap = queue_capacity(len);
  if (!cap) return nil;
  Requests_ requests = make_Requests(a, len);
  if (!requests.ok) return nil;
  return (Work_) {.v = { .requests = requests.v, .q = 0} };
}

void sigint_cb(EV_P_ ev_signal *w, i32 events) {
  fprintf(stderr, "SIGINT\n");
  ev_break (EV_A_ EVBREAK_ALL);
}

void sigpipe_cb(EV_P_ ev_signal *w, i32 events) {
  fprintf(stderr, "SIGPIPE\n");
  // TODO 2025-10-01 22:08:19 handle?
}

void launch(Server *server) {
  // ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ Arenas
  server->store = alloc_arena(server->config.server_mem);
  server->scratch = alloc_arena(server->config.server_mem);
  if (!server->store.beg || !server->scratch.beg)
    fprintf(stderr, "💣 Failed to allocate %td KB server arenas.", server->config.server_mem/KiB(1));
  // ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ Workers  
  i32 nw = server->config.workers;
  i32 rc = 0;
  Workshop *workshops = new (&server->store, Workshop, nw);
  if (!workshops) {
    fprintf(stderr, "💣 Failed to allocate %d workshops\n", nw);
    exit(1);
  }
  for (size i = 0; i < nw; i++) {
    workshops[i].server = server;
    workshops[i].store = alloc_arena(server->config.worker_mem);
    workshops[i].scratch = alloc_arena(server->config.worker_mem);
    u8 *buf = new (&workshops[i].store, u8, server->config.chunk_size);
    if (!buf) {
      fprintf(stderr, "💣 Failed to allocate workshop %td pending buffer\n", i);
      exit(1);
    }
    workshops[i].pending = (Chunk) {
      .buf = buf,
      .cap = server->config.chunk_size
    };
    workshops[i].store_reset = workshops[i].store.cur;
  }
  server->workshops.buf = workshops;
  size workers = 0;
  for (size i = 0; i < nw; i++)
    if (!(rc = pthread_create(&(workshops[i].thread), 0, (Worker)worker,
                              &workshops[i]))) {
      server->workshops.len = ++workers;
    } else {
      fprintf(stderr, "Unable to create thread %td: %i\n", i, rc);
      if (i == 0) exit(1);
      break;
    }
  Work_ work = make_work(&server->store, 32);
  if (!work.ok) {
    fprintf(stderr, "💣 Failed to make work queue\n");
    exit(1);
  }
  server->work = work.v;
  server->work_waiting = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
  pthread_mutex_init(&server->work_waiting_lock, 0);
  // ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ Client tracking
  Clientptrs_ track = make_Clientptrs(&server->store, server->config.clients); 
  if (!track.ok) {
    fprintf(stderr, "💣 Failed to allocate client tracking array for %d clients\n",
            server->config.clients);
    exit(1);
  }
  server->clients = track.v;
  // ──────────────────────────────────────────────────────────────────── Launch
  ipstr(server_, server->address);
  copy((u8 *)server->ip, (u8 *)server_ip, sizeof server_ip); // convenience
  server->port = server_port;
  printf("👂 Listening on %s:%d using %td threads for up to %d clients.\n",
         server->ip, server->port, workers, server->config.clients);
  printf("🧠 Internal memory usage will be %td-%td MiB.\n", // excludes libraries' allocs
      // TODO 2025-10-02 01:47:17 could configure individually... after profiling
      (server->config.server_mem * 2 // store, scratch
          + server->config.client_mem * 2 * 0
          + server->config.worker_mem * 2 * server->config.workers) / MiB(1),
         (server->config.server_mem * 3
          + server->config.client_mem * 2 * server->config.clients
          + server->config.worker_mem * 2 * server->config.workers) / MiB(1));
  
  server->loop = ev_loop_new(0);
  set_non_blocking(server->socket);

  ev_io accept_watcher;
  ev_io_init(&accept_watcher, accept_client, server->socket, EV_READ);
  accept_watcher.data = server; // allows access within callbacks
  ev_io_start(server->loop, &accept_watcher);

  ev_signal sigint_watcher;
  ev_signal_init(&sigint_watcher, sigint_cb, SIGINT);
  sigint_watcher.data = server;
  ev_signal_start(server->loop, &sigint_watcher);

  ev_signal sigpipe_watcher;
  ev_signal_init(&sigpipe_watcher, sigpipe_cb, SIGPIPE);
  sigpipe_watcher.data = server;
  ev_signal_start(server->loop, &sigpipe_watcher);

  ev_run(server->loop, 0);
  ev_loop_destroy(server->loop);

  // TODO is it necessary to join/kill workers? do they need enclosing while(running) loop?
}

#endif // jdfhttp_h
