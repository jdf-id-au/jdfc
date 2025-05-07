#include "jdfhttp.h"

Response handler(arena *store, arena scratch, Request req) {
  // TODO check and deal with req.error
  s8map *headers = s8mapassocl(store, 0, s8("Content-Type"), s8("text/html; charset=UTF-8"));
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
  Server server = make_server(handler, .port = 8080);
  launch(&server);
  return 0;
}
