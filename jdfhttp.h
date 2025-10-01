// No, don't! Use mongoose instead!
// Starting at
// https://medium.com/@justup1080/tutorial-creating-a-minimalist-http-server-in-c-2303d140c725
// https://hoad.io/libev-is-neat/

#include "jdf.h" // TODO remove if want flexibility of choosing relptr.h; may not be worth matching APIs though
// #include "relptr.h"
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

MAP_LIST(s8map, s8, s8, s8equal)

// Electing not to introduce MAYBEness to MAP_LIST get for the moment.
s8_ s8mapget_(s8map *head, s8 key) {
  s8map *kv = s8mapget(head, key);
  if (!kv) return (s8_){0};
  return (s8_){.v = kv->val };
}

b32 s8mapcontains(s8map *head, s8 key, s8 val) {
  s8_ v = s8mapget_(head, key);
  if (!v.ok) return 0;
  return s8equal(v.v, val);
}

// Dump s8 in desperation (debugging)
void dumbp(s8 s) {
  printf("%*ti B ✏ ", 5, s.len);
  for (size i = 0; i < s.len; i++) printf("%c", s.buf[i]);
  printf("\n");
  fflush(0); // flush all open output streams
}

// Associate cloned k & v. May fail (and just return previous head).
s8map *s8mapassocl(arena *store, s8map *head, s8 k, s8 v) {
  s8_ kc = s8clone(store, k);
  s8_ vc = s8clone(store, v);
  if (kc.ok && vc.ok) {
    s8map *ret = s8mapassoc(store, head, kc.v, vc.v);
    if (ret) return ret;
  }
  fprintf(stderr, "Store usage %td/%td\n", used(store), available(store));
  fprintf(stderr, "Problem setting kv\n");
  dumbp(k); dumbp(v);
  return head;
}

typedef struct server Server; // forward decl for Request and Workshop
typedef struct client Client; // forward decl for Request

typedef struct {
  Client *client;
  b32 is_update;
  union {
    s8 update; // e.g. message for SSE to send through, next transfer chunk to send through, websocket input (eventually)
    struct {
      s8 raw;
      enum http_status error; // in anticipation...
      enum http_method method;
      s8 uri;
      s8 protocol;
      void *params; // optional pointer-to-struct of parsed params
      s8map *headers;
      s8map *cookies;
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
      s8map *headers; // does not accommodate repeat keys, which are permitted by http spec https://stackoverflow.com/a/4371395/780743
      s8map *cookies;
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

enum direction {
  WRITE, // standby for next response e.g. server sent events, transfer-encoding chunked
  READ, // listen for next request e.g. normal request
  BOTH // full duplex e.g. websocket (future)
};

typedef struct product Product; // forward decl

typedef struct { // impl after jdf.h bufout
  u8 *buf;
  size len;
  size cap;
  enum direction then;
  b32 finished; // 1 = end of current message
  Product *dest; // for workshop.chunk's benefit
} Chunk;

typedef struct {
  Server *server;
  arena store;
  arena scratch;
  pthread_t thread;
  ev_io write_io;
  Chunk pending; // under construction, before copy to Product->chunks
} Workshop; // Resources for one worker!

ARRAY(Workshops, Workshop)

typedef struct { // allocated in Server arena
  _Atomic Request *requests;
  // simpler than _Atomic Requests *requests from ARRAY(Requests, Request)
  // because _Atomic struct member access is UB:
  size cap;
  queue q;
} Work; // Concurrent queue (multiple consumer)

MAYBE(Work)

ARRAY(Clients, Client *)
  
typedef struct server {
  Config config;
  i32 socket;
  struct sockaddr_in address;
  struct ev_loop *loop;
  arena store;
  arena scratch;
  Handler handler;
  Workshops workshops;
  Work work;
  // https://randu.org/tutorials/threads/
  pthread_cond_t work_waiting;
  pthread_mutex_t work_waiting_lock; // just required for cond
  Clients clients;
} Server;

typedef struct product { // allocated in Client arena
  _Atomic Chunk *chunks;
  size cap;
  queue q;
} Product; // Concurrent queue (single consumer)

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
  ev_io read_io;
  ev_io write_io;
  Product deliver;
  enum mode mode;
} Client; // Server's resources for serving one client // TODO 2025-09-29 22:24:48 rename to Connection ?

// Run on main thread (by Client)
Product_ make_Product(arena *a, i32 cap, size chunk_size) {
  Product_ nil = (Product_){0};
  cap = queue_capacity(cap);
  if (!cap) return nil;
  i32 len = cap + 1;
  _Atomic Chunk *chunks = new (a, _Atomic Chunk, len);
  if (!chunks) return nil;
  u8 *buf = new (a, u8, len * chunk_size);
  if (!buf) return nil;
  for (size i = 0; i < len; i++) {
    // All at once so no UB _Atomic struct member access.
    chunks[i] = (Chunk) {
      .buf = &buf[i * chunk_size],
      .cap = chunk_size
    };
  }
  return (Product_) { .v = {.chunks = chunks, .cap = cap, .q = 0} };
}

// ────────────────────────────────────────────────────────────────────── Server
Server make_server_fn(Handler h, Config c) {
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

// Slightly misleading name because launch does most of resource alloc.
#define make_server(h, ...) make_server_fn(h, (Config){DEFAULT_CONFIG, __VA_ARGS__})

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

  s8map *headers = {0};
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
    headers = s8mapassoc(store, headers, header.head, header.tail);
    if (!headers) {
      fprintf(stderr, "💣 OOM saving headers \n");
      ReqErr(SERVICE_UNAVAILABLE);
    }
    req.headers = headers;
  }

  s8map *cookies = {0};
  // e.g. Cookie: name=value; name2=value2; name3=value3
  s8map *cookiekv = s8mapget(headers, s8("Cookie"));
  if (cookiekv) {
    line = (s8pair){.head = (s8){0}, .tail = cookiekv->val};
    while (line.tail.len) {
      s8pair next = s8cut(line.tail, s8("; "));
      if (next.ok) line = next;
      else line = (s8pair){.head = line.tail, .tail = (s8){0}};
      s8pair cookie = s8cutu8(line.head, '=');
      if (!cookie.ok) ReqErr(BAD_REQUEST);
      cookies = s8mapassoc(store, cookies, cookie.head, cookie.tail);
      if (!cookies) {
        fprintf(stderr, "💣 OOM saving cookies \n");
        ReqErr(SERVICE_UNAVAILABLE);
      }
      req.cookies = cookies;
    }
  }
  return req;
}

// printf contents of arena. Terminates string in situ!
size s8arenaprintf(arena *a, const char *format) {
  if (a->cur < a->end) *a->cur = 0;
  else {
    const char *warning = "❗️(too long for buffer)";
    snprintf(a->end - sizeof(warning), sizeof(warning), "%s", warning);
  }
  return printf(format, a->beg);
}

Response add_header(arena *store, Response *maybe, enum header h, s8 v) {
  Response res = maybe ? *maybe : (Response){0};
  res.headers = s8mapassocl(store, res.headers, spell_header[h], v);
  return res;
}

Response add_headers(arena *store, arena scratch, Response res) {
  // TODO  2025-09-29 13:40:10 Transfer-Encoding: chunked
  // https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/Content-Length
  // TODO should be conditional on client's invitation
  switch (res.status) {
  case INVALID_HTTP_STATUS:
    res.status = INTERNAL_SERVER_ERROR;
    return res;
  case NO_CONTENT:
    return res;
  default:
    break;
  }
  res = add_header(store, &res, CONNECTION, s8("keep-alive"));
  switch (res.type) {
  case EVENT_STREAM:
    return res;
  case INVALID_CONTENT_TYPE:
    break;
  default:
    res = add_header(store, &res, CONTENT_TYPE, describe_content_type[res.type]);
    break;
  }
  s8_ v = s8sprintf(&scratch, "%ti", res.body ? s8llen(res.body) : 0);
  if (v.ok) res = add_header(store, &res, CONTENT_LENGTH, v.v);
  else fprintf(stderr, "Error setting Content-Length\n");
  return res;
}

void flushc(Chunk *c) {
  Product *p = c->dest;
  if (!p) {
    fprintf(stderr, "💣 Tried to flush to uninitialised destination\n");
    return;
  }
  i32 idx = 0; // impl after write_qout
  // printf("Trying to flush %td bytes", c->len);
  // s8 insp = (s8){.buf = c->buf, .len = c->len};
  // log_debug(insp);
  idx = queue_push(&p->q, p->cap); // ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ Queue access
  if (idx < 0) {
    printf("⚠ Product queue full, chunk flush failed.\n");
    return;
  } 
  // shallow copy to work around Atomic struct member access UB
  Chunk tmp = p->chunks[idx];
  // tmp.buf is client.deliver.chunks.buf, preallocated in Client arena by
  // `make_Product` c->buf is workshop.pending.buf, preallocated in Workshop
  // arena by `launch`
  // printf("\n🔍 %td/%td\n", c->len, c->cap);
  // FIXME  2025-10-01 12:08:07 eventually fails with read deref high value
  // address which I think is actually "read" of dest?!
  copy(tmp.buf, c->buf, c->len);
  tmp.len = c->len;
  tmp.then = c->then;
  tmp.finished = c->finished;
  p->chunks[idx] = tmp;
  queue_push_commit(&p->q); // ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴

  // Reset chunk for reuse! FIXME 2025-09-30 15:15:06 Error-prone
  c->len = 0;
  c->then = WRITE;
  c->finished = 0;
  // keep buf, cap, dest
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
    if (c->len == c->cap) flushc(c);
  }
  return total_copied;
}

void finishc(Chunk *c, enum direction then) {
  c->finished = 1;
  c->then = then;
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
    finishc(out, WRITE); // TODO 2025-09-30 11:41:06 BOTH if websocket...
  } else {
    s8 crlf = s8("\r\n");
    // TODO 2025-09-30 15:43:44 SSE: finishc with `WRITE`
    // Would need to decide where to track work generator. How would
    // subsequent Fetch requests ([necessarily?] new Client connections)
    // cause feedback on an established SSE channel? Tracking server->clients.
    res = add_headers(store, scratch, res); // reassigning to pass-by-value parameter
    s8map *header = res.headers;
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
    printf("Finishing %td/%td \n", out->len, out->cap);
    finishc(out, READ);
    printf("📣 %i\n", res.status);
  }
  // Reset! FIXME 2025-10-01 12:04:13 error prone and wrong
  //res.client->store.cur = res.client->store.beg + sizeof(Client); 
}

// TODO 2025-10-01 07:36:32 could work up into general SET_ARRAY macro
b32 add_client(Server *server, Client *client) {
  Client **available = 0; // first zero value (caused by remove_client)
  Client **end = endof(server->clients);
  for (Client **cur = server->clients.buf; cur < end; cur++) {
    if (*cur == client) return 0; // already there
    else if (!*cur) available = cur; // but keep scanning
  }
  if (available) {
    *available = client;
    return 1;
  }
  fprintf(stderr, "Unable to track client!\n");
  return 0;
}

b32 remove_client(Server *server, Client *client) {
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

// signature cosplay for consistency
void client_set_readable(EV_P_ ev_io *w, b32 readable) {
  Client *client = (Client *)w->data;
  ev_io *read_io = &client->read_io;
  // if (readable == ev_is_active(read_io)) printf("Inconsistent client %s readable call\n", readable ? "set" : "unset");
  if (readable) ev_io_start(EV_A_ read_io);
  else ev_io_stop(EV_A_ read_io);
}

void client_set_writable(EV_P_ ev_io *w, b32 writable) {
  Client *client = (Client *)w->data;
  ev_io *write_io = &client->write_io;
  // if (writable == ev_is_active(write_io)) printf("Inconsistent client %s writable call\n", writable ? "set" : "unset");
  if (writable) ev_io_start(EV_A_ write_io);
  else ev_io_stop(EV_A_ write_io);
}

const static s8 HTTP_OOM = s8("HTTP/1.1 503 Service Unavailable\r\n");

// FIXME 2025-05-25 15:28:50 need to close client gracefully when arena
// available below a threshold, or timeout since last read... 
// TODO 2025-05-25 15:31:23 rate limitation, or leave it to nginx?
void unavailable(i32 sock, char *msg) {
  fprintf(stderr, "💣 Failed to %s\n", msg);
  write(sock, HTTP_OOM.buf, HTTP_OOM.len);
}

/*
  Runs on main thread.
 */
void write_client(EV_P_ ev_io *w, i32 events) {
  Client *client = (Client *)w->data;
  arena scratch = client->scratch; 
  Product *p = &client->deliver;

  i32 idx = queue_pop(&p->q, p->cap);
  if (idx < 0) {
    // printf("Queue empty\n"); // e.g. 20x... TODO 2025-10-01 08:36:33 is this wasteful?
    return;
  }
  Chunk c = p->chunks[idx]; // c.dest is irrelevant here
  u8 *buf = new (&scratch, u8, client->server->config.chunk_size);
  if (!buf) {
    unavailable(w->fd, "allocate out buffer");
    cleanup_client(EV_A_ w);
    return;
  }
  copy(buf, c.buf, c.len); // defensive copy, would be hard to debug if winged it
  queue_pop_commit(&p->q);

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
        cleanup_client(EV_A_ w);
      }
    } else {
      total_bytes_written += bytes_written;
    }
    if (total_bytes_written < c.len) {
      printf("Incomplete socket write (%li/%li B), trying to continue.\n", bytes_written, c.len);
    } else break;
  }
  if (c.finished)
    printf(
        "✅ Done, %ti B written, %ti KiB client arena use for %p (%d clients)\n",
        total_bytes_written, used(&client->store)/KiB(1), (void *)client,
        count_clients(client->server));
  else if (c.len)
    printf("➡️ Chunk written, message not finished %td B\n",
           c.len); // spacing required for terminal...?
  // Only legitimate empty is when finished, to set direction.
  else fprintf(stderr, "Erroneously wrote no data to client.\n");
  
  switch (c.then) { // clang exhaustiveness checking ftw
  case WRITE:
    client_set_writable(EV_A_ w, 1);
    client_set_readable(EV_A_ w, 0);
    break;
  case READ:
    client_set_writable(EV_A_ w, 0);
    client_set_readable(EV_A_ w, 1);
    break;
  case BOTH:
    client_set_writable(EV_A_ w, 1);
    client_set_readable(EV_A_ w, 1);
    break;
  }
}

b32 enqueue_request(Request req) {
  Server *server = req.client->server;
  i32 idx = queue_push(&server->work.q, server->work.cap);
  //printf("queue_push position %i\n", idx);
  if (idx < 0) return 0; // queue full
  server->work.requests[idx] = req;
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
    // printf("read client closed cleanup\n");
    cleanup_client(EV_A_ w);
  } else if (bytes_read < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // nothing to read yet
    } else {
      perror("Error reading client");
      cleanup_client(EV_A_ w);
    }
  } else {
    // TODO handle large read, e.g. stream to arena until finished or excessive,
    // then handle? For now, store (copy) request in client store arena.
    s8_ raw = s8clone(&client->store, s8bytespan(client->scratch.beg, client->scratch.cur));
    if (!raw.ok) {
      unavailable(w->fd, "store raw request");
      cleanup_client(EV_A_ w);
      return;
    }
    // s8arenaprintf(&client->scratch, "🔔 %s\n");
    client->scratch.cur = client->scratch.beg; // Reset!
    Request req = parse_request(&client->store, client->scratch, raw.v);
    s8writefd(1, s8("🔔 "));
    s8log(1, req.uri);
    req.client = client;
    if (!enqueue_request(req)) {
      unavailable(w->fd, "enqueue job");
      cleanup_client(EV_A_ w);
      return;
    }
    client_set_readable(EV_A_ w, 0); // enforce half-duplex! see worker fn
    client_set_writable(EV_A_ w, 1); // unset in write_client when actually finished
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
    
    ev_io_init(&client->read_io, read_client, new_socket, EV_READ);
    client->read_io.data = client; // I'm a woozie (see libev doc)

    // TODO 2025-09-30 09:04:44 make cap configurable
    Product_ deliver = make_Product(&client->store, 31, server->config.chunk_size);
    if (deliver.ok) client->deliver = deliver.v;
    else {
      unavailable(new_socket, "allocate out queue");
      client_cleanup_basics(&client->store, &client->scratch, new_socket);
      return;
    }
    add_client(server, client);
    client_set_readable(EV_A_ &client->read_io, 1);
      
    // This is started and stopped conditionally on whether there is data to
    // write, to prevent excessive activation...
    // https://buildmage.com/blog/libev-tutorial-and-wrapper
    ev_io_init(&client->write_io, write_client, new_socket, EV_WRITE);
    client->write_io.data = client;
    // ...so deliberately not starting here.
  }
}

void *worker(Workshop *workshop) {
  Server *server = workshop->server;
  
  i32 qi = 0;
  u32 save = 0;
  Request req = {0};

  /*
  // ─────────────────────────────────────────────────────── concurrent de-queue
  // ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ unify this
  do {
    do {
      qi = queue_mpop(&server->work.q, server->work.len, &save);
    } while (qi < 0); // FIXME busy wait should sleep instead
    job = server->work.jobs[qi];
  } while (!queue_mpop_commit(&server->work.q, save));
  // ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ with this
  pthread_mutex_lock(&server->work_waiting_lock); // this thread will be the only one waiting for work
  while ((qi = queue_mpop(&server->work.q, server->work.len, &save)) < 0)
    // loop to cover suprious wakeup
    pthread_cond_wait(&server->work_waiting, &server->work_waiting_lock); // blocks thread instead of busy-waiting
  pthread_mutex_unlock(&server->work_waiting_lock);
  */
  // ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ as follows
  while (1) {
    pthread_mutex_lock(&server->work_waiting_lock);
    while ((qi = queue_mpop(&server->work.q, server->work.cap, &save)) < 0)
      // loop to cover suprious wakeup
      pthread_cond_wait(&server->work_waiting, &server->work_waiting_lock);
    pthread_mutex_unlock(&server->work_waiting_lock);
    req = server->work.requests[qi];
    if (queue_mpop_commit(&server->work.q, save)) {
      Client *client = req.client;
      Response res = {0};
      switch (req.error) {
      case SERVICE_UNAVAILABLE: // mainly being some disaster allocating memory
        unavailable(client->write_io.fd, "parse request");
        cleanup_client(server->loop, &client->write_io);
        break;
      case BAD_REQUEST: // TODO 2025-09-29 16:12:55 fall throughs relating only to request parsing
        res = (Response){.status = req.error};
        break;
      default:
        if (req.error) printf("Disregarding Request.error status %d.\n", req.error);
        // NB 2025-09-29 16:13:45 handler is currently also responsible for routing!

        // https://developer.mozilla.org/en-US/docs/Web/HTTP/Guides/Connection_management_in_HTTP_1.x
        // Electing not to implement pipelining ("not activated by
        // default in modern browsers"!), or HTTP/2 or /3. Client can
        // open multiple connections (resulting in multiple jdfhttp
        // Clients, probably served by different workers/threads).

        // Multiple workers would therefore not serialise to the same
        // client->deliver queue simultaneously. Pipelining is
        // prevented by half-duplex using client_set_readable. Writer
        // is on main thread so libev can deal with delays writing.
        // client->deliver should buffer 32KiB.
        res = server->handler(&workshop->store, workshop->scratch, req);
      }
      res.client = client;
      workshop->pending.dest = &client->deliver;
      // TODO 2025-09-29 22:21:10 some server-level notion of Client for session state.
      serialise_response(workshop, res);
      // Reset back to just original workshop.pending.buf allocation from `launch`.
      // FIXME 2025-09-30 15:40:08 error prone
      workshop->store.cur = workshop->store.beg + server->config.chunk_size;
    }
  }
}

typedef void *(*Worker)(void *);

Work_ make_Work(arena *a, i32 cap) {
  Work_ nil = (Work_){0};
  cap = queue_capacity(cap);
  if (!cap) return nil;
  i32 len = cap + 1; 
  _Atomic Request *requests = new (a, _Atomic Request, len);
  if (!requests) return nil;
  return (Work_) { .v = {.requests = requests, .cap = cap, .q = 0} };
}

void sigint_cb(EV_P_ ev_signal *w, i32 events) {
  fprintf(stderr, "SIGINT\n");
  ev_break (EV_A_ EVBREAK_ALL);
}

void launch(Server *server) {
  server->store = alloc_arena(server->config.server_mem);
  server->scratch = alloc_arena(server->config.server_mem);
  if (!server->store.beg || !server->scratch.beg)
    fprintf(stderr, "💣 Failed to allocate %td KB server arenas.", server->config.server_mem/KiB(1));
  i32 nw = server->config.workers;
  i32 rc = 0;
  Workshop *workshops = new (&server->store, Workshop, nw);
  if (!workshops) {
    fprintf(stderr, "💣 Failed to allocate %td workshops\n", nw);
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
  }
  server->workshops.buf = workshops;
  size successful = 0;
  for (size i = 0; i < nw; i++)
    if (!(rc = pthread_create(&(workshops[i].thread), 0, (Worker)worker,
                              &workshops[i]))) {
      server->workshops.len = ++successful;
    } else {
      fprintf(stderr, "Unable to create thread %td: %i\n", i, rc);
      if (i == 0) exit(1); // TODO could provide single threaded impl?
      break;
    }
  printf("Set up %ti workshops\n", successful);
  Work_ work = make_Work(&server->store, 31);
  if (!work.ok) {
    fprintf(stderr, "💣 Failed to make work queue\n");
    exit(1);
  }
  server->work = work.v;
  server->work_waiting = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
  pthread_mutex_init(&server->work_waiting_lock, 0);

  Clients_ track = make_Clients(&server->store, server->config.clients); 
  if (!track.ok) {
    fprintf(stderr, "💣 Failed to allocate client tracking array for %d clients\n",
            server->config.clients);
    exit(1);
  }
  server->clients = track.v;
  printf("Internal memory usage will be %td-%td MiB\n", // excludes libraries
         (server->config.server_mem * 2
          + server->config.client_mem * 2 * 0
          + server->config.worker_mem * 2 * server->config.workers) / MiB(1),
         (server->config.server_mem * 2
          + server->config.client_mem * 2 * server->config.clients
          + server->config.worker_mem * 2 * server->config.workers) / MiB(1));
  
  server->loop = ev_loop_new(0);
  set_non_blocking(server->socket);

  ev_io accept_watcher;
  ev_io_init(&accept_watcher, accept_client, server->socket, EV_READ);
  accept_watcher.data = server; // allows access within callbacks
  ev_io_start(server->loop, &accept_watcher);

  ev_signal signal_watcher;
  ev_signal_init(&signal_watcher, sigint_cb, SIGINT);
  signal_watcher.data = server;
  ev_signal_start(server->loop, &signal_watcher);

  ev_run(server->loop, 0);
  ev_loop_destroy(server->loop);

  // TODO is it necessary to join/kill workers? do they need enclosing while(running) loop?
}

#endif // jdfhttp_h
