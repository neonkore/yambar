#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>

#include <threads.h>
#include <poll.h>
#include <sys/timerfd.h>

#include <sys/stat.h>
#include <fcntl.h>

#include <arpa/inet.h>
#include <linux/if.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>
#include <linux/rtnetlink.h>
#include <linux/nl80211.h>

#include <tllist.h>

#define LOG_MODULE "network"
#define LOG_ENABLE_DBG 0
#include "../log.h"
#include "../bar/bar.h"
#include "../config.h"
#include "../config-verify.h"
#include "../module.h"
#include "../plugin.h"

#define UNUSED __attribute__((unused))

struct rt_stats_msg {
    struct rtmsg rth;
    struct rtnl_link_stats64 stats;
};

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
    int poll_interval;

    int genl_sock;
    int rt_sock;
    int urandom_fd;

    struct {
        uint16_t family_id;
        uint32_t get_interface_seq_nr;
        uint32_t get_station_seq_nr;
        uint32_t get_scan_seq_nr;
    } nl80211;

    bool get_addresses;

    int ifindex;
    uint8_t mac[6];
    bool carrier;
    uint8_t state;  /* IFLA_OPERSTATE */

    /* IPv4 and IPv6 addresses */
    tll(struct af_addr) addrs;

    /* WiFi extensions */
    char *ssid;
    int signal_strength_dbm;
    uint32_t rx_bitrate;
    uint32_t tx_bitrate;

    uint64_t ul_speed;
    uint64_t ul_bits;

    uint64_t dl_speed;
    uint64_t dl_bits;
};

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;

    assert(m->rt_sock == -1);

    m->label->destroy(m->label);

    if (m->urandom_fd >= 0)
        close(m->urandom_fd);

    tll_free(m->addrs);
    free(m->ssid);
    free(m->iface);
    free(m);

    module_default_destroy(mod);
}

static const char *
description(const struct module *mod)
{
    static char desc[32];
    const struct private *m = mod->private;

    snprintf(desc, sizeof(desc), "net(%s)", m->iface);
    return desc;
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
            tag_new_string(mod, "ssid", m->ssid),
            tag_new_int(mod, "signal", m->signal_strength_dbm),
            tag_new_int(mod, "rx-bitrate", m->rx_bitrate),
            tag_new_int(mod, "tx-bitrate", m->tx_bitrate),
            tag_new_float(mod, "dl-speed", m->dl_speed),
            tag_new_float(mod, "ul-speed", m->ul_speed),
        },
        .count = 13,
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
    return (pid_t)(uintptr_t)thrd_current() ^ getpid();
}

/* Connect and bind to netlink socket. Returns socket fd, or -1 on error */
static int
netlink_connect_rt(void)
{
    int sock = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (sock == -1) {
        LOG_ERRNO("failed to create netlink socket");
        return -1;
    }

    const struct sockaddr_nl addr = {
        .nl_family = AF_NETLINK,
        .nl_pid = nl_pid_value(),
        .nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR,
    };

    if (bind(sock, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERRNO("failed to bind netlink RT socket");
        close(sock);
        return -1;
    }

    return sock;
}

static int
netlink_connect_genl(void)
{
    int sock = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_GENERIC);
    if (sock == -1) {
        LOG_ERRNO("failed to create netlink socket");
        return -1;
    }

    const struct sockaddr_nl addr = {
        .nl_family = AF_NETLINK,
        .nl_pid = nl_pid_value(),
        /* no multicast notifications by default, will be added later */
    };

    if (bind(sock, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERRNO("failed to bind netlink socket");
        close(sock);
        return -1;
    }

    return sock;
}

static bool
send_nlmsg(int sock, const void *nlmsg, size_t len)
{
    int r = sendto(
        sock, nlmsg, len, 0,
        (struct sockaddr *)&(struct sockaddr_nl){.nl_family = AF_NETLINK},
        sizeof(struct sockaddr_nl));

    return r == len;
}

static bool
send_rt_request(struct private *m, int request)
{
    struct {
        struct nlmsghdr hdr;
        struct rtgenmsg rt __attribute__((aligned(NLMSG_ALIGNTO)));
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

    if (!send_nlmsg(m->rt_sock, &req, req.hdr.nlmsg_len)) {
        LOG_ERRNO("%s: failed to send netlink RT request (%d)",
                  m->iface, request);
        return false;
    }

    return true;
}

static bool
send_rt_getstats_request(struct private *m)
{
    struct {
        struct nlmsghdr hdr;
        struct if_stats_msg rt;
    } req = {
        .hdr = {
            .nlmsg_len = NLMSG_LENGTH(sizeof(req.rt)),
            .nlmsg_type = RTM_GETSTATS,
            .nlmsg_flags = NLM_F_REQUEST,
            .nlmsg_seq = 1,
            .nlmsg_pid = nl_pid_value(),
        },

        .rt = {
            .ifindex = m->ifindex,
            .filter_mask = IFLA_STATS_LINK_64,
            .family = AF_UNSPEC,
        },
    };

    if (!send_nlmsg(m->rt_sock, &req, req.hdr.nlmsg_len)) {
        LOG_ERRNO("%s: failed to send netlink RT getstats request (%d)",
                m->iface, RTM_GETSTATS);
        return false;
    }
    return true;
}

static bool
send_ctrl_get_family_request(struct private *m)
{
    const struct {
        struct nlmsghdr hdr;
        struct {
            struct genlmsghdr genl;
            struct {
                struct nlattr hdr;
                char data[8] __attribute__((aligned(NLA_ALIGNTO)));
            } family_name_attr __attribute__((aligned(NLA_ALIGNTO)));
        } msg __attribute__((aligned(NLMSG_ALIGNTO)));
    } req = {
        .hdr = {
            .nlmsg_len = NLMSG_LENGTH(sizeof(req.msg)),
            .nlmsg_type = GENL_ID_CTRL,
            .nlmsg_flags = NLM_F_REQUEST,
            .nlmsg_seq = 1,
            .nlmsg_pid = nl_pid_value(),
        },

        .msg = {
            .genl = {
                .cmd = CTRL_CMD_GETFAMILY,
                .version = 1,
            },

            .family_name_attr = {
                .hdr = {
                    .nla_type = CTRL_ATTR_FAMILY_NAME,
                    .nla_len = sizeof(req.msg.family_name_attr),
                },

                .data = NL80211_GENL_NAME,
            },
        },
    };

    _Static_assert(
        sizeof(req.msg.family_name_attr) ==
        NLA_HDRLEN + NLA_ALIGN(sizeof(req.msg.family_name_attr.data)),
        "");

    if (!send_nlmsg(m->genl_sock, &req, req.hdr.nlmsg_len)) {
        LOG_ERRNO("%s: failed to send netlink ctrl-get-family request",
                  m->iface);
        return false;
    }

    return true;
}

static bool
send_nl80211_request(struct private *m, uint8_t cmd, uint16_t flags, uint32_t seq)
{
    if (m->ifindex < 0)
        return false;

    if (m->nl80211.family_id == (uint16_t)-1)
        return false;

    const struct {
        struct nlmsghdr hdr;
        struct {
            struct genlmsghdr genl;
            struct {
                struct nlattr attr;
                int index __attribute__((aligned(NLA_ALIGNTO)));
            } ifindex __attribute__((aligned(NLA_ALIGNTO)));
        } msg __attribute__((aligned(NLMSG_ALIGNTO)));
    } req = {
        .hdr = {
            .nlmsg_len = NLMSG_LENGTH(sizeof(req.msg)),
            .nlmsg_type = m->nl80211.family_id,
            .nlmsg_flags = flags,
            .nlmsg_seq = seq,
            .nlmsg_pid = nl_pid_value(),
        },

        .msg = {
            .genl = {
                .cmd = cmd,
                .version = 1,
            },

            .ifindex = {
                .attr = {
                    .nla_type = NL80211_ATTR_IFINDEX,
                    .nla_len = sizeof(req.msg.ifindex),
                },

                .index = m->ifindex,
            },
        },
    };

    if (!send_nlmsg(m->genl_sock, &req, req.hdr.nlmsg_len)) {
        LOG_ERRNO("%s: failed to send netlink nl80211 get-inteface request",
                  m->iface);
        return false;
    }

    return true;
}

static bool
send_nl80211_get_interface(struct private *m)
{
    if (m->nl80211.get_interface_seq_nr > 0) {
        LOG_DBG(
            "%s: nl80211 get-interface request already in progress", m->iface);
        return true;
    }

    LOG_DBG("%s: sending nl80211 get-interface request", m->iface);

    uint32_t seq;
    if (read(m->urandom_fd, &seq, sizeof(seq)) != sizeof(seq)) {
        LOG_ERRNO("failed to read from /dev/urandom");
        return false;
    }

    if (send_nl80211_request(m, NL80211_CMD_GET_INTERFACE, NLM_F_REQUEST, seq)) {
        m->nl80211.get_interface_seq_nr = seq;
        return true;
    } else
        return false;
}

static bool
send_nl80211_get_station(struct private *m)
{
    if (m->nl80211.get_station_seq_nr > 0) {
        LOG_DBG(
            "%s: nl80211 get-station request already in progress", m->iface);
        return true;
    }

    LOG_DBG("%s: sending nl80211 get-station request", m->iface);

    uint32_t seq;
    if (read(m->urandom_fd, &seq, sizeof(seq)) != sizeof(seq)) {
        LOG_ERRNO("failed to read from /dev/urandom");
        return false;
    }

    if (send_nl80211_request(
            m, NL80211_CMD_GET_STATION, NLM_F_REQUEST | NLM_F_DUMP, seq))
    {
        m->nl80211.get_station_seq_nr = seq;
        return true;
    } else
        return false;
}

static bool
send_nl80211_get_scan(struct private *m)
{
    if (m->nl80211.get_scan_seq_nr > 0) {
        LOG_ERR(
            "%s: nl80211 get-scan request already in progress", m->iface);
        return true;
    }

    LOG_DBG("%s: sending nl80211 get-scan request", m->iface);

    uint32_t seq;
    if (read(m->urandom_fd, &seq, sizeof(seq)) != sizeof(seq)) {
        LOG_ERRNO("failed to read from /dev/urandom");
        return false;
    }

    if (send_nl80211_request(
            m, NL80211_CMD_GET_SCAN, NLM_F_REQUEST | NLM_F_DUMP, seq))
    {
        m->nl80211.get_scan_seq_nr = seq;
        return true;
    } else
        return false;
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

                send_nl80211_get_interface(m);
                send_nl80211_get_station(m);
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

static bool
foreach_nlattr(struct module *mod, const struct genlmsghdr *genl, size_t len,
               bool (*cb)(struct module *mod, uint16_t type, bool nested,
                          const void *payload, size_t len))
{
    const uint8_t *raw = (const uint8_t *)genl + GENL_HDRLEN;
    const uint8_t *end = (const uint8_t *)genl + len;

    for (const struct nlattr *attr = (const struct nlattr *)raw;
         raw < end;
         raw += NLA_ALIGN(attr->nla_len), attr = (const struct nlattr *)raw)
    {
        uint16_t type = attr->nla_type & NLA_TYPE_MASK;
        bool nested = (attr->nla_type & NLA_F_NESTED) != 0;;
        const void *payload = raw + NLA_HDRLEN;

        if (!cb(mod, type, nested, payload, attr->nla_len - NLA_HDRLEN))
            return false;
    }

    return true;
}

static bool
foreach_nlattr_nested(struct module *mod, const void *parent_payload, size_t len,
                      bool (*cb)(struct module *mod, uint16_t type,
                                 bool nested, const void *payload, size_t len,
                                 void *ctx),
                      void *ctx)
{
    const uint8_t *raw = parent_payload;
    const uint8_t *end = parent_payload + len;

    for (const struct nlattr *attr = (const struct nlattr *)raw;
         raw < end;
         raw += NLA_ALIGN(attr->nla_len), attr = (const struct nlattr *)raw)
    {
        uint16_t type = attr->nla_type & NLA_TYPE_MASK;
        bool nested = (attr->nla_type & NLA_F_NESTED) != 0;
        const void *payload = raw + NLA_HDRLEN;

        if (!cb(mod, type, nested, payload, attr->nla_len - NLA_HDRLEN, ctx))
            return false;
    }

    return true;
}

struct mcast_group {
    uint32_t id;
    const char *name;
};

static bool
parse_mcast_group(struct module *mod, uint16_t type, bool nested,
                  const void *payload, size_t len, void *_ctx)
{
    struct private *m = mod->private;
    struct mcast_group *ctx = _ctx;

    switch (type) {
    case CTRL_ATTR_MCAST_GRP_ID: {
        ctx->id = *(uint32_t *)payload;
        break;
    }

    case CTRL_ATTR_MCAST_GRP_NAME: {
        ctx->name = (const char *)payload;
        break;
    }

    default:
        LOG_WARN("%s: unrecognized GENL MCAST GRP attribute: "
                 "type=%hu, nested=%d, len=%zu", m->iface, type, nested, len);
        break;
    }

    return true;
}

static bool
parse_mcast_groups(struct module *mod, uint16_t type, bool nested,
                   const void *payload, size_t len, void *_ctx)
{
    struct private *m = mod->private;

    struct mcast_group group = {0};
    foreach_nlattr_nested(mod, payload, len, &parse_mcast_group, &group);

    LOG_DBG("MCAST: %s -> %u", group.name, group.id);

    if (strcmp(group.name, NL80211_MULTICAST_GROUP_MLME) == 0) {
        /*
         * Join the nl80211 MLME multicast group - for
         * CONNECT/DISCONNECT events.
         */

        int r = setsockopt(
            m->genl_sock, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
            &group.id, sizeof(int));

        if (r < 0)
            LOG_ERRNO("failed to joint the nl80211 MLME mcast group");
    }

    return true;
}

static bool
handle_genl_ctrl(struct module *mod, uint16_t type, bool nested,
                 const void *payload, size_t len)
{
    struct private *m = mod->private;

    switch (type) {
    case CTRL_ATTR_FAMILY_ID: {
        m->nl80211.family_id = *(const uint16_t *)payload;
        send_nl80211_get_interface(m);
        send_nl80211_get_station(m);
        break;
    }

    case CTRL_ATTR_FAMILY_NAME:
        //LOG_INFO("NAME: %.*s (%zu bytes)", (int)len, (const char *)payload, len);
        break;

    case CTRL_ATTR_MCAST_GROUPS:
        foreach_nlattr_nested(mod, payload, len, &parse_mcast_groups, NULL);
        break;

    default:
        LOG_DBG("%s: unrecognized GENL CTRL attribute: "
                "type=%hu, nested=%d, len=%zu", m->iface, type, nested, len);
        break;
    }

    return true;
}

static bool
check_for_nl80211_ifindex(struct module *mod, uint16_t type, bool nested,
                          const void *payload, size_t len)
{
    struct private *m = mod->private;

    switch (type) {
    case NL80211_ATTR_IFINDEX:
        return *(uint32_t *)payload == m->ifindex;
    }

    return true;
}

static bool
nl80211_is_for_us(struct module *mod, const struct genlmsghdr *genl,
                   size_t msg_size)
{
    return foreach_nlattr(mod, genl, msg_size, &check_for_nl80211_ifindex);
}

static bool
handle_nl80211_new_interface(struct module *mod, uint16_t type, bool nested,
                             const void *payload, size_t len)
{
    struct private *m = mod->private;

    switch (type) {
    case NL80211_ATTR_IFINDEX:
        assert(*(uint32_t *)payload == m->ifindex);
        break;

    case NL80211_ATTR_SSID: {
        const char *ssid = payload;

        if (m->ssid == NULL || strncmp(m->ssid, ssid, len) != 0)
            LOG_INFO("%s: SSID: %.*s", m->iface, (int)len, ssid);

        mtx_lock(&mod->lock);
        free(m->ssid);
        m->ssid = strndup(ssid, len);
        mtx_unlock(&mod->lock);

        mod->bar->refresh(mod->bar);
        break;
    }

    default:
        LOG_DBG("%s: unrecognized nl80211 attribute: "
                "type=%hu, nested=%d, len=%zu", m->iface, type, nested, len);
        break;
    }

    return true;
}

struct rate_info_ctx {
    unsigned bitrate;
};

static bool
handle_nl80211_rate_info(struct module *mod, uint16_t type, bool nested,
                         const void *payload, size_t len, void *_ctx)
{
    struct private *m UNUSED = mod->private;
    struct rate_info_ctx *ctx = _ctx;

    switch (type) {
    case NL80211_RATE_INFO_BITRATE32: {
        uint32_t bitrate_100kbit = *(uint32_t *)payload;
        ctx->bitrate = bitrate_100kbit * 100 * 1000;
        break;
    }

    case NL80211_RATE_INFO_BITRATE:
        if (ctx->bitrate == 0) {
            uint16_t bitrate_100kbit = *(uint16_t *)payload;
            ctx->bitrate = bitrate_100kbit * 100 * 1000;
        } else {
            /* Prefer the BITRATE32 attribute */
        }
        break;

    default:
        LOG_DBG("%s: unrecognized nl80211 rate info attribute: "
                "type=%hu, nested=%d, len=%zu", m->iface, type, nested, len);
        break;
    }

    return true;
}

struct station_info_ctx {
    bool update_bar;
};

static bool
handle_nl80211_station_info(struct module *mod, uint16_t type, bool nested,
                            const void *payload, size_t len, void *_ctx)
{
    struct private *m = mod->private;
    struct station_info_ctx *ctx = _ctx;

    switch (type) {
    case NL80211_STA_INFO_SIGNAL:
        LOG_DBG("signal strength (last): %hhd dBm", *(uint8_t *)payload);
        break;

    case NL80211_STA_INFO_SIGNAL_AVG: {
        LOG_DBG("signal strength (average): %hhd dBm", *(uint8_t *)payload);
        mtx_lock(&mod->lock);
        m->signal_strength_dbm = *(int8_t *)payload;
        mtx_unlock(&mod->lock);
        ctx->update_bar = true;
        break;
    }

    case NL80211_STA_INFO_TX_BITRATE: {
        struct rate_info_ctx rctx = {0};
        foreach_nlattr_nested(
            mod, payload, len, &handle_nl80211_rate_info, &rctx);

        LOG_DBG("TX bitrate: %.1f Mbit/s", rctx.bitrate / 1000. / 1000.);
        mtx_lock(&mod->lock);
        m->tx_bitrate = rctx.bitrate;
        mtx_unlock(&mod->lock);
        ctx->update_bar = true;
        break;
    }

    case NL80211_STA_INFO_RX_BITRATE: {
        struct rate_info_ctx rctx = {0};
        foreach_nlattr_nested(
            mod, payload, len, &handle_nl80211_rate_info, &rctx);

        LOG_DBG("RX bitrate: %.1f Mbit/s", rctx.bitrate / 1000. / 1000.);
        mtx_lock(&mod->lock);
        m->rx_bitrate = rctx.bitrate;
        mtx_unlock(&mod->lock);
        ctx->update_bar = true;
        break;
    }

    default:
        LOG_DBG("%s: unrecognized nl80211 station info attribute: "
                "type=%hu, nested=%d, len=%zu", m->iface, type, nested, len);
        break;
    }

    return true;
}

static bool
handle_nl80211_new_station(struct module *mod, uint16_t type, bool nested,
                           const void *payload, size_t len)
{
    struct private *m UNUSED = mod->private;

    switch (type) {
    case NL80211_ATTR_STA_INFO: {
        struct station_info_ctx ctx = {0};
        foreach_nlattr_nested(
            mod, payload, len, &handle_nl80211_station_info, &ctx);

        if (ctx.update_bar)
            mod->bar->refresh(mod->bar);
        break;
    }

    default:
        LOG_DBG("%s: unrecognized nl80211 attribute: "
                "type=%hu, nested=%d, len=%zu", m->iface, type, nested, len);
        break;
    }

    return true;
}

static bool
handle_ies(struct module *mod, const void *_ies, size_t len)
{
    struct private *m = mod->private;
    const uint8_t *ies = _ies;

    while (len >= 2 && len - 2 >= ies[1]) {
        switch (ies[0]) {
        case 0: {  /* SSID */
            const char *ssid = (const char *)&ies[2];
            const size_t ssid_len = ies[1];

            if (m->ssid == NULL || strncmp(m->ssid, ssid, ssid_len) != 0)
                LOG_INFO("%s: SSID: %.*s", m->iface, (int)ssid_len, ssid);

            mtx_lock(&mod->lock);
            free(m->ssid);
            m->ssid = strndup(ssid, ssid_len);
            mtx_unlock(&mod->lock);

            mod->bar->refresh(mod->bar);
            break;
        }
        }
        len -= ies[1] + 2;
        ies += ies[1] + 2;
    }

    return true;
}

struct scan_results_context {
    bool associated;

    const void *ies;
    size_t ies_size;
};

static bool
handle_nl80211_bss(struct module *mod, uint16_t type, bool nested,
                   const void *payload, size_t len, void *_ctx)
{
    struct private *m UNUSED = mod->private;
    struct scan_results_context *ctx = _ctx;

    switch (type) {
    case NL80211_BSS_STATUS: {
        const uint32_t status = *(uint32_t *)payload;

        if (status == NL80211_BSS_STATUS_ASSOCIATED) {
            ctx->associated = true;

            if (ctx->ies != NULL) {
                /* Deferred handling of BSS_INFORMATION_ELEMENTS */
                return handle_ies(mod, ctx->ies, ctx->ies_size);
            }
        }
        break;
    }

    case NL80211_BSS_INFORMATION_ELEMENTS:
        if (ctx->associated)
            return handle_ies(mod, payload, len);
        else {
            /*
             * We’re either not associated, or, we haven’t seen the
             * BSS_STATUS attribute yet.
             *
             * Save a pointer to the IES payload, so that we can
             * process it later, if we see a
             *   BSS_STATUS == BSS_STATUS_ASSOCIATED.
             */
            ctx->ies = payload;
            ctx->ies_size = len;
        }
    }

    return true;
}

static bool
handle_nl80211_scan_results(struct module *mod, uint16_t type, bool nested,
                            const void *payload, size_t len)
{
    struct private *m UNUSED = mod->private;

    struct scan_results_context ctx = {0};

    switch (type) {
    case NL80211_ATTR_BSS:
        foreach_nlattr_nested(mod, payload, len, &handle_nl80211_bss, &ctx);
        break;
    }

    return true;
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

static void
handle_stats(struct module *mod, struct rt_stats_msg *msg)
{
    struct private *m = mod->private;
    uint64_t ul_bits = msg->stats.tx_bytes*8;
    uint64_t dl_bits = msg->stats.rx_bytes*8;

    if (m->ul_bits != 0) {
        m->ul_speed = (ul_bits - m->ul_bits) / m->poll_interval;
    }
    if (m->dl_bits != 0) {
        m->dl_speed = (dl_bits - m->dl_bits) / m->poll_interval;
    }
    m->ul_bits = ul_bits;
    m->dl_bits = dl_bits;
}

static bool
parse_rt_reply(struct module *mod, const struct nlmsghdr *hdr, size_t len)
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
                send_rt_request(m, RTM_GETADDR);
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
        case RTM_NEWSTATS: {
            struct rt_stats_msg *msg = NLMSG_DATA(hdr);
            handle_stats(mod, msg);
            break;
        }

        case NLMSG_ERROR:{
            const struct nlmsgerr *err = NLMSG_DATA(hdr);
            LOG_ERRNO_P(-err->error, "%s: netlink RT reply", m->iface);
            return false;
        }

        default:
            LOG_WARN(
                "%s: unrecognized netlink message type: 0x%x",
                m->iface, hdr->nlmsg_type);
            return false;
        }
    }

    return true;
}

static bool
parse_genl_reply(struct module *mod, const struct nlmsghdr *hdr, size_t len)
{
    struct private *m = mod->private;

    for (; NLMSG_OK(hdr, len); hdr = NLMSG_NEXT(hdr, len)) {
        if (hdr->nlmsg_seq == m->nl80211.get_interface_seq_nr) {
            /* Current request is now considered complete */
            m->nl80211.get_interface_seq_nr = 0;
        }

        if (hdr->nlmsg_type == NLMSG_DONE) {
            if (hdr->nlmsg_seq == m->nl80211.get_station_seq_nr) {
                /* Current request is now considered complete */
                m->nl80211.get_station_seq_nr = 0;
            }

            else if (hdr->nlmsg_seq == m->nl80211.get_scan_seq_nr) {
                /* Current request is now considered complete */
                m->nl80211.get_scan_seq_nr = 0;
            }
        }

        else if (hdr->nlmsg_type == GENL_ID_CTRL) {
            const struct genlmsghdr *genl = NLMSG_DATA(hdr);
            const size_t msg_size = NLMSG_PAYLOAD(hdr, 0);
            foreach_nlattr(mod, genl, msg_size, &handle_genl_ctrl);
        }

        else if (hdr->nlmsg_type == m->nl80211.family_id) {
            const struct genlmsghdr *genl = NLMSG_DATA(hdr);
            const size_t msg_size = NLMSG_PAYLOAD(hdr, 0);

            switch (genl->cmd) {
            case NL80211_CMD_NEW_INTERFACE:
                if (nl80211_is_for_us(mod, genl, msg_size)) {
                    LOG_DBG("%s: got interface information", m->iface);
                    foreach_nlattr(
                        mod, genl, msg_size, &handle_nl80211_new_interface);
                }
                break;

            case NL80211_CMD_CONNECT:
                /*
                 * Update SSID
                 *
                 * Unfortunately, the SSID doesn’t appear to be
                 * included in *any* of the notifications sent when
                 * associating, authenticating and connecting to a
                 * station.
                 *
                 * Thus, we need to explicitly request an update.
                 */
                if (nl80211_is_for_us(mod, genl, msg_size)) {
                    LOG_DBG("%s: connected, requesting interface information",
                            m->iface);
                    send_nl80211_get_interface(m);
                    send_nl80211_get_station(m);
                }
                break;

            case NL80211_CMD_DISCONNECT:
                if (nl80211_is_for_us(mod, genl, msg_size)) {
                    LOG_DBG("%s: disconnected, resetting SSID etc", m->iface);

                    mtx_lock(&mod->lock);
                    free(m->ssid);
                    m->ssid = NULL;
                    m->signal_strength_dbm = 0;
                    m->rx_bitrate = m->tx_bitrate = 0;
                    mtx_unlock(&mod->lock);
                }
                break;

            case NL80211_CMD_NEW_STATION:
                if (nl80211_is_for_us(mod, genl, msg_size)) {
                    LOG_DBG("%s: got station information", m->iface);
                    foreach_nlattr(mod, genl, msg_size, &handle_nl80211_new_station);
                }

                LOG_DBG("%s: signal: %d dBm, RX=%u Mbit/s, TX=%u Mbit/s",
                        m->iface, m->signal_strength_dbm,
                        m->rx_bitrate / 1000 / 1000,
                        m->tx_bitrate / 1000 / 1000);

                /* Can’t issue both get-station and get-scan at the
                 * same time. So, always run a get-scan when a
                 * get-station is complete */
                send_nl80211_get_scan(m);
                break;

            case NL80211_CMD_NEW_SCAN_RESULTS:
                if (nl80211_is_for_us(mod, genl, msg_size)) {
                    LOG_DBG("%s: got scan results", m->iface);
                    foreach_nlattr(mod, genl, msg_size, &handle_nl80211_scan_results);
                }
                break;

            default:
                LOG_DBG("unrecognized nl80211 command: %hhu", genl->cmd);
                break;
            }
        }

        else if (hdr->nlmsg_type == NLMSG_ERROR) {
            const struct nlmsgerr *err = NLMSG_DATA(hdr);
            int nl_errno = -err->error;

            if (nl_errno == ENODEV)
                ; /* iface is not an nl80211 device */
            else if (nl_errno == ENOENT)
                ; /* iface down? */
            else
                LOG_ERRNO_P(nl_errno, "%s: nl80211 reply (seq-nr: %u)",
                            m->iface, hdr->nlmsg_seq);
        }

        else {
            LOG_WARN(
                "%s: unrecognized netlink message type: 0x%x",
                m->iface, hdr->nlmsg_type);
            return false;
        }
    }

    return true;
}

static int
run(struct module *mod)
{
    int ret = 1;
    struct private *m = mod->private;

    int timer_fd = -1;
    if (m->poll_interval > 0) {
        timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
        if (timer_fd < 0) {
            LOG_ERRNO("%s: failed to create poll timer FD", m->iface);
            goto out;
        }

        struct itimerspec poll_time = {
            .it_value = {.tv_sec = m->poll_interval},
            .it_interval = {.tv_sec = m->poll_interval},
        };

        if (timerfd_settime(timer_fd, 0, &poll_time, NULL) < 0) {
            LOG_ERRNO("%s: failed to arm poll timer", m->iface);
            goto out;
        }
    }

    m->rt_sock = netlink_connect_rt();
    m->genl_sock = netlink_connect_genl();

    if (m->rt_sock < 0 || m->genl_sock < 0)
        goto out;

    if (!send_rt_request(m, RTM_GETLINK) ||
        !send_ctrl_get_family_request(m))
    {
        goto out;
    }

    /* Main loop */
    while (true) {
        struct pollfd fds[] = {
            {.fd = mod->abort_fd, .events = POLLIN},
            {.fd = m->rt_sock, .events = POLLIN},
            {.fd = m->genl_sock, .events = POLLIN},
            {.fd = timer_fd, .events = POLLIN},
        };

        poll(fds, 3 + (timer_fd >= 0 ? 1 : 0), -1);

        if (fds[0].revents & (POLLIN | POLLHUP))
            break;

        if ((fds[1].revents & POLLHUP) ||
            (fds[2].revents & POLLHUP))
        {
            LOG_ERR("%s: disconnected from netlink socket", m->iface);
            break;
        }

        if (fds[3].revents & POLLHUP) {
            LOG_ERR("%s: disconnected from timer FD", m->iface);
            break;
        }

        if (fds[1].revents & POLLIN) {
            /* Read one (or more) messages */
            void *reply;
            size_t len;
            if (!netlink_receive_messages(m->rt_sock, &reply, &len))
                break;

            /* Parse (and act upon) the received message(s) */
            if (!parse_rt_reply(mod, (const struct nlmsghdr *)reply, len)) {
                free(reply);
                break;
            }

            free(reply);
        }

        if (fds[2].revents & POLLIN) {
            /* Read one (or more) messages */
            void *reply;
            size_t len;
            if (!netlink_receive_messages(m->genl_sock, &reply, &len))
                break;

            if (!parse_genl_reply(mod, (const struct nlmsghdr *)reply, len)) {
                free(reply);
                break;
            }

            free(reply);
        }

        if (fds[3].revents & POLLIN) {
            uint64_t count;
            ssize_t amount = read(timer_fd, &count, sizeof(count));
            if (amount < 0) {
                LOG_ERRNO("failed to read from timer FD");
                break;
            }

            send_nl80211_get_station(m);
            send_rt_getstats_request(m);
        }
    }

    ret = 0;

    out:
    if (m->rt_sock >= 0)
        close(m->rt_sock);
    if (m->genl_sock >= 0)
        close(m->genl_sock);
    if (timer_fd >= 0)
        close(timer_fd);
    m->rt_sock = m->genl_sock = -1;
    return ret;
}

static struct module *
network_new(const char *iface, struct particle *label, int poll_interval)
{
    int urandom_fd = open("/dev/urandom", O_RDONLY);
    if (urandom_fd < 0) {
        LOG_ERRNO("failed to open /dev/urandom");
        return NULL;
    }

    struct private *priv = calloc(1, sizeof(*priv));
    priv->iface = strdup(iface);
    priv->label = label;
    priv->poll_interval = poll_interval;

    priv->genl_sock = -1;
    priv->rt_sock = -1;
    priv->urandom_fd = urandom_fd;
    priv->nl80211.family_id = -1;
    priv->get_addresses = true;
    priv->ifindex = -1;
    priv->state = IF_OPER_DOWN;

    struct module *mod = module_common_new();
    mod->private = priv;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    mod->description = &description;
    return mod;
}

static struct module *
from_conf(const struct yml_node *node, struct conf_inherit inherited)
{
    const struct yml_node *name = yml_get_value(node, "name");
    const struct yml_node *content = yml_get_value(node, "content");
    const struct yml_node *poll = yml_get_value(node, "poll-interval");

    return network_new(
        yml_value_as_string(name), conf_to_particle(content, inherited),
        poll != NULL ? yml_value_as_int(poll) : 0);
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"name", true, &conf_verify_string},
        {"poll-interval", false, &conf_verify_unsigned},
        MODULE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct module_iface module_network_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct module_iface iface __attribute__((weak, alias("module_network_iface")));
#endif
