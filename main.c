#include <assert.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <getopt.h>

#include <sys/types.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <pwd.h>

#include "bar/bar.h"
#include "config.h"
#include "yml.h"

#define LOG_MODULE "main"
#include "log.h"

static volatile sig_atomic_t aborted = 0;

static void
signal_handler(int signo)
{
    aborted = signo;
}

static char *
get_config_path_user_config(void)
{
    struct passwd *passwd = getpwuid(getuid());
    if (passwd == NULL) {
        LOG_ERRNO("failed to lookup user");
        return NULL;
    }

    const char *home_dir = passwd->pw_dir;
    LOG_DBG("user's home directory: %s", home_dir);

    int len = snprintf(NULL, 0, "%s/.config/f00bar/config.yml", home_dir);
    char *path = malloc(len + 1);
    snprintf(path, len + 1, "%s/.config/f00bar/config.yml", home_dir);
    return path;
}

static char *
get_config_path_xdg(void)
{
    const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
    if (xdg_config_home == NULL)
        return NULL;

    int len = snprintf(NULL, 0, "%s/f00bar/config.yml", xdg_config_home);
    char *path = malloc(len + 1);
    snprintf(path, len + 1, "%s/f00bar/config.yml", xdg_config_home);
    return path;
}

static char *
get_config_path(void)
{
    struct stat st;

    char *config = get_config_path_xdg();
    if (config != NULL && stat(config, &st) == 0 && S_ISREG(st.st_mode))
        return config;
    free(config);

    /* 'Default' XDG_CONFIG_HOME */
    config = get_config_path_user_config();
    if (config != NULL && stat(config, &st) == 0 && S_ISREG(st.st_mode))
        return config;
    free(config);

    return NULL;
}

static struct bar *
load_bar(const char *config_path)
{
    FILE *conf_file = fopen(config_path, "r");
    if (conf_file == NULL) {
        LOG_ERRNO("%s: failed to open", config_path);
        return NULL;
    }

    struct bar *bar = NULL;
    char *yml_error = NULL;

    struct yml_node *conf = yml_load(conf_file, &yml_error);
    if (conf == NULL) {
        LOG_ERR("%s:%s", config_path, yml_error);
        goto out;
    }

    const struct yml_node *bar_conf = yml_get_value(conf, "bar");
    if (bar_conf == NULL) {
        LOG_ERR("%s: missing required top level key 'bar'", config_path);
        goto out;
    }

    bar = conf_to_bar(bar_conf);
    if (bar == NULL) {
        LOG_ERR("%s: failed to load configuration", config_path);
        goto out;
    }

out:
    free(yml_error);
    yml_destroy(conf);
    fclose(conf_file);
    return bar;
}

static void
print_usage(const char *prog_name)
{
    printf("Usage: %s [OPTION]...\n", prog_name);
    printf("\n");
    printf("Options:\n");
    printf("  -c,--check-config          verify configuration then quit\n"
           "  -v,--version               print f00sel version and quit\n");
}

int
main(int argc, char *const *argv)
{
    setlocale(LC_ALL, "");

    static const struct option longopts[] = {
        {"check-config",     no_argument,       0, 'c'},
        {"version",          no_argument,       0, 'v'},
        {"help",             no_argument,       0, 'h'},
        {NULL,               no_argument,       0, 0},
    };

    bool verify_config = false;

    while (true) {
        int c = getopt_long(argc, argv, ":cvh", longopts, NULL);
        if (c == -1)
            break;

        switch (c) {
        case 'c':
            verify_config = true;
            break;

        case 'v':
            printf("f00bar version %s\n", F00BAR_VERSION);
            return EXIT_SUCCESS;

        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS;

        case ':':
            fprintf(stderr, "error: -%c: missing required argument\n", optopt);
            return EXIT_FAILURE;

        case '?':
            fprintf(stderr, "error: -%c: invalid option\n", optopt);
            return EXIT_FAILURE;
        }
    }

    const struct sigaction sa = {.sa_handler = &signal_handler};
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Block SIGINT (this is under the assumption that threads inherit
     * the signal mask */
    sigset_t signal_mask;
    sigemptyset(&signal_mask);
    sigaddset(&signal_mask, SIGINT);
    sigaddset(&signal_mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &signal_mask, NULL);

    int abort_fd = eventfd(0, EFD_CLOEXEC);
    if (abort_fd == -1) {
        LOG_ERRNO("failed to create eventfd (for abort signalling)");
        return 1;
    }

    char *config_path = get_config_path();
    if (config_path == NULL) {
        LOG_ERR("could not find a configuration (see man 5 f00bar)");
        return 1;
    }

    struct bar *bar = load_bar(config_path);
    free(config_path);

    if (bar == NULL) {
        close(abort_fd);
        return 1;
    }

    if (verify_config) {
        bar->destroy(bar);
        close(abort_fd);
        return 0;
    }

    bar->abort_fd = abort_fd;

    thrd_t bar_thread;
    thrd_create(&bar_thread, (int (*)(void *))bar->run, bar);

    /* Now unblock. We should be only thread receiving SIGINT */
    pthread_sigmask(SIG_UNBLOCK, &signal_mask, NULL);

    while (!aborted) {
        struct pollfd fds[] = {{.fd = abort_fd, .events = POLLIN}};
        int r __attribute__((unused)) = poll(fds, 1, -1);

        /*
         * Either the bar aborted (triggering the abort_fd), or user
         * killed us (triggering the signal handler which sets
         * 'aborted')
         */
        assert(aborted || r == 1);
        break;
    }

    if (aborted)
        LOG_INFO("aborted: %s (%d)", strsignal(aborted), aborted);

    /* Signal abort to other threads */
    if (write(abort_fd, &(uint64_t){1}, sizeof(uint64_t)) != sizeof(uint64_t))
        LOG_ERRNO("failed to signal abort to threads");

    int res;
    int r = thrd_join(bar_thread, &res);
    if (r != 0)
        LOG_ERRNO_P("failed to join bar thread", r);

    bar->destroy(bar);
    close(abort_fd);
    return res;
}
