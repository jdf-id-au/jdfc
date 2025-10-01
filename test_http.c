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
  Response res = (Response){.status = OK, .type = EVENT_STREAM};
  add_header(store, &res, CACHE_CONTROL, s8("no-cache"));
  // NB 2025-10-01 17:56:45 nginx special
  add_header(store, &res, X_ACCEL_BUFFERING, s8("no"));
  return res;
}

Response send_handler(arena *store, arena scratch, Request req) {
  Server *server = req.client->server; 
  // FIXME 2025-10-01 22:43:07 not sending to everyone
  for (size i = 0; i < server->clients.len; i++) {
    Client *c = server->clients.buf[i];
    if (c && c->mode == SERVER_SENT_EVENTS) {
      ipstr(src_, req.client->address);
      ipstr(dst_, c->address);
      s8_ msg = s8sprintf(&scratch,
                          "event: message\ndata: hello from %s:%d to %s:%d\n\n",
                          src_ip, src_port, dst_ip, dst_port);
      if (!msg.ok) return (Response){.status = SERVICE_UNAVAILABLE};
      u8 *buf = malloc(msg.v.len); // freed by worker after consumption in serialise_response
      if (!buf) return (Response){.status = SERVICE_UNAVAILABLE};
      copy(buf, msg.v.buf, msg.v.len);
      b32 stat = enqueue_request((Request){
          .client = c,
          .is_update = 1, // destination
          .update = {buf, msg.v.len},
          .from = req.client});
      printf("%s %s:%d → %s:%d\n", stat ? "🟢" : "🔴", src_ip, src_port, dst_ip, dst_port);
    }
  }
  return (Response) {.status = OK};
}

s8 view_sse = s8("<!doctype html>\n"
                 "<html><head><title>SSE listener</title></head>\n"
                 "<body><div id=\"output\"></div>"
                 "<script type=\"module\">\n"
                 "const esrc = new EventSource(\"//localhost:8080/sse\");\n"
                 "esrc.onmessage = (event) => { console.log(event); "
                 "output.innerHTML += `${event.data}<br/>`; }\n"
                 "output.innerHTML += `Awaiting server-sent events...<br/>`;\n"
                 "console.log(\"Awaiting server-sent events...\");\n"
                 "</script>\n"
                 "</body></html>\n"
    ); 

Response receive_handler(arena *store, arena scratch, Request req) {
  return (Response) {
    .status = OK, .type = HTML,
    .body = s8lappend(store, 0, view_sse) 
  };
}

const Route routes[] = {
    {.uri = s8("/"), .handler = handler},
    {.uri = s8("/sse"), .handler = sse_handler},
    {.uri = s8("/send"), .handler = send_handler},
    {.uri = s8("/receive"), .handler = receive_handler}
};

Response router(arena *store, arena scratch, Request req) {
  // Updates bypass routing but should be in handler for app logic
  if (req.is_update)
    return (Response){.client = req.client, .is_update = 1, .update = req.update};
  // Other requests:
  for (size i = 0; i < countof(routes); i++) {
    Handler h = routes[i].handler;
    if (routes[i].uri.len) {
      if (routes[i].parser) {
        void *params = routes[i].parser(store, scratch, req.uri);
        if (!params) continue;
        req.params = params;
        return h(store, scratch, req);
      } else if (s8equal(req.uri, routes[i].uri))
        return h(store, scratch, req);
    } else { // default route
      if (h) return h(store, scratch, req);
      else {
        fprintf(stderr, "No handler for default route");
        return (Response){.status = NOT_FOUND};
      }
    }
  }
  return (Response){.status = NOT_FOUND};
}

i32 main(void) {
  Server server = make_server(router, .port = 8080);
  launch(&server);
  return 0;
}
