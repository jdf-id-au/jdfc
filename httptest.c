#include "jdfhttp.h"

int main(void) {
  // FIXME shouldn't succeed trying to serve port 80 as non-root!
  Server server = make_server(PF_INET, 8000, SOCK_STREAM,
                              0, 10, INADDR_ANY);
  launch(&server);
  return 0;
}
