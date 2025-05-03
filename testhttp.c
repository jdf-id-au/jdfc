#include "jdfhttp.h"

Response handler(arena *store, arena scratch, Request req) {
  // TODO check and deal with req.error
  s8map *headers = s8mapassoc(store, 0, s8("Content-Type"),
                              s8("text/html; charset=UTF-8"));
  s8 body = s8("<!doctype html>" 
               "<html>"
               "<head>"
               "<title>Hello from C</title>"
               "</head>"
               "<body>wtf man</body>"
               "</html>");
  Response res = {.status = OK,
                  .headers = headers,
                  .body = body};
  return res;
}

int main(void) {
  
  Server server = make_server((Config){ // C99 ftw
      .domain = PF_INET,
      .port = 80, // port 80 surprisingly works in userland on macOS
      .backlog = 10,
      .interface = INADDR_ANY,
      .client_arena_cap = MiB(1),
      .handler = handler});
  launch(&server);
  return 0;
}
