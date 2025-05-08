#include "jdfhttp.h"

Response handler(arena *store, arena scratch, Request req) {
  // TODO check and deal with req.error (and use it in jdfhttp.h)
  s8map *headers = s8mapassocl(store, 0, s8("Content-Type"), s8("text/html; charset=UTF-8"));
  s8_ body = s8sprintf(&scratch,
                       "<!doctype html>"
                       "<html>"
                       "<head>"
                       "<title>Hello from C</title>"
                       "</head>"
                       "<body>Using %ti/%ti B for server, %ti/%ti B for client"
                       "<h1>Workshops</h1>",
                       used(&req.client->server->store),
                       capacity(&req.client->server->store),
                       used(&req.client->store), capacity(&req.client->store));
  Response res = {0};
  res.body = s8lappendcl(store, res.body, body.v); // TODO check .ok first...
  s8l *cur = res.body;
  Workshops *ws = &req.client->server->workshops;
  // TODO come up with pleasant-enough API for request body string composition
  
  cur = s8lappend(
      store, cur,
      s8("<table><thead><th>Used</th><th>Available</th></thead><tbody>")) ;

  printf("ws->len %ti\n", ws->len); // FIXME why zero?
  for (size i = 0; i < ws->len; i++) {
    s8_ row =
      s8sprintf(store, "<tr><td>%ti</td><td>%ti</td></tr>",
                used(&ws->buf[i].store), available(&ws->buf[i].store));
    if (row.ok)
      cur = s8lappend(store, cur, row.v);
  }
  cur = s8lappend(store, cur, s8("</tbody></table></body></html>"));
  if (!body.ok) {
    res.body = 0;
    res.status = INTERNAL_SERVER_ERROR; 
  }
  else {
    res.status = OK;
    res.headers = headers;
  }
  return res;
}

int main(void) {
  Server server = make_server(handler, .port = 8080);
  launch(&server);
  return 0;
}
