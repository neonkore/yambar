#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>

#include <poll.h>
#include <fcntl.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/timerfd.h>

#define LOG_MODULE "script"
#define LOG_ENABLE_DBG 0
#include "../log.h"
#include "../config.h"
#include "../config-verify.h"
#include "../module.h"
#include "../plugin.h"

struct private {
    char *path;
    size_t argc;
    char **argv;

    struct particle *content;

    struct tag_set tags;

    struct {
        char *data;
        size_t sz;
        size_t idx;
    } recv_buf;
};

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;
    m->content->destroy(m->content);

    struct tag **tag_array = m->tags.tags;
    tag_set_destroy(&m->tags);
    free(tag_array);

    for (size_t i = 0; i < m->argc; i++)
        free(m->argv[i]);
    free(m->argv);
    free(m->recv_buf.data);
    free(m->path);
    free(m);
    module_default_destroy(mod);
}

static struct exposable *
content(struct module *mod)
{
    const struct private *m = mod->private;

    mtx_lock(&mod->lock);
    struct exposable *e = m->content->instantiate(m->content, &m->tags);
    mtx_unlock(&mod->lock);

    return e;
}

static struct tag *
process_line(struct module *mod, const char *line, size_t len)
{
    char *name = NULL;
    char *value = NULL;

    const char *_name = line;

    const char *type = memchr(line, '|', len);
    if (type == NULL)
        goto bad_tag;

    size_t name_len = type - _name;
    type++;

    const char *_value = memchr(type, '|', len - name_len - 1);
    if (_value == NULL)
        goto bad_tag;

    size_t type_len = _value - type;
    _value++;

    size_t value_len = line + len - _value;

    LOG_DBG("%.*s: name=\"%.*s\", type=\"%.*s\", value=\"%.*s\"",
            (int)len, line,
            (int)name_len, _name, (int)type_len, type, (int)value_len, _value);

    name = malloc(name_len + 1);
    memcpy(name, _name, name_len);
    name[name_len] = '\0';

    value = malloc(value_len + 1);
    memcpy(value, _value, value_len);
    value[value_len] = '\0';

    struct tag *tag = NULL;

    if (type_len == 6 && memcmp(type, "string", 6) == 0)
        tag = tag_new_string(mod, name, value);

    else if (type_len == 3 && memcmp(type, "int", 3) == 0) {
        errno = 0;
        char *end;
        long v = strtol(value, &end, 0);

        if (errno != 0 || *end != '\0') {
            LOG_ERR("tag value is not an integer: %s", value);
            goto bad_tag;
        }
        tag = tag_new_int(mod, name, v);
    }

    else if (type_len == 4 && memcmp(type, "bool", 4) == 0) {
        bool v;
        if (strcmp(value, "true") == 0)
            v = true;
        else if (strcmp(value, "false") == 0)
            v = false;
        else {
            LOG_ERR("tag value is not a boolean: %s", value);
            goto bad_tag;
        }
        tag = tag_new_bool(mod, name, v);
    }

    else if (type_len == 5 && memcmp(type, "float", 5) == 0) {
        errno = 0;
        char *end;
        double v = strtod(value, &end);

        if (errno != 0 || *end != '\0') {
            LOG_ERR("tag value is not a float: %s", value);
            goto bad_tag;
        }

        tag = tag_new_float(mod, name, v);
    }

    else if ((type_len > 6 && memcmp(type, "range:", 6) == 0) ||
             (type_len > 9 && memcmp(type, "realtime:", 9 == 0)))
    {
        const char *_start = type + 6;
        const char *split = memchr(_start, '-', type_len - 6);

        if (split == NULL || split == _start || (split + 1) - type >= type_len) {
            LOG_ERR(
                "tag range delimiter ('-') not found in type: %.*s",
                (int)type_len, type);
            goto bad_tag;
        }

        const char *_end = split + 1;

        size_t start_len = split - _start;
        size_t end_len = type + type_len - _end;

        long start = 0;
        for (size_t i = 0; i < start_len; i++) {
            if (!(_start[i] >= '0' && _start[i] <= '9')) {
                LOG_ERR(
                    "tag range start is not an integer: %.*s",
                    (int)start_len, _start);
                goto bad_tag;
            }

            start *= 10;
            start += _start[i] - '0';
        }

        long end = 0;
        for (size_t i = 0; i < end_len; i++) {
            if (!(_end[i] >= '0' && _end[i] < '9')) {
                LOG_ERR(
                    "tag range end is not an integer: %.*s",
                    (int)end_len, _end);
                goto bad_tag;
            }

            end *= 10;
            end += _end[i] - '0';
        }

        if (type_len > 9 && memcmp(type, "realtime:", 9) == 0) {
            LOG_ERR("unimplemented: realtime tag");
            goto bad_tag;
        }

        errno = 0;
        char *vend;
        long v = strtol(value, &vend, 0);
        if (errno != 0 || *vend != '\0') {
            LOG_ERR("tag value is not an integer: %s", value);
            goto bad_tag;
        }

        if (v < start || v > end) {
            LOG_ERR("tag value is outside range: %ld <= %ld <= %ld",
                    start, v, end);
            goto bad_tag;
        }

        tag = tag_new_int_range(mod, name, v, start, end);
    }

    else {
        goto bad_tag;
    }

    free(name);
    free(value);
    return tag;

bad_tag:
    LOG_ERR("invalid tag: %.*s", (int)len, line);
    free(name);
    free(value);
    return NULL;
}

static void
process_transaction(struct module *mod, size_t size)
{
    struct private *m = mod->private;
    mtx_lock(&mod->lock);

    size_t left = size;
    const char *line = m->recv_buf.data;

    size_t line_count = 0;
    {
        const char *p = line;
        while ((p = memchr(p, '\n', size - (p - line))) != NULL) {
            p++;
            line_count++;
        }
    }

    struct tag **old_tag_array = m->tags.tags;
    tag_set_destroy(&m->tags);
    free(old_tag_array);

    m->tags.tags = calloc(line_count, sizeof(m->tags.tags[0]));
    m->tags.count = line_count;

    size_t idx = 0;

    while (left > 0) {
        char *line_end = memchr(line, '\n', left);
        assert(line_end != NULL);

        size_t line_len = line_end - line;

        struct tag *tag = process_line(mod, line, line_len);
        if (tag != NULL)
            m->tags.tags[idx++] = tag;

        left -= line_len + 1;
        line += line_len + 1;
    }

    m->tags.count = idx;

    mtx_unlock(&mod->lock);
    mod->bar->refresh(mod->bar);
}

static bool
data_received(struct module *mod, const char *data, size_t len)
{
    struct private *m = mod->private;

    if (len > m->recv_buf.sz - m->recv_buf.idx) {
        size_t new_sz = m->recv_buf.sz == 0 ? 1024 : m->recv_buf.sz * 2;
        char *new_buf = realloc(m->recv_buf.data, new_sz);

        if (new_buf == NULL)
            return false;

        m->recv_buf.data = new_buf;
        m->recv_buf.sz = new_sz;
    }

    assert(m->recv_buf.sz >= m->recv_buf.idx);
    assert(m->recv_buf.sz - m->recv_buf.idx >= len);

    memcpy(&m->recv_buf.data[m->recv_buf.idx], data, len);
    m->recv_buf.idx += len;

    const char *eot = memmem(m->recv_buf.data, m->recv_buf.idx, "\n\n", 2);
    if (eot == NULL) {
        /* End of transaction not yet available */
        return true;
    }

    const size_t transaction_size = eot - m->recv_buf.data + 1;
    process_transaction(mod, transaction_size);

    assert(m->recv_buf.idx >= transaction_size + 1);
    memmove(m->recv_buf.data,
            &m->recv_buf.data[transaction_size + 1],
            m->recv_buf.idx - (transaction_size + 1));
    m->recv_buf.idx -= transaction_size + 1;

    return true;
}

static int
run_loop(struct module *mod, pid_t pid, int comm_fd)
{
    int ret = 0;

    while (true) {
        struct pollfd fds[] = {
            {.fd = mod->abort_fd, .events = POLLIN},
            {.fd = comm_fd, .events = POLLIN},
        };

        int r = poll(fds, sizeof(fds) / sizeof(fds[0]), -1);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            LOG_ERRNO("failed to poll");
            break;
        }

        if (fds[1].revents & POLLIN) {
            char data[4096];
            ssize_t amount = read(comm_fd, data, sizeof(data));
            if (amount < 0) {
                LOG_ERRNO("failed to read from script");
                break;
            }

            LOG_DBG("recv: \"%.*s\"", (int)amount, data);

            data_received(mod, data, amount);
        }

        if (fds[0].revents & POLLHUP) {
            /* Aborted */
            break;
        }

        if (fds[1].revents & POLLHUP) {
            /* Child's stdout closed */
            LOG_DBG("script pipe closed (script terminated?)");
            break;
        }

        if (fds[0].revents & POLLIN) {
            /* Aborted */
            break;
        }
    }

    return ret;
}

static int
run(struct module *mod)
{
    struct private *m = mod->private;

    /* Pipe to detect exec() failures */
    int exec_pipe[2];
    if (pipe2(exec_pipe, O_CLOEXEC) < 0) {
        LOG_ERRNO("failed to create pipe");
        return -1;
    }

    /* Stdout redirection pipe */
    int comm_pipe[2];
    if (pipe(comm_pipe) < 0) {
        LOG_ERRNO("failed to create stdin/stdout redirection pipe");
        close(exec_pipe[0]);
        close(exec_pipe[1]);
        return -1;
    }

    int pid = fork();
    if (pid < 0) {
        LOG_ERRNO("failed to fork");
        close(comm_pipe[0]);
        close(comm_pipe[1]);
        close(exec_pipe[0]);
        close(exec_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        /* Child */

        /* Construct argv for execvp() */
        char *argv[1 + m->argc + 1];
        argv[0] = m->path;
        for (size_t i = 0; i < m->argc; i++)
            argv[i + 1] = m->argv[i];
        argv[1 + m->argc] = NULL;

        /* Restore signal handlers and signal mask */
        sigset_t mask;
        sigemptyset(&mask);

        const struct sigaction sa = {.sa_handler = SIG_DFL};
        if (sigaction(SIGINT, &sa, NULL) < 0 ||
            sigaction(SIGTERM, &sa, NULL) < 0 ||
            sigaction(SIGCHLD, &sa, NULL) < 0 ||
            sigprocmask(SIG_SETMASK, &mask, NULL) < 0)
        {
            goto fail;
        }

        /* New process group, so that we can use killpg()  */
        setpgid(0, 0);

        /* Close pipe read ends */
        close(exec_pipe[0]);
        close(comm_pipe[0]);

        /* Re-direct stdin/stdout */
        int dev_null = open("/dev/null", O_RDONLY);
        if (dev_null < 0)
            goto fail;

        if (dup2(dev_null, STDIN_FILENO) < 0 ||
            dup2(comm_pipe[1], STDOUT_FILENO) < 0)
        {
            goto fail;
        }

        /* We're done with the redirection pipe */
        close(comm_pipe[1]);
        comm_pipe[1] = -1;

        /* Close *all* other FDs */
        for (int i = STDERR_FILENO + 1; i < 65536; i++) {
            if (i == exec_pipe[1]) {
                /* Needed for error reporting. Automatically closed
                 * when execvp() succeeds */
                continue;
            }
            close(i);
        }

        execvp(m->path, argv);

    fail:
        (void)!write(exec_pipe[1], &errno, sizeof(errno));
        close(exec_pipe[1]);
        if (comm_pipe[1] >= 0)
            close(comm_pipe[1]);
        _exit(errno);
    }

    /* Close pipe write ends */
    close(exec_pipe[1]);
    close(comm_pipe[1]);

    int _errno;
    static_assert(sizeof(_errno) == sizeof(errno), "errno size mismatch");

    /* Wait for errno from child, or FD being closed in execvp() */
    int r = read(exec_pipe[0], &_errno, sizeof(_errno));
    close(exec_pipe[0]);

    if (r < 0) {
        LOG_ERRNO("failed to read from pipe");
        close(comm_pipe[0]);
        return -1;
    }

    if (r > 0) {
        LOG_ERRNO_P("%s: failed to start", _errno, m->path);
        close(comm_pipe[0]);
        waitpid(pid, NULL, 0);
        return -1;
    }

    /* Pipe was closed. I.e. execvp() succeeded */
    assert(r == 0);
    LOG_DBG("script running under PID=%u", pid);

    int ret = run_loop(mod, pid, comm_pipe[0]);
    close(comm_pipe[0]);

    if (waitpid(pid, NULL, WNOHANG) == 0) {
        static const struct {
            int signo;
            int timeout;
            const char *name;
        } sig_info[] = {
            {SIGINT, 2, "SIGINT"},
            {SIGTERM, 5, "SIGTERM"},
            {SIGKILL, 0, "SIGKILL"},
        };

        for (size_t i = 0; i < sizeof(sig_info) / sizeof(sig_info[0]); i++) {
            struct timeval start;
            gettimeofday(&start, NULL);

            const int signo = sig_info[i].signo;
            const int timeout = sig_info[i].timeout;
            const char *const name __attribute__((unused)) = sig_info[i].name;

            LOG_DBG("sending %s to PID=%u (timeout=%ds)", name, pid, timeout);
            killpg(pid, signo);

            /*
             * Child is unlikely to terminate *immediately*. Wait a
             * *short* period of time before checking waitpid() the
             * first time
             */
            usleep(10000);

            pid_t waited_pid;
            while ((waited_pid = waitpid(
                        pid, NULL, timeout > 0 ? WNOHANG : 0)) == 0)
            {
                struct timeval now;
                gettimeofday(&now, NULL);

                struct timeval elapsed;
                timersub(&now, &start, &elapsed);

                if (elapsed.tv_sec >= timeout)
                    break;

                /* Don't spinning */
                thrd_yield();
                usleep(100000);  /* 100ms */
            }

            if (waited_pid == pid) {
                /* Child finally dead */
                break;
            }
        }
    } else
        LOG_DBG("PID=%u already terminated", pid);

    return ret;
}

static struct module *
script_new(const char *path, size_t argc, const char *const argv[static argc],
           struct particle *_content)
{
    struct private *m = calloc(1, sizeof(*m));
    m->path = strdup(path);
    m->content = _content;
    m->argc = argc;
    m->argv = malloc(argc * sizeof(m->argv[0]));
    for (size_t i = 0; i < argc; i++)
        m->argv[i] = strdup(argv[i]);

    struct module *mod = module_common_new();
    mod->private = m;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    return mod;
}

static struct module *
from_conf(const struct yml_node *node, struct conf_inherit inherited)
{
    const struct yml_node *path = yml_get_value(node, "path");
    const struct yml_node *args = yml_get_value(node, "args");
    const struct yml_node *c = yml_get_value(node, "content");

    size_t argc = args != NULL ? yml_list_length(args) : 0;
    const char *argv[argc];

    if (args != NULL) {
        size_t i = 0;
        for (struct yml_list_iter iter = yml_list_iter(args);
             iter.node != NULL;
             yml_list_next(&iter), i++)
        {
            argv[i] = yml_value_as_string(iter.node);
        }
    }

    return script_new(
        yml_value_as_string(path), argc, argv, conf_to_particle(c, inherited));
}

static bool
conf_verify_path(keychain_t *chain, const struct yml_node *node)
{
    if (!conf_verify_string(chain, node))
        return false;

    const char *path = yml_value_as_string(node);
    if (strlen(path) < 1 || path[0] != '/') {
        LOG_ERR("%s: path must be absolute", conf_err_prefix(chain, node));
        return false;
    }

    return true;
}

static bool
conf_verify_args(keychain_t *chain, const struct yml_node *node)
{
    return conf_verify_list(chain, node, &conf_verify_string);
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"path", true, &conf_verify_path},
        {"args", false, &conf_verify_args},
        MODULE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct module_iface module_script_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct module_iface iface __attribute__((weak, alias("module_script_iface")));
#endif
