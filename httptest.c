#include "jdfhttp.h"

Response handler(Request req) {
      // TODO check and deal with req.error
      //    char *response = "HTTP/1.1 200 OK\r\n"
       //                     "Content-Type: text/html; charset=UTF-8\r\n\r\n"
       //                     "<!doctype html>\r\n"
       //                     "<html>\r\n"
       //                     "<head>\r\n"
       //                     "<title>Hello from C</title>\r\n"
       //                     "</head>\r\n"
       //                     "<body>wtf man</body>\r\n"
       //                     "</html>\r\n";
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
