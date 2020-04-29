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
#include <errno.h>

#include <sys/types.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>

#include "bar/bar.h"
#include "config.h"
#include "yml.h"

#define LOG_MODULE "main"
#include "log.h"
#include "version.h"

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

    int len = snprintf(NULL, 0, "%s/.config/yambar/config.yml", home_dir);
    char *path = malloc(len + 1);
    snprintf(path, len + 1, "%s/.config/yambar/config.yml", home_dir);
    return path;
}

static char *
get_config_path_xdg(void)
{
    const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
    if (xdg_config_home == NULL)
        return NULL;

    int len = snprintf(NULL, 0, "%s/yambar/config.yml", xdg_config_home);
    char *path = malloc(len + 1);
    snprintf(path, len + 1, "%s/yambar/config.yml", xdg_config_home);
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
load_bar(const char *config_path, enum bar_backend backend)
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

    bar = conf_to_bar(bar_conf, backend);
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
    printf("  -b,--backend={xcb,wayland,auto}       backend to use (default: auto)\n"
           "  -c,--config=FILE                      alternative configuration file\n"
           "  -C,--validate                         verify configuration then quit\n"
           "  -p,--print-pid=FILE|FD                print PID to file or FD\n"
           "  -l,--log-colorize=[never|always|auto] enable/disable colorization of log output on stderr\n"
           "  -s,--log-no-syslog                    disable syslog logging\n"
           "  -v,--version                          show the version number and quit\n");
}

static bool
print_pid(const char *pid_file, bool *unlink_at_exit)
{
    LOG_DBG("printing PID to %s", pid_file);

    errno = 0;
    char *end;
    int pid_fd = strtoul(pid_file, &end, 10);

    if (errno != 0 || *end != '\0') {
        if ((pid_fd = open(pid_file,
                           O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                           S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0) {
            LOG_ERRNO("%s: failed to open", pid_file);
            return false;
        } else
            *unlink_at_exit = true;
    }

    if (pid_fd >= 0) {
        char pid[32];
        snprintf(pid, sizeof(pid), "%u\n", getpid());

        ssize_t bytes = write(pid_fd, pid, strlen(pid));
        close(pid_fd);

        if (bytes < 0) {
            LOG_ERRNO("failed to write PID to FD=%u", pid_fd);
            return false;
        }

        LOG_DBG("wrote %zd bytes to FD=%d", bytes, pid_fd);
        return true;
    } else
        return false;
}

int
main(int argc, char *const *argv)
{
    static const struct option longopts[] = {
        {"backend",          required_argument, 0, 'b'},
        {"config",           required_argument, 0, 'c'},
        {"validate",         no_argument,       0, 'C'},
        {"print-pid",        required_argument, 0, 'p'},
        {"log-colorize",     optional_argument, 0, 'l'},
        {"log-no-syslog",    no_argument,       0, 's'},
        {"version",          no_argument,       0, 'v'},
        {"help",             no_argument,       0, 'h'},
        {NULL,               no_argument,       0, 0},
    };

    bool unlink_pid_file = false;
    const char *pid_file = NULL;

    bool verify_config = false;
    char *config_path = NULL;
    enum bar_backend backend = BAR_BACKEND_AUTO;

    enum log_colorize log_colorize = LOG_COLORIZE_AUTO;
    bool log_syslog = true;

    while (true) {
        int c = getopt_long(argc, argv, ":b:c:Cp:l::svh", longopts, NULL);
        if (c == -1)
            break;

        switch (c) {
        case 'b':
            if (strcmp(optarg, "xcb") == 0)
                backend = BAR_BACKEND_XCB;
            else if (strcmp(optarg, "wayland") == 0)
                backend = BAR_BACKEND_WAYLAND;
            else {
                fprintf(stderr, "%s: invalid backend\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'c': {
            struct stat st;
            if (stat(optarg, &st) == -1) {
                fprintf(stderr, "%s: invalid configuration file: %s\n", optarg, strerror(errno));
                return EXIT_FAILURE;
            } else if (!S_ISREG(st.st_mode)) {
                fprintf(stderr, "%s: invalid configuration file: not a regular file\n",
                        optarg);
                return EXIT_FAILURE;
            }

            config_path = strdup(optarg);
            break;
        }

        case 'C':
            verify_config = true;
            break;

        case 'p':
            pid_file = optarg;
            break;

        case 'l':
            if (optarg == NULL || strcmp(optarg, "auto") == 0)
                log_colorize = LOG_COLORIZE_AUTO;
            else if (strcmp(optarg, "never") == 0)
                log_colorize = LOG_COLORIZE_NEVER;
            else if (strcmp(optarg, "always") == 0)
                log_colorize = LOG_COLORIZE_ALWAYS;
            else {
                fprintf(stderr, "%s: argument must be one of 'never', 'always' or 'auto'\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 's':
            log_syslog = false;
            break;

        case 'v':
            printf("yambar version %s\n", YAMBAR_VERSION);
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

    log_init(log_colorize, log_syslog, LOG_FACILITY_DAEMON, LOG_CLASS_WARNING);

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
        log_deinit();
        return 1;
    }

    if (config_path == NULL) {
        config_path = get_config_path();
        if (config_path == NULL) {
            LOG_ERR("could not find a configuration (see man 5 yambar)");
            log_deinit();
            return 1;
        }
    }

    struct bar *bar = load_bar(config_path, backend);
    free(config_path);

    if (bar == NULL) {
        close(abort_fd);
        log_deinit();
        return 1;
    }

    if (verify_config) {
        bar->destroy(bar);
        close(abort_fd);
        log_deinit();
        return 0;
    }

    setlocale(LC_ALL, "");

    bar->abort_fd = abort_fd;

    thrd_t bar_thread;
    thrd_create(&bar_thread, (int (*)(void *))bar->run, bar);

    /* Now unblock. We should be only thread receiving SIGINT */
    pthread_sigmask(SIG_UNBLOCK, &signal_mask, NULL);

    if (pid_file != NULL) {
        if (!print_pid(pid_file, &unlink_pid_file))
            goto done;
    }

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

done:
    /* Signal abort to other threads */
    if (write(abort_fd, &(uint64_t){1}, sizeof(uint64_t)) != sizeof(uint64_t))
        LOG_ERRNO("failed to signal abort to threads");

    int res;
    int r = thrd_join(bar_thread, &res);
    if (r != 0)
        LOG_ERRNO_P("failed to join bar thread", r);

    bar->destroy(bar);
    close(abort_fd);

    if (unlink_pid_file)
        unlink(pid_file);
    log_deinit();
    return res;
}
