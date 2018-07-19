#include "compton.h"

/**
 * Turn on the program reset flag.
 *
 * This will result in compton resetting itself after next paint.
 */
static void
reset_enable(int __attribute__((unused)) signum) {
  session_t * const ps = ps_g;

  ps->reset = true;
}

/**
 * The function that everybody knows.
 */
int
main(int argc, char **argv) {
  // Set locale so window names with special characters are interpreted
  // correctly
  setlocale(LC_ALL, "");

  // Set up SIGUSR1 signal handler to reset program
  {
    sigset_t block_mask;
    sigemptyset(&block_mask);
    const struct sigaction action= {
      .sa_handler = reset_enable,
      .sa_mask = block_mask,
      .sa_flags = 0
    };
    sigaction(SIGUSR1, &action, NULL);
  }

  // Main loop
  session_t *ps_old = ps_g;
  while (1) {
    ps_g = session_init(ps_old, argc, argv);
    if (!ps_g) {
      printf_errf("(): Failed to create new session.");
      return 1;
    }
    session_run(ps_g);
    ps_old = ps_g;
    session_destroy(ps_g);
  }

  free(ps_g);

  return 0;
}
