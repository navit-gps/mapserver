#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <getopt.h>

#include <ydaemon/ydaemon.h>

#define VERSION  "0.0.1"
#define INFO     "ntsprund - daemon to run services"

int main(int argc, char **argv)
{
  yd_context_t *ctx = malloc(sizeof(yd_context_t));
  char *cnf = NULL;
  int r = 0, opt, daemonize = 0;
  int exit = 0, ver_only = 0;
  FILE *ostr;

  if(!ctx) {
    r = ENOMEM;
    goto __fini;
  }

  while((opt = getopt(argc, argv, "dvh")) != -1) {
    switch(opt) {
    case 'd':
      daemonize = 1;
      break;
    case 'v':
      r = 0;
      ver_only = 1;
    case 'h':
      exit = 1;
      break;
    default:
      r = EINVAL;
      exit = 1;
      fprintf(stderr, "Unknown option given '%c'. Exiting.\n", opt);
      break;
    }
  }

  if((optind >= argc) && !exit) {
    exit = 1;
    r = EINVAL;
    fprintf(stderr, "No configuration given. Exiting.\n");
  } else cnf = argv[optind];

  if(exit) {
    if(!r) fprintf(stdout, "%s version %s.\n(c) Navit project 2016\n",
                   INFO, VERSION);
    else fprintf(stderr, "%s version %s.\n(c) Navit project 2016\n",
                   INFO, VERSION);
    if(!ver_only) {
      if(!r) ostr = stdout;
      else ostr = stderr;
      fprintf(ostr, "Usage: %s [-d] [-v] [-h] <pathname of service configuration>\n", argv[0]);
      fprintf(ostr, "\t Where:\n\t -d: Daemonize\n\t -v: Print version and exit\n");
      fprintf(ostr, "\t -h: Print help and exit.\n");
    }

    /* free context and return from main */
    free(ctx);
    return r;
  }

  r = yd_init_ctx(ctx);
  if(r) goto __fini;

  if(daemonize) yddaemon(ctx);

  r = yd_eval_ctx(ctx, cnf);
  if(r) goto __fini;

 __fini:
  if(r) {
    errno = r;
    fprintf(stderr, " -- (%d) ", r);
    perror("Illegal exit: ");
  } else yd_mainloop(ctx);

  return r;
}
