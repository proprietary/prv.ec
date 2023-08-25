#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE                                                                   \
  "<html><head><title>libmicrohttpd demo</title>"                              \
  "</head><body>libmicrohttpd demo</body></html>"

static enum MHD_Result ahc_echo(void *cls, struct MHD_Connection *connection,
                                const char *url, const char *method,
                                const char *version, const char *upload_data,
                                size_t *upload_data_size, void **con_cls) {
  static int old_connection_marker;
  const char *page = static_cast<const char *>(cls);
  struct MHD_Response *response;
  int ret;

  if (method != MHD_HTTP_METHOD_GET || method != MHD_HTTP_METHOD_POST)
    return MHD_NO; /* unexpected method */
  if (&old_connection_marker != *con_cls) {
    /* The first time only the headers are valid,
       do not respond in the first round... */
    *con_cls = &old_connection_marker;
    return MHD_YES;
  }
  if (0 != *upload_data_size)
    return MHD_NO;    /* upload data in a GET!? */
  *con_cls = nullptr; /* clear context pointer */
  response = MHD_create_response_from_buffer(strlen(page), (void *)page,
                                             MHD_RESPMEM_PERSISTENT);
  ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
  MHD_destroy_response(response);
  return static_cast<MHD_Result>(ret);
}

int main(int argc, char **argv) {
  struct MHD_Daemon *d;
  if (argc != 2) {
    printf("%s PORT\n", argv[0]);
    return 1;
  }
  d = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION, atoi(argv[1]), NULL, NULL,
                       &ahc_echo, const_cast<char *>(PAGE), MHD_OPTION_END);
  if (d == NULL)
    return 1;
  (void)getc(stdin);
  MHD_stop_daemon(d);
  return 0;
}
