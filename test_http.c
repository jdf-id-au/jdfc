#include "jdfhttp.h"

#define print(x) cur = s8l_append(store, cur, x);

// TODO 2026-06-07 19:59:01 opaque "client resources" pointer for db conn etc

// Response websocket_handler(arena *store, arena *scratch, Request req) {
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

// TODO 2026-06-07 18:32:21 proper testing, incl big POST etc

Response handler(arena *store, arena *scratch, Request req) {
  Response res = (Response){.type = HTML};
  Client *client = Client_abs(req.client);
  s8 body = s8printf(
      store,
      "<!doctype html>"
      "<html>"
      "<head>"
      "<title>Hello from C</title>"
      "</head>"
      "<body>Using %ti/%ti B for server, %ti/%ti B for this client %s:%d"
      "<h1>Workshops</h1>",
      used(&client->server->store), capacity(&client->server->store),
      used(arena_abs(client->store)), capacity(arena_abs(client->store)), client->ip, client->port);
  
  assert(body.len, "failed to construct body");
  res.body = s8l_rel(store, s8l_append(store, s8l_abs(res.body), body));
  s8l *cur = s8l_abs(res.body);
  Workshops *ws = &Client_abs(req.client)->server->workshops;
  
  print(s8("<table><thead><th>Used</th><th>Available</th></thead><tbody>"));

  for (size i = 0; i < ws->len; i++) {
    s8 row = s8printf(store, "<tr><td>%ti</td><td>%ti</td></tr>",
                       used(&Workshops_array_abs(*ws)[i].store),
                       available(&Workshops_array_abs(*ws)[i].store));
    if (row.len) print(row);
  }
  print(s8("</tbody></table></body></html>"));
  if (!body.len) {
    res.body = s8l_rel(store, 0);
    res.status = INTERNAL_SERVER_ERROR; 
  }
  else res.status = OK;
  return res;
}

Response sse_handler(arena *store, arena *scratch, Request req) {
  Response res = (Response){.status = OK, .type = EVENT_STREAM};
  add_header(store, &res, CACHE_CONTROL, s8("no-cache"));
  // NB 2025-10-01 17:56:45 nginx special
  add_header(store, &res, X_ACCEL_BUFFERING, s8("no"));
  return res;
}

Response send_handler(arena *store, arena *scratch, Request req) {
  Client *client = Client_abs(req.client);
  Server *server = client->server;
  for (size i = 0; i < server->client_stores.len; i++) {
    arena *a = &arenas_array_abs(server->client_stores)[i];
    if (!a) continue;
    Client *cur = client_from_arena(a);
    if (!cur) continue;
    if (cur->mode == SERVER_SENT_EVENTS) {
      s8 msg = s8printf(a, // recipient's arena!
                          "event: message\ndata: hello from %s:%d to %s:%d\n\n",
                          client->ip, client->port, cur->ip, cur->port);
      if (!msg.len) return (Response){.status = SERVICE_UNAVAILABLE};
      b32 stat = enqueue_request((Request){
          .client = Client_rel(a, cur),
          .is_update = 1, // destination
          .update = msg,
          .from = req.client});
      //printf("%s %s:%d → %s:%d\n", stat ? "🟢" : "🔴", src_ip, src_port, dst_ip, dst_port);
    }
  }
  return (Response) {.status = OK};
}

s8 view_sse = s8("<!doctype html>\n"
                 "<html><head><title>SSE listener</title></head>\n"
                 "<body><div id=\"output\"></div>"
                 "<script type=\"module\">\n"
                 "const esrc = new EventSource(\"/sse\");\n"
                 "esrc.onmessage = (event) => { console.log(event); "
                 "output.innerHTML += `${event.data}<br/>`; }\n"
                 "output.innerHTML += `Awaiting server-sent events...<br/>`;\n"
                 "console.log(\"Awaiting server-sent events...\");\n"
                 "</script>\n"
                 "</body></html>\n"
    ); 

Response receive_handler(arena *store, arena *scratch, Request req) {
  return (Response) {
    .status = OK, .type = HTML,
    .body = s8l_rel(store, s8l_append(store, 0, view_sse)) 
  };
}

const Route routes[] = {
    {.uri = s8("/"), .handler = handler},
    {.uri = s8("/sse"), .handler = sse_handler},
    {.uri = s8("/send"), .handler = send_handler},
    {.uri = s8("/receive"), .handler = receive_handler}
};

Response router(arena *store, arena *scratch, Request req) {
  // Updates bypass routing but should be in handler for app logic
  if (req.is_update)
    return (Response){.client = req.client, .is_update = 1, .update = req.update};
  // Other requests:
  for (size i = 0; i < countof(routes); i++) {
    Handler h = routes[i].handler;
    if (routes[i].uri.len) {
      if (routes[i].parser) {
        void *params = routes[i].parser(store, scratch, req.uri);
        if (!params) continue; // NB 2026-04-21 13:19:23 parser also needs to handle uri match
        req.params = (struct rel){.arena = store, .ptr = (byte *)params - store->beg + 1}; // FIXME 2026-05-26 22:10:04 fragile
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

i32 main(int argc, char **argv) {
  arena init = alloc_arena(KiB(1), 0);
  
  // TODO 2026-06-07 16:04:41 plumb through other config options; probably keep make_server as macro etc
  struct args args = argparse(&init, "--port=int --workers=int", argc, argv);
  i32_ port = int_arg(args, "port");
  if (!port.ok) failwith(1, "Please specify a port.");
  Server server = make_server(router, .port = port.val);
  launch(&server);
  return 0;
}
