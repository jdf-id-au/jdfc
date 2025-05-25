#include "jdfhttp.h"

#define print(x) cur = s8lappend(store, cur, x);

Response handler(arena *store, arena scratch, Request req) {
  // TODO check and deal with req.error (and use it in jdfhttp.h)
  if (!s8equal(req.uri, s8("/"))) return (Response){.status = NOT_FOUND};
  
  s8map *headers = s8mapassocl(store, 0, s8("Content-Type"), s8("text/html; charset=UTF-8"));
  s8_ body = s8sprintf(&scratch,
                       "<!doctype html>"
                       "<html>"
                       "<head>"
                       "<title>Hello from C</title>"
                       "</head>"
                       "<body>Using %ti/%ti B for server, %ti/%ti B for this client"
                       "<h1>Workshops</h1>",
                       used(&req.client->server->store),
                       capacity(&req.client->server->store),
                       used(&req.client->store), capacity(&req.client->store));
  Response res = {0};
  res.body = s8lappendcl(store, res.body, body.v);
  s8l *cur = res.body;
  Workshops *ws = &req.client->server->workshops;
  
  print(s8("<table><thead><th>Used</th><th>Available</th></thead><tbody>"));

  for (size i = 0; i < ws->len; i++) {
    s8_ row =
      s8sprintf(store, "<tr><td>%ti</td><td>%ti</td></tr>",
                used(&ws->buf[i].store), available(&ws->buf[i].store));
    if (row.ok) print(row.v);
  }
  print(s8("</tbody></table></body></html>"));
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
  Server server = make_server(handler, .port = 8080, .client_mem = KiB(6));
  launch(&server);
  return 0;
}
