// No, don't!
// Starting at
// https://medium.com/@justup1080/tutorial-creating-a-minimalist-http-server-in-c-2303d140c725
// Really you'd use mongoose or similar...

#include "jdf.h"

#ifndef jdfhttp_h
#define jdfhttp_h

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define BUFFER_SIZE 16000

typedef struct Server Server;
struct Server {
  int domain;
  int port;
  int service;
  int protocol;
  int backlog;
  int socket;
  struct sockaddr_in address;
  void (*launch)(Server *server);
};

Server make_server(int domain, int port, int service, int protocol,
                   int backlog, u_long interface, void (*launch)(Server *server)) {
  Server server = {
    .domain = domain,
    .port = port,
    .service = service,
    .protocol = protocol,
    .backlog = backlog,
    .socket = socket(domain, service, protocol),
    .address = {.sin_family = domain,
                .sin_port = htons(port), // convert byte order
                .sin_addr = {.s_addr = htonl(interface)}}
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
  server.launch = launch;
  return server;
}

void launch(Server *server) {
  u8 buffer[BUFFER_SIZE] = {0};
  int addrlen = sizeof(server->address);

  while (1) {
    printf("Await connection\n");
    // No threading or async... try libev
    int new_socket = accept(server->socket,
                            (struct sockaddr *)&server->address,
                            (socklen_t *)&addrlen);
    if (new_socket < 0) {
      perror("Socket connection failed");
      exit(EXIT_FAILURE);
    }
    ssize_t bytes_read = read(new_socket, buffer, BUFFER_SIZE - 1);
    if (bytes_read < 0) {
      perror("Socket read failed");
      exit(EXIT_FAILURE);
    }
    // No parsing... try llhttp (which depends on llvm...)
    oswrite(1, (u8 *)&buffer, bytes_read);
    // No protocol awareness...
    char *response = "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html; charset=UTF-8\r\n\r\n"
                    "<!doctype html>\r\n"
                    "<html>\r\n"
                    "<head>\r\n"
                    "<title>Hello from C</title>\r\n"
                    "</head>\r\n"
                    "<body>wtf man</body>\r\n"
                    "</html>\r\n";
    write(new_socket, response, strlen(response));
    close(new_socket);
  }
}

#endif // jdfhttp_h
