#include "network.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <poll.h>

#include <arpa/inet.h>
#include <linux/if.h>

#include <netlink/netlink.h>
#include <netlink/route/addr.h>
#include <netlink/route/link.h>

#define LOG_MODULE "network"
#define LOG_ENABLE_DBG 1
#include "../log.h"
#include "../module.h"
#include "../bar.h"
#include "../tllist.h"

struct af_addr {
    int family;
    union {
        struct in_addr ipv4;
        struct in6_addr ipv6;
    } u;
};

struct private {
    char *iface;
    struct particle *label;

    int ifindex;
    uint8_t mac[6];
    uint8_t state;  /* IFLA_OPERSTATE */

    /* IPv4 and IPv6 addresses */
    tll(struct af_addr) addrs;
};

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;

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
            inet_ntop(AF_INET, &it->item.u.ipv4, ipv4_str, sizeof(ipv4_str));
        else if (it->item.family == AF_INET6)
            inet_ntop(AF_INET6, &it->item.u.ipv6, ipv6_str, sizeof(ipv6_str));
    }

    struct tag_set tags = {
        .tags = (struct tag *[]){
            tag_new_string(mod, "name", m->iface),
            tag_new_int(mod, "index", m->ifindex),
            tag_new_string(mod, "state", state),
            tag_new_string(mod, "mac", mac_str),
            tag_new_string(mod, "ipv4", ipv4_str),
            tag_new_string(mod, "ipv6", ipv6_str),
        },
        .count = 6,
    };

    mtx_unlock(&mod->lock);

    struct exposable *exposable =  m->label->instantiate(m->label, &tags);
    tag_set_destroy(&tags);
    return exposable;
}

static int
nl_event(struct nl_msg *msg, void *arg)
{
    struct module *mod = arg;
    struct private *m = mod->private;

    struct nlmsghdr *hdr = nlmsg_hdr(msg);

    switch (hdr->nlmsg_type) {
    case RTM_NEWLINK:
    case RTM_DELLINK: {
        /* First, ignore if this isn't for us */
        const struct ifinfomsg *info = nlmsg_data(hdr);
        if (info->ifi_index != m->ifindex)
            break;

        /* Parse attributes */
        struct nlattr *attrs[IFLA_MAX + 1];
        int r = nlmsg_parse(hdr, sizeof(*info), attrs, IFLA_MAX, NULL);
        if (r < 0) {
            LOG_ERR("failed to parse attributes");
            break;
        }

        assert(strcmp(nla_get_string(attrs[IFLA_IFNAME]), m->iface) == 0);

        mtx_lock(&mod->lock);

        uint8_t old_state = m->state;
        uint8_t new_state = attrs[IFLA_OPERSTATE] != NULL
            ? nla_get_u8(attrs[IFLA_OPERSTATE])
            : IF_OPER_DOWN;

        if (old_state != new_state) {
            LOG_DBG(
                "%s: %s: state: %hhu -> %hhu",
                hdr->nlmsg_type == RTM_NEWLINK ? "RTM_NEWLINK" : "RTM_DELLINK",
                m->iface, old_state, new_state);

            m->state = new_state;
            mod->bar->refresh(mod->bar);
        }

        mtx_unlock(&mod->lock);
        break;
    }

    case RTM_NEWADDR:
    case RTM_DELADDR: {
        /* Ignore if not for us */
        const struct ifaddrmsg *addr = nlmsg_data(hdr);
        if (addr->ifa_index != m->ifindex)
            break;

        /* We only support IPv4 and IPv6 */
        if (addr->ifa_family != AF_INET && addr->ifa_family != AF_INET6)
            break;

        /* Parse attributes */
        struct nlattr *attrs[IFA_MAX + 1];
        int r = nlmsg_parse(hdr, sizeof(*addr), attrs, IFA_MAX, NULL);
        if (r < 0) {
            LOG_ERR("failed to parse attributes");
            break;
        }

        /* raw_addr is now a "struct in_addr" or a "struct in6_addr" */
        const void *raw_addr = nla_data(attrs[IFA_ADDRESS]);
        size_t addr_len = nla_len(attrs[IFA_ADDRESS]);

#if defined(LOG_ENABLE_DBG) && LOG_ENABLE_DBG
        char s[INET6_ADDRSTRLEN];
        inet_ntop(addr->ifa_family, raw_addr, s, sizeof(s));
#endif

        LOG_DBG(
            "%s: %s: family=%d, %s",
            hdr->nlmsg_type == RTM_NEWADDR ? "RTM_NEWADDR" : "RTM_DELADDR",
            m->iface, addr->ifa_family, s);

        mtx_lock(&mod->lock);

        if (hdr->nlmsg_type == RTM_DELADDR) {
            /* Find address in our list and remove it */
            tll_foreach(m->addrs, it) {
                if (it->item.family != addr->ifa_family)
                    continue;

                if (memcmp(&it->item.u, raw_addr, addr_len) != 0)
                    continue;

                tll_remove(m->addrs, it);
                break;
            }
        } else {
            /* Append address to our list */
            struct af_addr a = {.family = addr->ifa_family};
            memcpy(&a.u, raw_addr, addr_len);
            tll_push_back(m->addrs, a);
        }

        mtx_unlock(&mod->lock);
        mod->bar->refresh(mod->bar);
        break;
    }

    default:
        LOG_WARN("unknown message type: 0x%x", hdr->nlmsg_type);
        break;
    }

    return NL_OK;
}

static int
run(struct module_run_context *ctx)
{
    struct module *mod = ctx->module;
    struct private *m = mod->private;

    module_signal_ready(ctx);

    struct nl_sock *conn = nl_socket_alloc();
    if (conn == NULL) {
        LOG_ERR("failed to allocate netlink socket");
        return 1;
    }

    int r = nl_connect(conn, NETLINK_ROUTE);
    if (r < 0) {
        LOG_ERR("failed to connect to netlink socket");
        nl_socket_free(conn);
        return 1;
    }

    /* TODO: get just our link */
    struct nl_cache *links = NULL;
    r = rtnl_link_alloc_cache(conn, AF_UNSPEC, &links);
    if (r < 0) {
        LOG_ERR("failed to fill link cache: %s", nl_geterror(r));
        nl_socket_free(conn);
        return 1;
    }

    struct rtnl_link *link = rtnl_link_get_by_name(links, m->iface);
    if (link == 0) {
        LOG_ERR("%s: no such link", m->iface);
        nl_cache_free(links);
        nl_socket_free(conn);
        return 1;
    }

    m->ifindex = rtnl_link_get_ifindex(link);
    LOG_DBG("%s: ifindex: %d", m->iface, m->ifindex);

    /* TODO: expose this in a tag. Need to figure out which event is
     * triggered by this */
    uint8_t carrier = rtnl_link_get_carrier(link);
    LOG_DBG("%s: carrier: %hhu", m->iface, carrier);

    /* down/up etc */
    m->state = rtnl_link_get_operstate(link);
    LOG_DBG("%s: operstate = %hhu", m->iface, m->state);

    struct nl_addr *mac = rtnl_link_get_addr(link);
    assert(nl_addr_get_len(mac) == 6);
    memcpy(m->mac, nl_addr_get_binary_addr(mac), sizeof(m->mac));

    char mac_str[2 * 6 + 5 + 1];
    nl_addr2str(mac, mac_str, sizeof(mac_str));
    LOG_DBG("%s: MAC: %s", m->iface, mac_str);

    struct nl_cache *addrs;
    r = rtnl_addr_alloc_cache(conn, &addrs);
    if (r < 0) {
        LOG_ERR("failed to address cache: %s", nl_geterror(r));
        assert(false);
    }

    /* Add all IPv4 and IPv6 addresses (that belongs to us) to our list */
    for (struct rtnl_addr *addr = (struct rtnl_addr *)nl_cache_get_first(addrs);
         addr != NULL;
         addr = (struct rtnl_addr *)nl_cache_get_next((struct nl_object *)addr))
    {
        if (rtnl_addr_get_ifindex(addr) != m->ifindex)
            continue;

        int family = rtnl_addr_get_family(addr);
        if (family != AF_INET && family != AF_INET6)
            continue;

        struct nl_addr *local = rtnl_addr_get_local(addr);
        struct af_addr a = {.family = family};
        memcpy(&a.u, nl_addr_get_binary_addr(local), nl_addr_get_len(local));
        tll_push_back(m->addrs, a);

        char s[INET6_ADDRSTRLEN];
        inet_ntop(family, &a.u, s, sizeof(s));
        LOG_DBG("%s: address: %s", m->iface, s);

    }

    rtnl_link_put(link);
    nl_cache_free(addrs);
    nl_cache_free(links);

    /* Configure a callback to handle netlink events */
    r = nl_socket_modify_cb(conn, NL_CB_VALID, NL_CB_CUSTOM, &nl_event, mod);
    if (r < 0)
        LOG_ERR("falied to set netlink callback: %s", nl_geterror(r));

    /* Register for link and IPv4/IPv6 address changes */
    r = nl_socket_add_memberships(
        conn,
        RTNLGRP_LINK,
        RTNLGRP_IPV4_IFADDR,
        RTNLGRP_IPV6_IFADDR,
        RTNLGRP_NONE);
    if (r < 0)
        LOG_ERR("failed to register for notifications: %s", nl_geterror(r));

    /* Events have no sequence numbers, thus disable checks */
    nl_socket_disable_seq_check(conn);

    const int nl_fd = nl_socket_get_fd(conn);
    while (true) {
        struct pollfd fds[] = {
            {.fd = ctx->abort_fd, .events = POLLIN},
            {.fd = nl_fd, .events = POLLIN}
        };

        r = poll(fds, 2, -1);
        if (r == -1) {
            LOG_ERRNO("poll() failed");
            break;
        }

        if (fds[0].revents && POLLIN)
            break;

        if (fds[1].revents & POLLHUP) {
            LOG_ERR("disconnected from netlink");
            break;
        }

        assert(fds[1].revents & POLLIN);

        r = nl_recvmsgs_default(conn);
        if (r < 0)
            LOG_ERR("failed to receive: %s", nl_geterror(r));
    }

    nl_close(conn);
    nl_socket_free(conn);
    return 0;
}

struct module *
module_network(const char *iface, struct particle *label)
{
    struct private *priv = malloc(sizeof(*priv));
    priv->iface = strdup(iface);
    priv->label = label;

    priv->ifindex = 0;
    priv->state = IF_OPER_DOWN;
    memset(&priv->addrs, 0, sizeof(priv->addrs));

    struct module *mod = module_common_new();
    mod->private = priv;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    return mod;
}
