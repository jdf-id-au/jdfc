// No, don't! Use mongoose instead!
// Starting at
// https://medium.com/@justup1080/tutorial-creating-a-minimalist-http-server-in-c-2303d140c725
// https://hoad.io/libev-is-neat/ 

#include "jdf.h"
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
  // handler function pointer?
} Server;

typedef struct {
  Server *server;
  arena store;
  arena scratch;
} Client;

ASSOCIATION_LIST(s8map, s8, s8, s8equal)

enum request_error {
  REQUEST_OK, REQUEST_EMPTY, INVALID_METHOD_LINE
};
  
typedef struct {
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
  s8map headers; // does not accommodate repeat keys, which are permitted by http spec https://stackoverflow.com/a/4371395/780743
  s8map cookies; 
  s8 body; // TODO streaming lol
} Response;

Server make_server(int domain, int port, int service, int protocol,
                   int backlog, u_long interface, size client_arena_cap) {
  arena store = alloc_arena(MiB(1));
  arena scratch = alloc_arena(MiB(1));
  if (!store.beg || !scratch.beg) failwith(1, s8("Failed to allocate server arenas."));
  Server server = {
    .domain = domain,
    .port = port,
    .service = service,
    .protocol = protocol,
    .backlog = backlog, // TODO learn semantics
    .socket = socket(domain, service, protocol),
    .address = {.sin_family = domain,
                .sin_port = htons(port), // convert byte order
                .sin_addr = {.s_addr = htonl(interface)}},
    .store = store,
    .scratch = scratch,
    .client_arena_cap = client_arena_cap
  };
  if (server.socket < 0) {
    perror("Socket creation failed");
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

enum http_method parse_method(s8 s) {
  if (s8equal(s, s8("GET"))) return GET;
  if (s8equal(s, s8("HEAD"))) return HEAD;
  if (s8equal(s, s8("POST"))) return POST;
  if (s8equal(s, s8("PUT"))) return PUT;
  if (s8equal(s, s8("DELETE"))) return DELETE;
  if (s8equal(s, s8("CONNECT"))) return CONNECT;
  if (s8equal(s, s8("OPTIONS"))) return OPTIONS;
  if (s8equal(s, s8("TRACE"))) return TRACE;
  if (s8equal(s, s8("PATCH"))) return PATCH;
  return INVALID_METHOD;
}

// would be better to use llhttp (which depends on llvm...)
Request parse_request(arena *store, arena scratch, s8 raw) {
  Request req = {0};
  s8s split = s8splitu8(store, scratch, raw, '\n', 100);
  if (split.len < 1) { req.error = REQUEST_EMPTY; return req; }
  s8s line0 = s8splitu8(store, scratch, split.buf[0], ' ', 2);
  if (line0.len < 3) { req.error = INVALID_METHOD_LINE; return req; }
  req.method = parse_method(line0.buf[0]);
  req.uri = line0.buf[1]; // copy s8, zerocopy its buffer
  req.protocol = line0.buf[2];
  s8map *headers = {0};
  for (size i = 1; i < split.len; i++) {
    if (s8blank(split.buf[i])) break; // TODO trailing headers...??
    s8s header = s8split(store, scratch, split.buf[i], s8(": "), 1);
    log_debug(split.buf[i]);
    if (header.len == 2)
      headers = s8mapassoc(store, headers, header.buf[0], header.buf[1]);
    req.headers = headers;
  }
  s8map *cookies = {0};
  // e.g. Cookie: name=value; name2=value2; name3=value3
  s8map *cookiekv = s8mapget(headers, s8("Cookie"));
  if (cookiekv) {
    s8s cookiekvs = s8split(store, scratch, cookiekv->val, s8("; "), 32);
    for (size i = 0; i < cookiekvs.len; i++) {
      s8s cookie = s8splitu8(store, scratch, cookiekvs.buf[i], '=', 1);
      if (cookie.len == 2)
        cookies = s8mapassoc(store, cookies, cookie.buf[0], cookie.buf[1]);
    }
    req.cookies = cookies;
  }
  return req;
}

// io must be first
// https://metacpan.org/dist/EV/view/libev/ev.pod#ASSOCIATING-CUSTOM-DATA-WITH-A-WATCHER
typedef struct {
  ev_io io;
  Client client;
} client_io;

void read_client(EV_P_ ev_io *w, int events) {
  Client client = ((client_io *)w)->client;
  arena_usage scratch_usage = usage(&client.scratch);
  assert(!scratch_usage.used);
  // using scratch arena as a buffer here, instead of local array
  ssize_t bytes_read = read(w->fd, client.scratch.beg, scratch_usage.remaining);
  client.scratch.cur = client.scratch.beg + bytes_read;
  if (bytes_read == 0) { // client closed connection
    ev_io_stop(EV_A_ w);
    close(w->fd);
    free_arena(&client.store);
  } else if (bytes_read < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // nothing to read yet
    } else {
      // something bad, do strerror(errno)
      perror("Error reading client");
      ev_io_stop(EV_A_ w);
      close(w->fd);
      free_arena(&client.store);
    }
  } else {
    // TODO handle large read, e.g. stream to arena until finished or excessive,
    // then handle?
    // For now, store (copy) request in client store arena.
    s8 raw = s8clone(&client.store, s8arena(&client.scratch));
    log_debug(raw);
    Request req = parse_request(&client.store, client.scratch, raw);
    /* TODO concept:
       - validate +- encode request
       - add request to queue, tracking source ?socket (mitigate against
recycling!)
       - worker thread/s consume request and add response to another queue
(queues need mutexes, or maybe Wellons' fancy lockfree queue)
       - server loop sends response to correct socket

       Will this need a write watcher too?
       
       Number of worker threads could be sched_getaffinity() -1 on linux, or sysctlbyname("machdep.cpu.core_count") -1 on macOS.
     */ 
    char *response = "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html; charset=UTF-8\r\n\r\n"
                    "<!doctype html>\r\n"
                    "<html>\r\n"
                    "<head>\r\n"
                    "<title>Hello from C</title>\r\n"
                    "</head>\r\n"
                    "<body>wtf man</body>\r\n"
                    "</html>\r\n";
    write(w->fd, response, strlen(response));
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
    client_io *client_watcher = new (&client_store, client_io, 1);
    client_watcher->client = (Client) {
      .server = server,
      .store = client_store, // for passing by reference
      .scratch = client_scratch // for passing by value
    };
    if (!client_watcher) {
      perror("Failed to allocate watcher");
      exit(EXIT_FAILURE);
    }
    ev_io_init(&client_watcher->io, read_client, new_socket, EV_READ);
    ev_io_start(EV_A_ &client_watcher->io);
  }
}

void launch(Server *server) {
  server->loop = EV_DEFAULT; // TOOD more explicitly one per server?
  ev_io accept_watcher;
  set_non_blocking(server->socket);
  ev_io_init(&accept_watcher, accept_client, server->socket, EV_READ);
  accept_watcher.data = server; // allows access within callbacks
  ev_io_start(server->loop, &accept_watcher);
  ev_run(server->loop, 0);
  // FIXME sometimes have to wait before relaunching because Address already in
  // use. Need signal handler to kill properly?
}

#endif // jdfhttp_h
