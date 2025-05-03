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
const char *spell_methods[] = {
  "", "GET", "HEAD", "POST", "PUT", "DELETE", "CONNECT", "OPTIONS", "TRACE", "PATCH"
};

#define s8unsafe(s) (s8){ .buf = (u8 *)s, .len = strlen(s) }

enum http_method parse_method(s8 s) {
  for (size i = 1; i < (size)PATCH; i++) 
    if (s8equal(s, s8unsafe(spell_methods[i]))) // should be safe because literal??
      return (enum http_method)i;
  return INVALID_METHOD;
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

typedef struct {
  int domain;
  int port;
  int backlog;
  u_long interface;
  size client_arena_cap;
  Handler handler;
} Config;

typedef struct {
  int domain;
  int port;
  int service;
  int protocol;
  int backlog;
  int socket;
  struct sockaddr_in address;
  struct ev_loop *loop;
  arena store;
  arena scratch;
  size client_arena_cap;
  Handler handler;
} Server;

#define BUFOUTSIZE 8192 // TODO what's optimal?
typedef struct {
  Server *server;
  arena store;
  arena scratch;
  ev_io read_io;
  ev_io write_io;
  s8_ deliverable; // slice some other s8 on client's arena, no need to retain head here unless redelivery or something
} Client;

Server make_server(Config c) {
  arena store = alloc_arena(MiB(1));
  arena scratch = alloc_arena(MiB(1));
  if (!store.beg || !scratch.beg) failwith(1, s8("Failed to allocate server arenas."));
  Server server = {
    .domain = c.domain, // PF_INET or PF_UNIX protocol families ~aka address families
    .port = c.port,
    .backlog = c.backlog, // max pending connection queue length
                        // learn about SOCK_DGRAM, SOCK_RAW types later
    .socket = socket(c.domain, SOCK_STREAM, 0), // 0 is IP, internet protocol!
    .address = {.sin_family = c.domain,
                .sin_port = htons(c.port), // convert byte order
                .sin_addr = {.s_addr = htonl(c.interface)}},
    .store = store,
    .scratch = scratch,
    .client_arena_cap = c.client_arena_cap,
    .handler = c.handler
  };
  if (server.socket < 0) {
    perror("Socket creation failed");
    exit(EXIT_FAILURE);
  }
  int yes = 1; // allow faster relaunch
  if (setsockopt(server.socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
    perror("Socket option setting failed");
    exit(EXIT_FAILURE);
  }
  if (bind(server.socket,
           (struct sockaddr *)&server.address,
           sizeof(server.address)) < 0) {
    perror("Socket binding failed");
    exit(EXIT_FAILURE);
  }
  if (listen(server.socket, server.backlog) < 0) {
    perror("Socket listen failed");
    exit(EXIT_FAILURE);
  }
  return server;
}

int set_non_blocking(int sockfd) {
  int flags = fcntl(sockfd, F_GETFL, 0);
  if (fcntl(sockfd, F_SETFL, (flags < 0 ? 0 : flags) | O_NONBLOCK) == -1) {
    perror("Failed to set nonblocking");
    exit(EXIT_FAILURE);
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

void add_headers(arena *store, arena scratch, Response res) {
  // TODO should be conditional on client's invitation
  s8mapassoc(store, res.headers, s8("Connection"), s8("keep-alive"));
  char content_length[10] = {0};
  if (snprintf(content_length, sizeof(content_length),
               "%ti", res.body.len) > 0) {
    s8_ v = s8clone(store, s8wrap(content_length, sizeof(content_length)));
    if (v.ok) s8mapassoc(store, &res.headers, s8("Content-Length"), v.v);
  }
}

// Non-streaming for the moment
s8_ serialise_response(arena *store, arena scratch, Response res) {
  // Use scratch as buffer.
  arena_usage scratch_usage = usage(&scratch);
  assert(!scratch_usage.used);
  s8buildcstr(&scratch, "HTTP/1.1 ");
  s8 status = {0}; 
  switch (res.status) {
  case OK:
    status = s8("200 OK"); // TODO check spec, etc
    break;
  }
  s8buildsep(&scratch, "\r\n", &status);
  add_headers(store, (arena){0}, res); // don't use scratch during s8build
  s8map *header = res.headers;
  do {
    printf("%s: %s\n", s8unwrap(store, header->key), s8unwrap(store, header->val));
    s8buildsep(&scratch, ": ", &header->key);
    s8buildsep(&scratch, "\r\n", &header->val);
  } while ((header = header->next));
  s8buildcstr(&scratch, "\r\n");
  s8build(&scratch, &res.body);
  // TODO handle cookies separately
  // FIXME s8_ is too annoying within s8build...? could wrap with macro returning not-ok?
  return (s8_){ .v = s8arena(&scratch) };
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
  if (!client->deliverable.ok) return; // TODO other handling? retry something?
  // TODO how to indicate zero length reply? Meaningless?
  s8 chunk = s8slice(client->deliverable.v, 0, BUFOUTSIZE);
  if (chunk.len > 0) {
    ssize_t bytes_written = write(w->fd, chunk.buf, chunk.len);
    if (bytes_written == 0) { // TODO CHECK SEMANTICS client closed connection?
      printf("write client closed cleanup\n");
      cleanup_client(EV_A_ w);
    } else if (bytes_written < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // does this just try again?
      } else {
        perror("Error writing to client");
        cleanup_client(EV_A_ w);
      }
    } else {
      client->deliverable.v = s8slice(client->deliverable.v, bytes_written, 0);
      // should become multiple writes if deliverable.v.len > BUFOUTSIZE
    }
  } else {
    //printf("Unsetting writable\n");
    client_set_writable(EV_A_ w, 0); // unset writable when write actually finished
    //cleanup_client(EV_A_ w); // seems to let browser "finish loading"; https://stackoverflow.com/questions/20763999/explain-http-keep-alive-mechanism
  }
}

void read_client(EV_P_ ev_io *w, int events) {
  Client *client = (Client *)w->data;
  arena_usage scratch_usage = usage(&client->scratch);
  assert(!scratch_usage.used);
  // using scratch arena as a buffer here, instead of local array
  ssize_t bytes_read = read(w->fd, client->scratch.beg, scratch_usage.remaining);
  client->scratch.cur = client->scratch.beg + bytes_read;
  if (bytes_read == 0) { // client closed connection
    // printf("read client closed cleanup\n");
    cleanup_client(EV_A_ w);
  } else if (bytes_read < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // nothing to read yet
    } else {
      // something bad, do strerror(errno)
      perror("Error reading client");
      cleanup_client(EV_A_ w);
    }
  } else {
    // TODO handle large read, e.g. stream to arena until finished or excessive,
    // then handle?
    // For now, store (copy) request in client store arena.
    s8_ raw = s8clone(&client->store, s8arena(&client->scratch));
    if (!raw.ok) {
      perror("Failed to store raw request");
      cleanup_client(EV_A_ w);
    }
    client->scratch.cur = client->scratch.beg; // Reset!
    log_debug(raw.v);
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
    // ⚠ SINGLE THREADED and synchronous for the moment; TODO offload to worker
    // pthread pool CAREFULLY
    Response res = client->server->handler(&client->store, client->scratch, req);
    client->deliverable = serialise_response(&client->store, client->scratch, res);
    if (client->deliverable.ok) client_set_writable(EV_A_ w, 1); // unset in write_client when actually finished
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
    arena client_store = alloc_arena(server->client_arena_cap);
    arena client_scratch = alloc_arena(server->client_arena_cap);
    // printf("store beg %p\nscratch beg %p\n", (void *)client_store.beg, (void *)client_scratch.beg); fflush(0);
    // https://metacpan.org/dist/EV/view/libev/ev.pod#ASSOCIATING-CUSTOM-DATA-WITH-A-WATCHER
    Client *client = new (&client_store, Client, 1);
    if (!client) {
      perror("Failed to allocate client");
      exit(EXIT_FAILURE); // TODO could give SERVICE_UNAVAILABLE...
    }
    client->server = server;
    client->store = client_store; // for passing by reference
    client->scratch = client_scratch; // for passing by value
    ev_io_init(&client->read_io, read_client, new_socket, EV_READ);
    client->read_io.data = client; // I'm a woozie (see libev doc) 
    ev_io_start(EV_A_ & client->read_io);
    // This is started and stopped conditionally on whether there is data to
    // write, to prevent excessive activation...
    // https://buildmage.com/blog/libev-tutorial-and-wrapper
    ev_io_init(&client->write_io, write_client, new_socket, EV_WRITE);
    client->write_io.data = client;
    // ...so deliberately not starting here.
  }
}

void sigint_cb(EV_P_ ev_signal *w, int events) {
  printf("SIGINT\n"); fflush(0);
  ev_break (EV_A_ EVBREAK_ALL);
}

void launch(Server *server) {
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
}

#endif // jdfhttp_h
