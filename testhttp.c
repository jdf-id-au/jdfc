#include "jdfhttp.h"

Response handler(arena *store, arena scratch, Request req) {
  // TODO check and deal with req.error
  s8map *headers = s8mapassoc(store, 0, s8("Content-Type"),
                              s8("text/html; charset=UTF-8"));
  s8mapassoc(store, headers, s8("Connection"), s8("keep-alive"));
  s8 body = s8(             //"HTTP/1.1 200 OK\r\n"
                            //"Content-Type: text/html; charset=UTF-8\r\n\r\n"
      "<!doctype html>\r\n" // crlf not required for body
      "<html>\r\n"
      "<head>\r\n"
      "<title>Hello from C</title>\r\n"
      "</head>\r\n"
      "<body>wtf man</body>\r\n"
      "</html>\r\n");
  char *content_length = new (&scratch, char, 1024);
  if (snprintf(content_length, 1024, "%ti", body.len) > 0) {
    s8_ cl = s8clone(store, s8wrap(content_length, 5));
    if (cl.ok) s8mapassoc(store, headers, s8("Content-Length"), cl.v);
  }
  Response res = {.status = OK,
                  .headers = *headers,
                  .body = body};
  return res;
}

int main(void) {
  // FIXME shouldn't succeed trying to serve port 80 as non-root!
  Server server = make_server((Config){ // C99 ftw
      .domain = PF_INET,
      .port = 8000,
      .backlog = 10,
      .interface = INADDR_ANY,
      .client_arena_cap = MiB(1),
      .handler = handler});
  launch(&server);
  return 0;
}
