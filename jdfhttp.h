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

#define BUFFER_SIZE 16384

typedef struct {
  int domain;
  int port;
  int service;
  int protocol;
  int backlog;
  int socket;
  struct sockaddr_in address;
  struct ev_loop *loop;
  // handler function pointer?
} Server;

typedef struct {
  int status; // http status
  s8s headers;
  s8 body; // TODO streaming lol
} Response;

Server make_server(int domain, int port, int service, int protocol,
                   int backlog, u_long interface) {
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

void read_client(EV_P_ ev_io *w, int events) {
  Server *server = (Server *)w->data;
  u8 buffer[BUFFER_SIZE] = {0}; // does this actually zero the whole array? lsp hint is {[0]=0}
  ssize_t bytes_read = read(w->fd, buffer, BUFFER_SIZE - 1); // TODO handle large read (>= BUFFER_SIZE)
  if (bytes_read == 0) { // client closed connection
    ev_io_stop(EV_A_ w);
    close(w->fd);
    free(w);
  } else if (bytes_read < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // nothing to read yet
    } else {
      // something bad, do strerror(errno)
      perror("Error reading client");
      ev_io_stop(EV_A_ w);
      close(w->fd);
      free(w);
    }
  } else {
    /* TODO concept:
       - validate +- encode request
       - add request to queue, tracking source ?socket (mitigate against recycling!)
       - worker thread/s consume request and add response to another queue (queues need mutexes, or maybe Wellons' fancy lockfree queue)
       - server loop sends response to correct socket

       Number of worker threads could be sched_getaffinity() -1 on linux, or sysctlbyname("machdep.cpu.core_count") -1 on macOS.

     */ 
    // No parsing... try llhttp (which depends on llvm...)
    oswrite(1, (u8 *)&buffer, bytes_read);
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
  if (new_socket < 0) {
    perror("Socket connection failed");
  } else {
    set_non_blocking(new_socket);
    ev_io *client_watcher = (ev_io *)calloc(1, sizeof(ev_io)); // TODO arenafy?
    if (!client_watcher) {
      perror("Failed to allocate watcher");
      exit(EXIT_FAILURE);
    }
    ev_io_init(client_watcher, read_client, new_socket, EV_READ);
    ev_io_start(EV_A_ client_watcher);
  }
}

void launch(Server *server) {
  server->loop = EV_DEFAULT;
  ev_io accept_watcher;
  set_non_blocking(server->socket);
  ev_io_init(&accept_watcher, accept_client, server->socket, EV_READ);
  accept_watcher.data = server; // allows access within callbacks
  ev_io_start(server->loop, &accept_watcher);
  ev_run(server->loop, 0);
}

#endif // jdfhttp_h
