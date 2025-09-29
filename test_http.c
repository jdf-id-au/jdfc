#include "jdfhttp.h"

#define print(x) cur = s8lappend(store, cur, x);

// Response websocket_handler(arena *store, arena scratch, Request req) {
//   Response res = {0};
//   if (!s8equal(req.uri, s8("/ws"))) return (Response){.status = NOT_FOUND};
//   if (req.method != GET) return (Response){.status = METHOD_NOT_ALLOWED};
//   if (s8mapcontains(req.headers, s8("Upgrade"), s8("websocket")) &&
//       s8mapcontains(req.headers, s8("Connection"), s8("Upgrade"))) {
//     // TODO check Sec-WebSocket-Key, Sec-WebSocket-Version, Origin
//     if (s8mapcontains(req.headers, s8("Sec-WebSocket-Version"), s8("13"))) {
//       res.status = SWITCHING_PROTOCOLS;
//       res.headers = s8mapassocl(store, res.headers, s8("Upgrade"),
//       s8("websocket")); res.headers = s8mapassocl(store, res.headers,
//       s8("Connection"), s8("Upgrade")); res.headers =
//           s8mapassocl(store, res.headers, s8("Sec-WebSocket-Accept"), );
//       //
// https://developer.mozilla.org/en-US/docs/Web/API/WebSockets_API/Writing_WebSocket_servers
// server SHOULD verify Origin
// clients MUST choose a new masking key for each frame
//       // TODO openssl sha1; base64
//     } else {
//       res.status = BAD_REQUEST;
//       res.headers = s8mapassocl(store, res.headers, s8("Sec-WebSocket-Version"), s8("13"));
//       return res;
//     }
//   }
// }

Response handler(arena *store, arena scratch, Request req) {
  // TODO check and deal with req.error (and use it in jdfhttp.h)
  if (!s8equal(req.uri, s8("/"))) return (Response){.status = NOT_FOUND};
  Response res = (Response){.type = HTML};
  s8_ body = s8sprintf(
      store,
      "<!doctype html>"
      "<html>"
      "<head>"
      "<title>Hello from C</title>"
      "</head>"
      "<body>Using %ti/%ti B for server, %ti/%ti B for this client %p"
      "<h1>Workshops</h1>",
      used(&req.client->server->store), capacity(&req.client->server->store),
      used(&req.client->store), capacity(&req.client->store), &req.client);
  
  assert(body.ok);
  res.body = s8lappend(store, res.body, body.v);
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
  else res.status = OK;
  return res;
}

Response sse_handler(arena *store, arena scratch, Request req) {
  if (!s8equal(req.uri, s8("/sse"))) return (Response){.status = NOT_FOUND};
  Response res = (Response){.type = EVENT_STREAM};
  // TODO 2025-09-29 15:13:43 how not to block worker?
  return res;
}

i32 main(void) {
  Server server = make_server(handler, .port = 8080);
  launch(&server);
  return 0;
}
