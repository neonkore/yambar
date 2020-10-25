#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>

#include <poll.h>
#include <fcntl.h>

#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/signalfd.h>

#define LOG_MODULE "script"
#define LOG_ENABLE_DBG 1
#include "../log.h"
#include "../config.h"
#include "../config-verify.h"
#include "../module.h"
#include "../plugin.h"

struct private {
    char *path;
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
    tag_set_destroy(&m->tags);
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
    const char *_name = line;
    LOG_INFO("LINE: %.*s", (int)len, line);

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

    char *name = malloc(name_len + 1);
    memcpy(name, _name, name_len);
    name[name_len] = '\0';

    struct tag *tag = NULL;

    if (type_len == 6 && memcmp(type, "string", 6) == 0)
        tag = tag_new_string(mod, name, _value);

    else if (type_len == 3 && memcmp(type, "int", 3) == 0) {
        long value = strtol(_value, NULL, 0);
        tag = tag_new_int(mod, name, value);
    }

    else if (type_len == 4 && memcmp(type, "bool", 4) == 0) {
        bool value = strtol(_value, NULL, 0);
        tag = tag_new_bool(mod, name, value);
    }

    else if (type_len == 5 && memcmp(type, "float", 5) == 0) {
        double value = strtod(_value, NULL);
        tag = tag_new_float(mod, name, value);
    }

    else if ((type_len > 6 && memcmp(type, "range:", 6) == 0) ||
             (type_len > 9 && memcmp(type, "realtime:", 9 == 0)))
    {
        const char *_start = type + 6;
        const char *split = memchr(_start, '-', type_len - 6);

        if (split == NULL || split == _start || (split + 1) - type >= type_len) {
            free(name);
            goto bad_tag;
        }

        const char *_end = split + 1;

        size_t start_len = split - _start;
        size_t end_len = type + type_len - _end;

        long start = 0;
        for (size_t i = 0; i < start_len; i++) {
            if (!(_start[i] >= '0' && _start[i] <= '9')) {
                free(name);
                goto bad_tag;
            }

            start *= 10;
            start |= _start[i] - '0';
        }

        long end = 0;
        for (size_t i = 0; i < end_len; i++) {
            if (!(_end[i] >= '0' && _end[i] < '9')) {
                free(name);
                goto bad_tag;
            }

            end *= 10;
            end |= _end[i] - '0';
        }

        if (type_len > 9 && memcmp(type, "realtime:", 9) == 0) {
            free(name);
            LOG_WARN("unimplemented: realtime tag");
            goto bad_tag;
        }

        long value = strtol(_value, NULL, 0);
        tag = tag_new_int_range(mod, name, value, start, end);
    }

    else {
        free(name);
        goto bad_tag;
    }

    free(name);
    return tag;

bad_tag:
    LOG_ERR("invalid: %.*s", (int)len, line);
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

    tag_set_destroy(&m->tags);
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
run_loop(struct module *mod, int comm_fd)
{
    //struct private *m = mod;

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);

    /* Block normal signal handling - we're using a signalfd instead */
    sigset_t original_mask;
    if (pthread_sigmask(SIG_BLOCK, &mask, &original_mask) < 0) {
        LOG_ERRNO("failed to block SIGCHLD");
        return -1;
    }

    int sig_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sig_fd < 0) {
        LOG_ERRNO("failed to create signal FD");
        pthread_sigmask(SIG_SETMASK, &original_mask, NULL);
        return -1;
    }

    int ret = 0;

    while (true) {
        struct pollfd fds[] = {
            {.fd = mod->abort_fd, .events = POLLIN},
            {.fd = sig_fd, .events = POLLIN},
            {.fd = comm_fd, .events = POLLIN},
        };

        int r = poll(fds, sizeof(fds) / sizeof(fds[0]), -1);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            LOG_ERRNO("failed to poll");
            break;
        }

        if (fds[2].revents & POLLIN) {
            char data[4096];
            ssize_t amount = read(comm_fd, data, sizeof(data));
            if (amount < 0) {
                LOG_ERRNO("failed to read from script");
                break;
            }

            data_received(mod, data, amount);
        }

        if (fds[0].revents & POLLHUP) {
            /* Aborted */
            break;
        }

        if (fds[1].revents & POLLHUP) {
            LOG_ERR("signal FD closed unexpectedly");
            ret = 1;
            break;
        }

        if (fds[2].revents & POLLHUP) {
            /* Child's stdout closed */
            break;
        }

        if (fds[0].revents & POLLIN)
            break;

        if (fds[1].revents & POLLIN) {
            struct signalfd_siginfo info;
            ssize_t amount = read(sig_fd, &info, sizeof(info));

            if (amount < 0) {
                LOG_ERRNO("failed to read from signal FD");
                break;
            }

            assert(info.ssi_signo == SIGCHLD);
            LOG_WARN("script died");
            break;
        }
    }

    close(sig_fd);
    pthread_sigmask(SIG_SETMASK, &original_mask, NULL);
    return ret;
}

static int
run(struct module *mod)
{
    struct private *m = mod->private;

    int exec_pipe[2];
    if (pipe2(exec_pipe, O_CLOEXEC) < 0) {
        LOG_ERRNO("failed to create pipe");
        return -1;
    }

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

        setsid();
        setpgid(0, 0);

        /* Close pipe read ends */
        close(exec_pipe[0]);
        close(comm_pipe[0]);

        /* Re-direct stdin/stdout/stderr */
        int dev_null = open("/dev/null", O_RDWR);
        if (dev_null < 0)
            goto fail;

        if (dup2(dev_null, STDIN_FILENO) < 0 ||
            dup2(dev_null, STDERR_FILENO) < 0 ||
            dup2(comm_pipe[1], STDOUT_FILENO) < 0)
        {
            goto fail;
        }

        close(comm_pipe[1]);

        char *const argv[] = {NULL};
        execvp(m->path, argv);

    fail:
        write(exec_pipe[1], &errno, sizeof(errno));
        close(exec_pipe[1]);
        close(comm_pipe[1]);
        _exit(errno);
    }

    /* Close pipe write ends */
    close(exec_pipe[1]);
    close(comm_pipe[1]);

    int _errno;
    static_assert(sizeof(_errno) == sizeof(errno), "errno size mismatch");

    int r = read(exec_pipe[0], &_errno, sizeof(_errno));
    close(exec_pipe[0]);

    if (r < 0) {
        LOG_ERRNO("failed to read from pipe");
        return -1;
    }

    if (r > 0) {
        LOG_ERRNO_P("%s: failed to start", _errno, m->path);
        waitpid(pid, NULL, 0);
        return -1;
    }

    LOG_WARN("child running under PID=%u", pid);

    int ret = run_loop(mod, comm_pipe[0]);

    close(comm_pipe[0]);
    if (waitpid(pid, NULL, WNOHANG) == 0) {
        LOG_WARN("sending SIGTERM to PGRP=%u", pid);
        killpg(pid, SIGTERM);

        /* TODO: send SIGKILL after X seconds */
        waitpid(pid, NULL, 0);
    }

    return ret;
}

static struct module *
script_new(const char *path, struct particle *_content)
{
    struct private *m = calloc(1, sizeof(*m));
    m->path = strdup(path);
    m->content = _content;

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
    const struct yml_node *run = yml_get_value(node, "path");
    const struct yml_node *c = yml_get_value(node, "content");
    return script_new(yml_value_as_string(run), conf_to_particle(c, inherited));
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"path", true, &conf_verify_string},
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
