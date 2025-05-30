// No, don't! Use mongoose instead!
// Starting at
// https://medium.com/@justup1080/tutorial-creating-a-minimalist-http-server-in-c-2303d140c725
// https://hoad.io/libev-is-neat/

#include "jdf.h" // TODO remove if want flexibility of choosing relptr.h; may not be worth matching APIs though
// #include "relptr.h"
#include "http_codes.h"
#include <ev.h>

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
  fflush(0);
}

// Associate cloned k & v.
s8map *s8mapassocl(arena *store, s8map *head, s8 k, s8 v) {
  s8_ kc = s8clone(store, k);
  s8_ vc = s8clone(store, v);
  if (kc.ok && vc.ok) return s8mapassoc(store, head, kc.v, vc.v);
  printf("Problem setting "); dumbp(k);
  return head;
}

typedef struct server Server; // forward decl for Request and Workshop
typedef struct client Client; // forward decl for Request

typedef struct {
  s8 raw;
  enum http_status error; // in anticipation...
  enum http_method method;
  s8 uri;
  s8 protocol;
  s8map *headers;
  s8map *cookies;
  s8 body;
  Client *client;
} Request;
  
typedef struct {
  i32 status; // http status
  s8map *headers; // does not accommodate repeat keys, which are permitted by http spec https://stackoverflow.com/a/4371395/780743
  s8map *cookies; 
  s8l *body;
} Response;

typedef Response (*Handler)(arena *store, arena scratch, Request req);
//                 ^^^^^^^

typedef struct {
  i32 domain; // PF_INET or PF_UNIX protocol families ~aka address families
  i32 port;
  i32 backlog; // max pending connection queue length
  u32 interface;
  size server_mem;
  size client_mem;
  size worker_mem;
  size outbuf;
  arena store;
  arena scratch;
} Config;

typedef struct { // Resources for one worker!
  Server *server;
  arena store;
  arena scratch;
  pthread_t thread;
} Workshop;

ARRAY(Workshops, Workshop)

typedef struct { // Concurrent queue (multiple consumer)
  _Atomic Request *requests;
  // simpler than _Atomic Requests *requests from ARRAY(Requests, Request)
  // because _Atomic struct member access is UB:
  size len; // this is 1 more than the number of requests!!
  queue q;
} Work;

MAYBE(Work)

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
} Server;

typedef struct client {
  Server *server;
  arena store;
  arena scratch;
  ev_io read_io;
  ev_io write_io;
  qout deliver;
} Client;

// ────────────────────────────────────────────────────────────────────── Server
Server make_server_fn(Handler h, Config c) {
  arena store = alloc_arena(MiB(1));
  arena scratch = alloc_arena(MiB(1));
  if (!store.beg || !scratch.beg) failwith(1, s8("Failed to allocate server arenas."));
  Server server = {
    .config = c,
      // learn about SOCK_DGRAM, SOCK_RAW types later
    .socket = socket(c.domain, SOCK_STREAM, 0), // 0 is IP, internet protocol!
    .address = {.sin_family = c.domain,
                .sin_port = htons(c.port), // convert byte order
                .sin_addr = {.s_addr = htonl(c.interface)}},
    .store = store,
    .scratch = scratch,
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

#define make_server(h, ...) /* default config */      \
  make_server_fn(h, (Config){.domain = PF_INET,       \
                             .backlog = 10,           \
                             .interface = INADDR_ANY, \
                             .server_mem = MiB(1),    \
                             .client_mem = MiB(1),    \
                             .worker_mem = MiB(1),    \
                             .outbuf = KiB(4),        \
                             __VA_ARGS__})

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
  if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (byte *)&yes, sizeof(i32)) <
      0) {
    perror("Failed to set nodelay");
    exit(1);
  }
  return 0;
}

#define ReqErr(e) do { req.error = e; return req; } while (0) // macrology semicolon hack

// would be "better" to use llhttp (which depends on llvm...)
Request parse_request(arena *store, arena scratch, s8 raw) {
  Request req = {.raw = raw};
  // TODO 2025-05-30 22:54:19 could rewrite using s8cut and compare readability
  s8a_ split =
    s8splitu8(store, scratch, raw, '\n', 100);
  if (!split.ok) ReqErr(SERVICE_UNAVAILABLE);
  if (split.v.len < 1) ReqErr(BAD_REQUEST);
  s8a_ line0 = s8splitu8(store, scratch, split.v.buf[0], ' ', 2);
  if (!line0.ok) ReqErr(SERVICE_UNAVAILABLE);
  if (line0.v.len < 3) ReqErr(BAD_REQUEST);
  req.method = parse_http_method(line0.v.buf[0]);
  if (!req.method) ReqErr(METHOD_NOT_ALLOWED);
  req.uri = line0.v.buf[1]; // copy s8, zerocopy its buffer
  req.protocol = line0.v.buf[2];
  s8map *headers = {0};
  for (size i = 1; i < split.v.len; i++) {
    if (s8blank(split.v.buf[i])) break; // TODO trailing headers...??
    s8a_ header = s8split(store, scratch, split.v.buf[i], s8(": "), 1);
    if (!header.ok) ReqErr(SERVICE_UNAVAILABLE);
    if (header.v.len == 2)
      headers = s8mapassoc(store, headers, header.v.buf[0], header.v.buf[1]);
    req.headers = headers;
  }
  s8map *cookies = {0};
  // e.g. Cookie: name=value; name2=value2; name3=value3
  s8map *cookiekv = s8mapget(headers, s8("Cookie"));
  if (cookiekv) {
    s8a_ cookiekvs = s8split(store, scratch, cookiekv->val, s8("; "), 32);
    if (!cookiekvs.ok) ReqErr(SERVICE_UNAVAILABLE);
    for (size i = 0; i < cookiekvs.v.len; i++) {
      s8pair cookie = s8cutu8(cookiekvs.v.buf[i], '=');
      if (!cookie.ok) ReqErr(SERVICE_UNAVAILABLE);
      cookies = s8mapassoc(store, cookies, cookie.head, cookie.tail);
    }
    req.cookies = cookies;
  }
  return req;
}

s8map *content_type(arena *store, s8map *head, enum content_type content_type) {
  s8mapassoc(store, head, s8("Content-Type"),
             (s8){.buf = (u8 *)spell_content_type[content_type]});
  return head;
}

// Terminates string in situ!
size s8arenaprintf(arena *a, const char *format) {
  if (a->cur < a->end) *a->cur = 0;
  else {
    const char *warning = "❗️(too long for buffer)";
    snprintf(a->end - sizeof(warning), sizeof(warning), "%s", warning);
  }
  return printf(format, a->beg);
}

Response add_headers(arena *store, arena scratch, Response res) {
  // TODO should be conditional on client's invitation
  res.headers = s8mapassocl(store, res.headers, s8("Connection"), s8("keep-alive"));
  if (!res.body) return res;
  s8 k = s8("Content-Length");
  s8_ v = s8sprintf(&scratch, "%ti", s8llen(res.body));
  if (v.ok) res.headers = s8mapassocl(store, res.headers, k, v.v);
  else fprintf(stderr, "Error setting Content-Length\n");
  //printf("✏ Expecting Content-Length: %td\n", s8llen(res.body));
  return res;
}

size s8writeq(void *out, s8 s) {
  qout *q = (qout *)out;
  if (!q->buf.buf) {
    fprintf(stderr, "💣 Tried to write to uninitialised qout\n");
    return 0;
  }
  if (!s.buf) {
    // perror("Tried to write unitialised string to qout");
    return 0;
  }
  // dumbp(s);
  size bytes_written = write_qout(q, s.buf, s.len);
  if (bytes_written < s.len) 
    // NB 2025-05-25 13:30:51 doesn't seme to be happening
    printf("⚠ only wrote %td/%td bytes\n", bytes_written, s.len);
  return bytes_written;
}

/*
  Runs on worker thread. Expect to block when qout full, until client loop
  drains it from the main thread.
*/
void serialise_response(arena *store, arena scratch, Client *client, Response res) {
  qout *out = &client->deliver; // FIXME 2025-05-25 12:08:25 vuln to UAF? Can't reproduce
  // FIXME 2025-05-25 12:09:08 
  s8 crlf = s8("\r\n");
  res = add_headers(store, scratch, res); // reassigning to pass-by-value arg
  s8map *header = res.headers;
  s8printf(scratch, s8writeq, out, "HTTP/1.1 %i %s\r\n",
           res.status, spell_http_status[res.status]); // TODO adapt to make_constants stuff when ready
  do { // grug approve
    s8writeq(out, header->key);
    s8writeq(out, s8(": "));
    s8writeq(out, header->val);
    s8writeq(out, crlf);
  } while ((header = header->next));
  s8writeq(out, crlf);
  for (s8l *node = res.body; node; node = node->next)
    s8writeq(out, node->val);
  s8writeq(out, s8("\0")); // message finished
  printf("📣 %i\n", res.status);
}

void client_cleanup_basics(arena *store, arena *scratch, i32 fd) {
  close(fd);
  free_arena(scratch); // needs to be freed first because *client itself is within client->store span
  free_arena(store);
}

void cleanup_client(EV_P_ ev_io *w) {
  Client *client = (Client *)w->data;
  // https://metacpan.org/dist/EV/view/libev/ev.pod#ev_TYPE_stop-(loop,-ev_TYPE-*watcher)
  ev_io_stop(EV_A_ &client->read_io);
  ev_io_stop(EV_A_ &client->write_io);
  client_cleanup_basics(&client->store, &client->scratch, w->fd);
}

// signature cosplay for consistency
void client_set_writable(EV_P_ ev_io *w, b32 writable) {
  Client *client = (Client *)w->data;
  ev_io *write_io = &client->write_io;
  if (writable == ev_is_active(write_io))
    printf("Inconsistent client %s writable call\n", writable ? "set" : "unset");
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
  qout *qo = &client->deliver;
  size outbuf_size = client->server->config.outbuf;
  arena scratch = client->scratch; // by value
  u8 *buf = new (&scratch, u8, outbuf_size);
  if (!buf) {
    unavailable(w->fd, "allocate out buffer");
    cleanup_client(EV_A_ w);
    return;
  }
  size bytes_read = 0;
  b32 complete = 0;
  while (!complete && bytes_read < outbuf_size) {
    // pop a byte at time from qo into local buffer
    i32 qidx = queue_pop(&qo->q, qo->buf.len);
    if (qidx < 0) {
      //printf("qout empty\n"); // NB 2025-05-25 14:25:18 happens minimum once per request
      break; // empty
    }
    u8 b = qo->buf.buf[qidx];
    if (b) buf[bytes_read++] = b;
    else complete = 1; // message finished as indicated by \0
    queue_pop_commit(&qo->q);
  }
  // FIXME ?wait for queue to be readable ?why necessary
  if (bytes_read == 0) return;
  size total_bytes_written = 0;
  size bytes_written = 0;
  while (1) {
    bytes_written = write(w->fd, buf, bytes_read);
    if (bytes_written == 0) { // TODO check semantics, client closed connection?
      printf("Write client wrote nothing\n");
      //cleanup_client(EV_A_ w);
    } else if (bytes_written < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // just try again? NB don't lose qo data!
        printf("Should try again?\n");
      } else {
        perror("Error writing to client");
        cleanup_client(EV_A_ w);
      }
    } else {
      total_bytes_written += bytes_written;
      // printf("Wrote %td/%td bytes\n", bytes_written, total_bytes_written); // NB 2025-05-25 14:29:12 generally in one go
    }
    if (total_bytes_written < bytes_read) {
      printf("Incomplete socket write (%li/%li B), trying to continue.\n", bytes_written, bytes_read);
    } else break;
  }
  // TODO handle arena oom... how?
  if (complete) { 
    printf("✅ Done, %ti B written, %ti B client arena use\n", total_bytes_written, used(&client->store));
    client_set_writable(EV_A_ w, 0); // unset writable when write actually finished
  } else printf("➡️  Partial read %td B\n", bytes_read); // spacing required for terminal...?
}

b32 enqueue_request(Request req) {
  Server *server = req.client->server;
  i32 idx = queue_push(&server->work.q, server->work.len);
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
    // Allocate arenas TODO monitor usage, tune
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
    qout_ deliver = make_qout(&client->store, server->config.outbuf);
    if (deliver.ok) client->deliver = deliver.v;
    else {
      unavailable(new_socket, "allocate out queue");
      client_cleanup_basics(&client->store, &client->scratch, new_socket);
      return;
    }
    ev_io_start(EV_A_ & client->read_io);
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
  pthread_mutex_lock(&server->work_waiting_lock);
  while ((qi = queue_mpop(&server->work.q, server->work.len, &save)) < 0)
    // loop to cover suprious wakeup
    pthread_cond_wait(&server->work_waiting, &server->work_waiting_lock);
  pthread_mutex_unlock(&server->work_waiting_lock);
  */
  while (1) {
    pthread_mutex_lock(&server->work_waiting_lock);
    while ((qi = queue_mpop(&server->work.q, server->work.len, &save)) < 0)
      // loop to cover suprious wakeup
      pthread_cond_wait(&server->work_waiting, &server->work_waiting_lock);
    pthread_mutex_unlock(&server->work_waiting_lock);
    req = server->work.requests[qi];
    if (queue_mpop_commit(&server->work.q, save)) {
      Client *client = req.client;
      if (req.error == SERVICE_UNAVAILABLE) {
        unavailable(client->write_io.fd, "parse request");
        cleanup_client(server->loop, &client->write_io);
      } // TODO 2025-05-25 16:02:09 REQUEST_EMPTY, INVALID_METHOD_LINE, ...
      Response res = server->handler(&workshop->store, workshop->scratch, req);
      serialise_response(&workshop->store, workshop->scratch, client, res);
    }
  }
}

#ifdef _WIN32
#include <sysinfoapi.h>
i32 nprocs(void) {
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

typedef void *(*Worker)(void *);

void sigint_cb(EV_P_ ev_signal *w, i32 events) {
  fprintf(stderr, "SIGINT\n");
  ev_break (EV_A_ EVBREAK_ALL);
}

Work_ make_Work(arena *a, i32 len) {
  Work_ nil = (Work_){0};
  i32 cap = queue_capacity(len);
  if (!cap) return nil;
  _Atomic Request *requests = new (a, _Atomic Request, cap);
  if (!requests) return nil;
  return (Work_) { .v = {.requests = requests, .len = len, .q = 0} };
}

void launch(Server *server) {
  server->store = alloc_arena(server->config.server_mem);
  server->scratch = alloc_arena(server->config.server_mem);
  i32 np = nproc();
  i32 nw = np == 1 ? np : np - 1;
  i32 rc = 0;
  Workshop *workshops = new (&server->store, Workshop, nw); // seemingly > 8KiB ea
  if (!workshops) {
    fprintf(stderr, "💣 Failed to allocate workshops\n");
    exit(1);
  }
  for (size i = 0; i < nw; i++) {
    workshops[i].server = server;
    workshops[i].store = alloc_arena(server->config.worker_mem);
    workshops[i].scratch = alloc_arena(server->config.worker_mem);
  }
  server->workshops.buf = workshops;
  size successful = 0;
  for (size i = 0; i < nw; i++)
    if (!(rc = pthread_create(&(workshops[i].thread), 0, (Worker)worker,
                              &workshops[i]))) {
      server->workshops.len = successful++;
      
    } else {
      fprintf(stderr, "Unable to create thread %td: %i\n", i, rc);
      if (i == 0) exit(1); // TODO could provide single threaded impl?
      break;
    }
  printf("Set up %ti workshops\n", successful);
  Work_ work = make_Work(&server->store, 32);
  if (!work.ok) {
    fprintf(stderr, "💣 Failed to make work queue\n");
    exit(1);
  }
  server->work = work.v;
  server->work_waiting = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
  pthread_mutex_init(&server->work_waiting_lock, 0);
  
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
