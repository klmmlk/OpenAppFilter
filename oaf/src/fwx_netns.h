// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * fwx_netns.h - per-netns install of fwx hooks/netlink/procfs
 *
 * Registering netfilter hooks, the netlink kernel socket and procfs
 * entries on every network namespace lets one module instance serve
 * the host (init_net) and LXC containers (own netns) at the same time.
 * On a single-netns system (OpenWrt) behaviour is unchanged.
 */
#ifndef _FWX_NETNS_H
#define _FWX_NETNS_H

#include <linux/list.h>
#include <net/net_namespace.h>

struct sock;

struct fwx_net_priv {
	struct list_head list;
	struct net *net;
	struct sock *nl_sock;
};

int fwx_netns_init(void);
void fwx_netns_exit(void);

/* remember the netns where the userspace daemon lives (called on netlink INIT) */
void fwx_daemon_net_set(struct net *net);
/* forget a netns that is going away (called from pernet exit) */
void fwx_daemon_net_clear(struct net *net);
/* kernel->user netlink socket of the daemon netns; caller must sock_put() */
struct sock *fwx_daemon_sock_get(void);

/* implemented in fwx_main.c */
struct sock *fwx_netlink_create_sock(struct net *net);
int fwx_hooks_register_net(struct net *net);
void fwx_hooks_unregister_net(struct net *net);

/* implemented in fwx_client.c */
int af_client_hooks_register_net(struct net *net);
void af_client_hooks_unregister_net(struct net *net);

/* implemented in fwx_client_fs.c */
int af_client_procfs_init_net(struct net *net);
void af_client_procfs_fini_net(struct net *net);
void af_client_proc_dirs_cleanup(void);

/* implemented in fwx_conntrack.c */
int af_conn_procfs_init_net(struct net *net);
void af_conn_procfs_fini_net(struct net *net);

/* implemented in fwx_main.c */
int af_active_app_procfs_init_net(struct net *net);
void af_active_app_procfs_fini_net(struct net *net);
int af_active_host_procfs_init_net(struct net *net);
void af_active_host_procfs_fini_net(struct net *net);

#endif
