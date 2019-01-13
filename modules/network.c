#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include <threads.h>
#include <poll.h>

#include <arpa/inet.h>
#include <linux/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#define LOG_MODULE "network"
#define LOG_ENABLE_DBG 0
#include "../log.h"
#include "../bar.h"
#include "../config.h"
#include "../module.h"
#include "../tllist.h"

struct af_addr {
    int family;
    union {
        struct in_addr ipv4;
        struct in6_addr ipv6;
    } addr;
};

struct private {
    char *iface;
    struct particle *label;

    int nl_sock;

    bool get_addresses;

    int ifindex;
    uint8_t mac[6];
    bool carrier;
    uint8_t state;  /* IFLA_OPERSTATE */

    /* IPv4 and IPv6 addresses */
    tll(struct af_addr) addrs;
};

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;

    assert(m->nl_sock == -1);

    m->label->destroy(m->label);

    tll_free(m->addrs);

    free(m->iface);
    free(m);

    module_default_destroy(mod);
}

static struct exposable *
content(struct module *mod)
{
    struct private *m = mod->private;

    mtx_lock(&mod->lock);

    const char *state = NULL;
    switch (m->state) {
    case IF_OPER_UNKNOWN:         state = "unknown"; break;
    case IF_OPER_NOTPRESENT:      state = "not present"; break;
    case IF_OPER_DOWN:            state = "down"; break;
    case IF_OPER_LOWERLAYERDOWN:  state = "lower layers down"; break;
    case IF_OPER_TESTING:         state = "testing"; break;
    case IF_OPER_DORMANT:         state = "dormant"; break;
    case IF_OPER_UP:              state = "up"; break;
    default:                      state = "unknown"; break;
    }

    char mac_str[6 * 2 + 5 + 1];
    char ipv4_str[INET_ADDRSTRLEN] = {0};
    char ipv6_str[INET6_ADDRSTRLEN] = {0};

    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             m->mac[0], m->mac[1], m->mac[2], m->mac[3], m->mac[4], m->mac[5]);

    /* TODO: this exposes the *last* added address of each kind. Can
     * we expose all in some way? */
    tll_foreach(m->addrs, it) {
        if (it->item.family == AF_INET)
            inet_ntop(AF_INET, &it->item.addr.ipv4, ipv4_str, sizeof(ipv4_str));
        else if (it->item.family == AF_INET6)
            inet_ntop(AF_INET6, &it->item.addr.ipv6, ipv6_str, sizeof(ipv6_str));
    }

    struct tag_set tags = {
        .tags = (struct tag *[]){
            tag_new_string(mod, "name", m->iface),
            tag_new_int(mod, "index", m->ifindex),
            tag_new_bool(mod, "carrier", m->carrier),
            tag_new_string(mod, "state", state),
            tag_new_string(mod, "mac", mac_str),
            tag_new_string(mod, "ipv4", ipv4_str),
            tag_new_string(mod, "ipv6", ipv6_str),
        },
        .count = 7,
    };

    mtx_unlock(&mod->lock);

    struct exposable *exposable =  m->label->instantiate(m->label, &tags);
    tag_set_destroy(&tags);
    return exposable;
}

/* Returns a value suitable for nl_pid/nlmsg_pid */
static uint32_t
nl_pid_value(void)
{
    return thrd_current() ^ getpid();
}

/* Connect and bind to netlink socket. Returns socket fd, or -1 on error */
static int
netlink_connect(void)
{
    int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (sock == -1) {
        LOG_ERRNO("failed to create netlink socket");
        return -1;
    }

    const struct sockaddr_nl addr = {
        .nl_family = AF_NETLINK,
        .nl_pid = nl_pid_value(),
        .nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR,
    };

    LOG_WARN("nl_pid_value = 0x%08x", addr.nl_pid);

    if (bind(sock, (const struct sockaddr *)&addr, sizeof(addr)) == -1) {
        LOG_ERRNO("failed to bind netlink socket");
        close(sock);
        return -1;
    }

    return sock;
}

static bool
send_rt_request(int nl_sock, int request)
{
    struct {
        struct nlmsghdr hdr;
        struct rtgenmsg rt;
    } req = {
        .hdr = {
            .nlmsg_len = NLMSG_LENGTH(sizeof(req.rt)),
            .nlmsg_type = request,
            .nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP,
            .nlmsg_seq = 1,
            .nlmsg_pid = nl_pid_value(),
        },

        .rt = {
            .rtgen_family = AF_UNSPEC,
        },
    };

    int r = sendto(
        nl_sock, &req, req.hdr.nlmsg_len, 0,
        (struct sockaddr *)&(struct sockaddr_nl){.nl_family = AF_NETLINK},
        sizeof(struct sockaddr_nl));

    if (r == -1) {
        LOG_ERRNO("failed to send netlink request");
        return false;
    }

    return true;
}
static bool
find_my_ifindex(struct module *mod, const struct ifinfomsg *msg, size_t len)
{
    struct private *m = mod->private;

    for (const struct rtattr *attr = IFLA_RTA(msg);
         RTA_OK(attr, len);
         attr = RTA_NEXT(attr, len))
    {
        switch (attr->rta_type) {
        case IFLA_IFNAME:
            if (strcmp((const char *)RTA_DATA(attr), m->iface) == 0) {
                LOG_INFO("%s: ifindex=%d", m->iface, msg->ifi_index);

                mtx_lock(&mod->lock);
                m->ifindex = msg->ifi_index;
                mtx_unlock(&mod->lock);
                return true;
            }

            return false;
        }
    }

    return false;
}

static void
handle_link(struct module *mod, uint16_t type,
            const struct ifinfomsg *msg, size_t len)
{
    assert(type == RTM_NEWLINK || type == RTM_DELLINK);

    struct private *m = mod->private;

    if (m->ifindex == -1) {
        /* We don't know our own ifindex yet. Let's see if we can find
         * it in the message */
        if (!find_my_ifindex(mod, msg, len)) {
            /* Nope, message wasn't for us (IFLA_IFNAME mismatch) */
            return;
        }
    }

    assert(m->ifindex >= 0);

    if (msg->ifi_index != m->ifindex) {
        /* Not for us */
        return;
    }

    bool update_bar = false;

    for (const struct rtattr *attr = IFLA_RTA(msg);
         RTA_OK(attr, len);
         attr = RTA_NEXT(attr, len))
    {
        switch (attr->rta_type) {
        case IFLA_OPERSTATE: {
            uint8_t operstate = *(const uint8_t *)RTA_DATA(attr);
            if (m->state == operstate)
                break;

            LOG_DBG("%s: IFLA_OPERSTATE: %hhu -> %hhu", m->iface, m->state, operstate);

            mtx_lock(&mod->lock);
            m->state = operstate;
            mtx_unlock(&mod->lock);
            update_bar = true;
            break;
        }

        case IFLA_CARRIER: {
            uint8_t carrier = *(const uint8_t *)RTA_DATA(attr);
            if (m->carrier == carrier)
                break;

            LOG_DBG("%s: IFLA_CARRIER: %hhu -> %hhu", m->iface, m->carrier, carrier);

            mtx_lock(&mod->lock);
            m->carrier = carrier;
            mtx_unlock(&mod->lock);
            update_bar = true;
            break;
        }

        case IFLA_ADDRESS: {
            if (RTA_PAYLOAD(attr) != 6)
                break;

            const uint8_t *mac = RTA_DATA(attr);
            if (memcmp(m->mac, mac, sizeof(m->mac)) == 0)
                break;

            LOG_DBG("%s: IFLA_ADDRESS: %02x:%02x:%02x:%02x:%02x:%02x",
                    m->iface,
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

            mtx_lock(&mod->lock);
            memcpy(m->mac, mac, sizeof(m->mac));
            mtx_unlock(&mod->lock);
            update_bar = true;
            break;
        }
        }
    }

    if (update_bar)
        mod->bar->refresh(mod->bar);
}

static void
handle_address(struct module *mod, uint16_t type,
               const struct ifaddrmsg *msg, size_t len)
{
    assert(type == RTM_NEWADDR || type == RTM_DELADDR);

    struct private *m = mod->private;

    assert(m->ifindex >= 0);

    if (msg->ifa_index != m->ifindex) {
        /* Not for us */
        return;
    }

    bool update_bar = false;

    for (const struct rtattr *attr = IFA_RTA(msg);
         RTA_OK(attr, len);
         attr = RTA_NEXT(attr, len))
    {
        switch (attr->rta_type) {
        case IFA_ADDRESS: {
            const void *raw_addr = RTA_DATA(attr);
            size_t addr_len = RTA_PAYLOAD(attr);

#if defined(LOG_ENABLE_DBG) && LOG_ENABLE_DBG
            char s[INET6_ADDRSTRLEN];
            inet_ntop(msg->ifa_family, raw_addr, s, sizeof(s));
#endif
            LOG_DBG("%s: IFA_ADDRESS (%s): %s", m->iface,
                    type == RTM_NEWADDR ? "add" : "del", s);

            mtx_lock(&mod->lock);

            if (type == RTM_DELADDR) {
                /* Find address in our list and remove it */
                tll_foreach(m->addrs, it) {
                    if (it->item.family != msg->ifa_family)
                        continue;

                    if (memcmp(&it->item.addr, raw_addr, addr_len) != 0)
                        continue;

                    tll_remove(m->addrs, it);
                    update_bar = true;
                    break;
                }
            } else {
                /* Append address to our list */
                struct af_addr a = {.family = msg->ifa_family};
                memcpy(&a.addr, raw_addr, addr_len);
                tll_push_back(m->addrs, a);
                update_bar = true;
            }

            mtx_unlock(&mod->lock);
            break;
        }
        }
    }

    if (update_bar)
        mod->bar->refresh(mod->bar);
}

/*
 * Reads at least one (possibly more) message.
 *
 * On success, 'reply' will point to a malloc:ed buffer, to be freed
 * by the caller. 'len' is set to the size of the message (note that
 * the allocated size may actually be larger).
 *
 * Returns true on success, otherwise false
 */
static bool
netlink_receive_messages(int sock, void **reply, size_t *len)
{
    /* Use MSG_PEEK to find out how large buffer we need */
    const size_t chunk_sz = 1024;
    size_t sz = chunk_sz;
    *reply = malloc(sz);

    while (true) {
        ssize_t bytes = recvfrom(sock, *reply, sz, MSG_PEEK, NULL, NULL);
        if (bytes == -1) {
            LOG_ERRNO("failed to receive from netlink socket");
            free(*reply);
            return false;
        }

        if (bytes < sz)
            break;

        sz += chunk_sz;
        *reply = realloc(*reply, sz);
    }

    *len = recvfrom(sock, *reply, sz, 0, NULL, NULL);
    assert(*len >= 0);
    assert(*len < sz);
    return true;
}

static bool
parse_reply(struct module *mod, const struct nlmsghdr *hdr, size_t len)
{
    struct private *m = mod->private;

    /* Process response */
    for (; NLMSG_OK(hdr, len); hdr = NLMSG_NEXT(hdr, len)) {
        switch (hdr->nlmsg_type) {
        case NLMSG_DONE:
            if (m->ifindex == -1) {
                LOG_ERR("%s: failed to find interface", m->iface);
                return false;
            }

            /* Request initial list of IPv4/6 addresses */
            if (m->get_addresses && m->ifindex != -1) {
                m->get_addresses = false;
                send_rt_request(m->nl_sock, RTM_GETADDR);
            }
            break;

        case RTM_NEWLINK:
        case RTM_DELLINK: {
            const struct ifinfomsg *msg = NLMSG_DATA(hdr);
            size_t msg_len = IFLA_PAYLOAD(hdr);

            handle_link(mod, hdr->nlmsg_type, msg, msg_len);
            break;
        }

        case RTM_NEWADDR:
        case RTM_DELADDR: {
            const struct ifaddrmsg *msg = NLMSG_DATA(hdr);
            size_t msg_len = IFA_PAYLOAD(hdr);

            handle_address(mod, hdr->nlmsg_type, msg, msg_len);
            break;
        }

        case NLMSG_ERROR:{
            const struct nlmsgerr *err = NLMSG_DATA(hdr);
            LOG_ERRNO_P("netlink", err->error);
            return false;
        }

        default:
            LOG_WARN(
                "unrecognized netlink message type: 0x%x", hdr->nlmsg_type);
            break;
        }
    }

    return true;
}

static int
run(struct module_run_context *ctx)
{
    struct module *mod = ctx->module;
    struct private *m = mod->private;

    module_signal_ready(ctx);

    m->nl_sock = netlink_connect();
    if (m->nl_sock == -1)
        return 1;

    if (!send_rt_request(m->nl_sock, RTM_GETLINK)) {
        close(m->nl_sock);
        m->nl_sock = -1;
        return 1;
    }

    /* Main loop */
    while (true) {
        struct pollfd fds[] = {
            {.fd = ctx->abort_fd, .events = POLLIN},
            {.fd = m->nl_sock, .events = POLLIN}
        };

        poll(fds, 2, -1);

        if (fds[0].revents & POLLIN)
            break;

        if (fds[1].revents & POLLHUP) {
            LOG_ERR("disconnected from netlink socket");
            break;
        }

        assert(fds[1].revents & POLLIN);

        /* Read one (or more) messages */
        void *reply;
        size_t len;
        if (!netlink_receive_messages(m->nl_sock, &reply, &len))
            break;

        /* Parse (and act upon) the received message(s) */
        if (!parse_reply(mod, (const struct nlmsghdr *)reply, len)) {
            free(reply);
            break;
        }

        free(reply);
    }

    close(m->nl_sock);
    m->nl_sock = -1;
    return 0;
}

static struct module *
network_new(const char *iface, struct particle *label)
{
    struct private *priv = malloc(sizeof(*priv));
    priv->iface = strdup(iface);
    priv->label = label;

    priv->nl_sock = -1;
    priv->get_addresses = true;
    priv->ifindex = -1;
    memset(priv->mac, 0, sizeof(priv->mac));
    priv->carrier = false;
    priv->state = IF_OPER_DOWN;
    memset(&priv->addrs, 0, sizeof(priv->addrs));

    struct module *mod = module_common_new();
    mod->private = priv;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    return mod;
}

static struct module *
from_conf(const struct yml_node *node, const struct font *parent_font)
{
    const struct yml_node *name = yml_get_value(node, "name");
    const struct yml_node *content = yml_get_value(node, "content");

    return network_new(
        yml_value_as_string(name), conf_to_particle(content, parent_font));
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"name", true, &conf_verify_string},
        {"content", true, &conf_verify_particle},
        {"anchors", false, NULL},
        {NULL, false, NULL}
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct module_info plugin_info = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};
