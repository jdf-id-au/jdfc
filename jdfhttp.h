// No, don't! Use mongoose instead!
// Starting at
// https://medium.com/@justup1080/tutorial-creating-a-minimalist-http-server-in-c-2303d140c725
// https://hoad.io/libev-is-neat/

#include "jdf.h" // TODO remove if want flexibility of choosing relptr.h; may not be worth matching APIs though
//#include "relptr.h"
#include <ev.h>

#ifndef jdfhttp_h
#define jdfhttp_h

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <pthread.h>

enum http_method { // https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Methods
  INVALID_METHOD, GET, HEAD, POST, PUT, DELETE, CONNECT, OPTIONS, TRACE, PATCH
};

// TODO macrology?
const char *spell_method[] = {
    [INVALID_METHOD] = "",
    [GET] = "GET",
    [HEAD] = "HEAD",
    [POST] = "POST",
    [PUT] = "PUT",
    [DELETE] = "DELETE",
    [CONNECT] = "CONNECT",
    [OPTIONS] = "OPTIONS",
    [TRACE] = "TRACE",
    [PATCH] =  "PATCH"
};

#define s8unsafe(s) (s8){ .buf = (u8 *)s, .len = strlen(s) }

enum http_method parse_method(s8 s) {
  for (size i = 1; i < (size)PATCH; i++) 
    if (s8equal(s, s8unsafe(spell_method[i]))) // should be safe because literal??
      return (enum http_method)i;
  return INVALID_METHOD;
}

// Terminates string in situ! Only suitable for arenas being prepared for s8arena.
size s8arenaprintf(arena *a, const char *format) {
  if (a->cur < a->end) *a->cur = 0;
  else {
    const char *warning = "❗️(too long for buffer)";
    snprintf(a->end - sizeof(warning), sizeof(warning), "%s", warning);
  }
  return printf(format, a->beg);
}

enum http_status { // https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Status
  // incomplete list
  OK = 200,
  CREATED,
  ACCEPTED,
  NON_AUTHORITATIVE_INFORMATION,
  NO_CONTENT,
  RESET_CONTENT,
  PARTIAL_CONTENT,
  MULTIPLE_CHOICES = 300,
  MOVED_PERMANENTLY,
  FOUND,
  SEE_OTHER,
  NOT_MODIFIED,
  TEMPORARY_REDIRECT = 307,
  PERMANENT_REDIRECT,
  BAD_REQUEST = 400,
  UNAUTHORIZED,
  PAYMENT_REQUIRED,
  FORBIDDEN,
  NOT_FOUND,
  METHOD_NOT_ALLOWED,
  NOT_ACCEPTABLE,
  GONE = 410,
  LENGTH_REQUIRED,
  PRECONDITION_FAILED,
  CONTENT_TOO_LARGE,
  URI_TOO_LONG,
  UNSUPPORTED_MEDIA_TYPE,
  RANGE_NOT_SATISFIED,
  EXPECTATION_FAILED,
  IM_A_TEAPOT,
  TOO_MANY_REQUESTS = 429,
  INTERNAL_SERVER_ERROR = 500,
  NOT_IMPLEMENTED,
  BAD_GATEWAY,
  SERVICE_UNAVAILABLE,
  GATEWAY_TIMEOUT,
  HTTP_VERSION_NOT_SUPPORTED
};

const char *spell_http_status[] = {
    [OK] = "OK",
    [NOT_FOUND] = "Not Found"
};

enum content_type {
  TEXT_HTML
};

const char *spell_content_type[] = {
  [TEXT_HTML] = "text/html; charset=UTF-8"
};

MAP_LIST(s8map, s8, s8, s8equal)

enum request_error {
  REQUST_OK, REQUEST_OOM, REQUEST_EMPTY, INVALID_METHOD_LINE 
};
  
typedef struct {
  s8 raw;
  enum request_error error;
  enum http_method method;
  s8 uri;
  s8 protocol;
  s8map *headers;
  s8map *cookies;
  s8 body;
} Request;
  
typedef struct {
  int status; // http status
  s8map *headers; // does not accommodate repeat keys, which are permitted by http spec https://stackoverflow.com/a/4371395/780743
  s8map *cookies; 
  s8 body;
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
  size outbuf;
  arena store;
  arena scratch;
} Config;

typedef struct server Server; // forward decl for Workshop

typedef struct { // Resources for one worker!
  Server *server;
  arena store;
  arena scratch;
  pthread_t thread;
} Workshop;

ARRAY(Workshops, Workshop)

typedef struct client Client; // forward decl for Work->Job

typedef struct {
  Client *client;
  Request req;
} Job;

typedef struct { // Concurrent queue (multiple consumer)
  _Atomic Job *jobs;
  // simpler than _Atomic Jobs *jobs from ARRAY(Jobs, Job):
  size len; // because _Atomic struct member access is UB
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
  int yes = 1; // allow faster relaunch
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
                             .outbuf = KiB(4),        \
                             __VA_ARGS__})

int set_non_blocking(int sockfd) {
  int flags = fcntl(sockfd, F_GETFL, 0);
  if (fcntl(sockfd, F_SETFL, (flags < 0 ? 0 : flags) | O_NONBLOCK) == -1) {
    perror("Failed to set nonblocking");
    exit(1);
  }
  return 0;
}

#define ReqErr(e) do { req.error = e; return req; } while (0) // macrology semicolon hack

// would be "better" to use llhttp (which depends on llvm...)
Request parse_request(arena *store, arena scratch, s8 raw) {
  Request req = { .raw = raw };
  s8s_ split = s8splitu8(store, scratch, raw, '\n', 100);
  if (!split.ok) ReqErr(REQUEST_OOM);
  if (split.v.len < 1) ReqErr(REQUEST_EMPTY);
  s8s_ line0 = s8splitu8(store, scratch, split.v.buf[0], ' ', 2);
  if (!line0.ok) ReqErr(REQUEST_OOM);
  if (line0.v.len < 3) ReqErr(INVALID_METHOD_LINE);
  req.method = parse_method(line0.v.buf[0]);
  req.uri = line0.v.buf[1]; // copy s8, zerocopy its buffer
  req.protocol = line0.v.buf[2];
  s8map *headers = {0};
  for (size i = 1; i < split.v.len; i++) {
    if (s8blank(split.v.buf[i])) break; // TODO trailing headers...??
    s8s_ header = s8split(store, scratch, split.v.buf[i], s8(": "), 1);
    if (!header.ok) ReqErr(REQUEST_OOM);
    if (header.v.len == 2)
      headers = s8mapassoc(store, headers, header.v.buf[0], header.v.buf[1]);
    req.headers = headers;
  }
  s8map *cookies = {0};
  // e.g. Cookie: name=value; name2=value2; name3=value3
  s8map *cookiekv = s8mapget(headers, s8("Cookie"));
  if (cookiekv) {
    s8s_ cookiekvs = s8split(store, scratch, cookiekv->val, s8("; "), 32);
    if (!cookiekvs.ok) ReqErr(REQUEST_OOM);
    for (size i = 0; i < cookiekvs.v.len; i++) {
      s8s_ cookie = s8splitu8(store, scratch, cookiekvs.v.buf[i], '=', 1);
      if (!cookie.ok) ReqErr(REQUEST_OOM);
      if (cookie.v.len == 2)
        cookies = s8mapassoc(store, cookies, cookie.v.buf[0], cookie.v.buf[1]);
    }
    req.cookies = cookies;
  }
  return req;
}

s8map *content_type(arena *store, s8map *head, enum content_type content_type) {
  s8mapassoc(store, head, s8("Content-Type"),
             (s8){ .buf = (u8 *)spell_content_type[content_type]});
}

// Associate cloned k & v.
s8map *s8mapassocl(arena *store, s8map *head, s8 k, s8 v) {
  s8_ kc = s8clone(store, k);
  s8_ vc = s8clone(store, v);
  if (kc.ok && vc.ok) return s8mapassoc(store, head, kc.v, vc.v);
  s8log(2, s8("Error setting "), 0);
  s8log(2, k, 1);
  return head;
}

Response add_headers(arena *store, arena scratch, Response res) {
  // TODO should be conditional on client's invitation
  res.headers = s8mapassocl(store, res.headers, s8("Connection"), s8("keep-alive"));
  s8 k = s8("Content-Length");
  s8_ v = s8sprintf(&scratch, "%ti", res.body.len);
  if (v.ok) res.headers = s8mapassocl(store, res.headers, k, v.v);
  else s8log(2, s8("Error setting Content-Length"), 1);
  return res;
}

size s8writeq(void *out, s8 s) {
  qout *q = (qout *)out;
  if (!q->buf.buf) {
    perror("Tried to write to uninitialised qout");
    return 0;
  }
  if (!s.buf) {
    perror("Tried to write unitialised string to qout");
    return 0;
  }
  return write_qout(q, s.buf, s.len);
}

void serialise_response(Client *client, Response res) { // TODO rework for client->deliver
  arena *store = &client->store;
  arena scratch = client->scratch;
  qout *out = &client->deliver;
  s8 crlf = s8("\r\n");
  res = add_headers(store, scratch, res); // reassigning to pass-by-value arg; do before store becomes buffer
  s8map *header = res.headers;
  s8printf(scratch, s8writeq, out, "HTTP/1.1 %i %s\r\n", res.status, spell_http_status[res.status]); // TODO adapt to make_constants stuff when ready
  s8_ k = {0};
  s8_ v = {0};
  do {
    k = s8unwrap(&scratch, header->key);
    v = s8unwrap(&scratch, header->val);
    s8printf(scratch, s8writeq, out, "%s: %s\r\n", k, v);
  } while ((header = header->next));
  s8writeq(out, crlf);
  s8writeq(out, res.body);
  printf("📣 %i\n", res.status);
}

void cleanup_client(EV_P_ ev_io *w) {
  Client *client = (Client *)w->data;
  // https://metacpan.org/dist/EV/view/libev/ev.pod#ev_TYPE_stop-(loop,-ev_TYPE-*watcher)
  ev_io_stop(EV_A_ &client->read_io);
  ev_io_stop(EV_A_ &client->write_io);
  close(w->fd);
  free_arena(&client->scratch); // needs to be freed first because *client itself is within client->store span
  free_arena(&client->store);
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

void write_client(EV_P_ ev_io *w, int events) {
  Client *client = (Client *)w->data;

  qout *q = &client->deliver;
  u8 buf[BUFOUTSIZE] = {0};
  for (size i = 0; i < BUFOUTSIZE; i++) {
    i32 qidx = queue_pop(q, QEXP);
    if (qidx < 0) return; // empty; go back to libev
    // TODO is `complete` _Atomic b32 needed?
    
  }
  
  // This is the analog of Wellons' flush and oswrite together

  //  bufout *b = &client->deliver; // TODO reimpl as read from qout
  //  if (!b) return;
  //  if (!b->err && b->len) {
    //    for (i32 off = 0; off < b->len;) { // b->fd redundant with w->fd but clear
      //      i32 written = (i32)write(b->fd, b->buf, b->len - off);
      //      if (written == 0) { // TODO CHECK SEMANTICS client closed connection?
        //        cleanup_client(EV_A_ w);
        //        return;
        //      } else if (written < 0) {
        //        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          //          // keep trying
          //        } else {
          //          perror("Error writing to client");
          //          cleanup_client(EV_A_ w);
          //          return;
          //        }
        //      }
      //      off += written;
      //    }
    //    b->len = 0;
    //  }
  //  ssize_t bytes_written = write(w->fd, client->deliver)
                           //  
                           //  if (!client->deliverable.ok) return; // TODO other handling? retry something?
  //  // TODO how to indicate zero length reply? Meaningless?
    //  s8 chunk = s8slice(client->deliverable.v, 0, BUFOUTSIZE);
  //  if (chunk.len > 0) {
    //    ssize_t bytes_written = write(w->fd, chunk.buf, chunk.len); // this is unsurprisingly just like oswrite...
    //    if (bytes_written == 0) { // TODO CHECK SEMANTICS client closed connection?
      //      // printf("write client closed cleanup\n");
      //      cleanup_client(EV_A_ w);
      //    } else if (bytes_written < 0) {
      //      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        //        // does this just try again?
        //      } else {
        //        perror("Error writing to client");
        //        cleanup_client(EV_A_ w);
        //      }
      //    } else {
      //      client->deliverable.v = s8slice(client->deliverable.v, bytes_written, 0);
      //      // should become multiple writes if deliverable.v.len > BUFOUTSIZE
        //    }
  //  } else {
  //    // Continues to rise as connections stays open... TODO graceful drop if
  //    hits limit

  // 
    //    printf("✅ Done, %ti B client arena use\n", (size)(client->store.cur - client->store.beg));
    //    client_set_writable(EV_A_ w, 0); // unset writable when write actually finished
    //  }
}

b32 enqueue_job(Server *server, Job job) {
  i32 idx = queue_push(&server->work.q, server->work.len);
  if (idx < 0) return idx; // queue full
  server->work.jobs[idx] = job;
  queue_push_commit(&server->work.q);
  pthread_mutex_lock(&server->work_waiting_lock);
  pthread_cond_signal(&server->work_waiting); // worker can just sleep again if queue already emptied
  pthread_mutex_unlock(&server->work_waiting_lock);
  // TODO check pthread_cond_signal to wake a thread to do the work
  // TODO check awake thread will do work from queue without needing to go via sleep
}

void read_client(EV_P_ ev_io *w, int events) {
  Client *client = (Client *)w->data;
  // using scratch arena as a buffer here, instead of local array
  assert(0 == used(&client->scratch));
  ssize_t bytes_read = read(w->fd, client->scratch.beg, remaining(&client->scratch));
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
    // then handle?
    // For now, store (copy) request in client store arena.
    s8_ sa = s8arena(&client->scratch, 0);
    if (!sa.ok) {
      perror("Failed to store raw request"); // TOOD 503
      cleanup_client(EV_A_ w);
      return;
    }
    s8_ raw = s8clone(&client->store, sa.v);
    if (!raw.ok) {
      perror("Failed to store raw request"); // TODO 503
      cleanup_client(EV_A_ w);
      return;
    }
    s8arenaprintf(&client->scratch, "🔔 %s\n");
    client->scratch.cur = client->scratch.beg; // Reset!
    Request req = parse_request(&client->store, client->scratch, raw.v);
    /* TODO concept:
       - validate +- encode request
       - add request to queue, tracking source ?socket 
         (mitigate against recycling!)
       - worker thread/s consume request and add response to another queue
         (queues need mutexes, or maybe Wellons' fancy lockfree queue)
       - server loop sends response to correct socket

       Number of worker threads could be sched_getaffinity() -1 on linux, or
       sysctlbyname("machdep.cpu.core_count") -1 on macOS.
     */
    if (!enqueue_job(client->server, (Job){.client = client, .req = req})) {
      perror("Failed to enqueue job"); // TODO 503
      cleanup_client(EV_A_ w);
      return;
    }
    // TODO do on a worker thread
    //*client->res = client->server->handler(&client->store, client->scratch, req);
    // TODO need to call serialise_response, do it from (one, multi-client, but not busy-waiting) worker thread writing to cilent's qout
    client_set_writable(EV_A_ w, 1); // unset in write_client when actually finished
    // not closing socket
  }
}

void accept_client(EV_P_ ev_io *w, int events) {
  Server *server = (Server *)w->data;
  int addrlen = sizeof(server->address);
  int new_socket = accept(w->fd, // should be same as server->socket
                          (struct sockaddr *)&server->address,
                          (socklen_t *)&addrlen);
  if (new_socket < 0) perror("Socket connection failed");
  else {
    set_non_blocking(new_socket);
    // Allocate arenas TODO monitor usage, tune
    arena client_store = alloc_arena(server->config.client_mem);
    arena client_scratch = alloc_arena(server->config.client_mem);
    // printf("store beg %p\nscratch beg %p\n", (void *)client_store.beg, (void *)client_scratch.beg); fflush(0);
    // https://metacpan.org/dist/EV/view/libev/ev.pod#ASSOCIATING-CUSTOM-DATA-WITH-A-WATCHER
    Client *client = new (&client_store, Client, 1);
    if (!client) {
      perror("Failed to allocate client");
      return; // TODO could give SERVICE_UNAVAILABLE...
    }
    client->server = server;
    client->store = client_store; // for passing by reference
    client->scratch = client_scratch; // for passing by value
    ev_io_init(&client->read_io, read_client, new_socket, EV_READ);
    client->read_io.data = client; // I'm a woozie (see libev doc)
    qout_ deliver = make_qout(&client->store, server->config.outbuf);
    if (deliver.ok) client->deliver = deliver.v;
    else {
      perror("Failed to allocate out buffer");
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
  // TODO serialise to client.deliver
  Server *server = workshop->server;
  
  i32 qi = 0;
  u32 save = 0;
  Job job = {0};

  /*
  // ─────────────────────────────────────────────────────── concurrent de-queue
  do {
    do {
      qi = queue_mpop(&server->work.q, server->work.len, &save);
    } while (qi < 0); // FIXME busy wait should sleep instead
    job = server->work.jobs[qi];
  } while (!queue_mpop_commit(&server->work.q, save));
  // ──────────────────────────────────────────────── FIXME unify with cond wake
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
    job = server->work.jobs[qi];
    if (queue_mpop_commit(&server->work.q, save)) {
      Response res = server->handler(&workshop->store, workshop->scratch, job.req);
      serialise_response(job.client, res);
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
#elif __APPLE__
#include <sys/sysctl.h>
i32 nproc(void) {
  i32 v = 0;
  usize len = 0;
  if (!sysctlbyname("hw.logicalcpu", &v, &len, 0, 0)) // 0 is success
    return v;
  perror("Couldn't get system information");
}
#elif __linux
i32 nprocs(void) { return sysconf(_SC_NPROCESSORS_ONLIN); }
#endif

typedef void *(*Worker)(void *);

void sigint_cb(EV_P_ ev_signal *w, int events) {
  s8log(2, s8("SIGINT"), 1);
  ev_break (EV_A_ EVBREAK_ALL);
}

Work_ make_Work(arena *a, i32 len) {
  Work_ nil = (Work_){0};
  i32 cap = queue_capacity(len);
  if (!cap) return nil;
  _Atomic Job *jobs = new (a, _Atomic Job, cap);
  if (!jobs) return nil;
  return (Work_) { .v = {.jobs = jobs, .len = cap, .q = 0} };
}

void launch(Server *server) {
  server->store = alloc_arena(server->config.server_mem);
  server->scratch = alloc_arena(server->config.server_mem);

  i32 np = nproc();
  i32 nw = np == 1 ? np : np - 1;
  i32 rc = 0;
  
  Workshop *workshops = new (&server->store, Workshop, nw); // seemingly > 8KiB ea
  if (!workshops) {
    perror("Unable to allocate workshops");
    exit(1);
  }
  server->workshops.buf = workshops;
  for (size i = 0; i < nw; i++)
    if ((rc = pthread_create(&(workshops[i].thread), 0, (Worker)worker, &workshops[i]))) {
      fprintf(stderr, "Unable to create thread %td: %i\n", i, rc);
      if (i == 0) exit(1); // TODO could provide single threaded impl?
      server->workshops.len = i;
      break;
    }

  Work_ work = make_Work(&server->store, 32768);
  if (!work.ok) {
    perror("Unable to make work queue");
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
