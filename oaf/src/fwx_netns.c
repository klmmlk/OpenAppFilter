// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * fwx_netns.c - per-netns install of fwx hooks/netlink/procfs
 *
 * A register_pernet_subsys() callback installs the netfilter hooks, the
 * netlink kernel socket and the /proc/net status files in every network
 * namespace, so a module loaded on the PVE host also filters traffic that
 * is routed inside LXC container netns (the ImmortalWrt router case).
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/netfilter.h>
#include <linux/netlink.h>
#include <net/sock.h>
#include <net/net_namespace.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include "fwx.h"
#include "fwx_log.h"
#include "fwx_netns.h"

static LIST_HEAD(fwx_net_list);
static DEFINE_SPINLOCK(fwx_net_lock);

/* netns + netlink kernel socket where the userspace daemon (oafd) lives */
static struct net *g_daemon_net;
static struct sock *g_daemon_sock;

void fwx_daemon_net_set(struct net *net)
{
	struct net *old_net = NULL;
	struct sock *old_sk = NULL;
	struct sock *sk = NULL;
	struct fwx_net_priv *node;

	spin_lock_bh(&fwx_net_lock);
	list_for_each_entry(node, &fwx_net_list, list) {
		if (node->net == net && node->nl_sock) {
			sk = node->nl_sock;
			break;
		}
	}
	old_net = g_daemon_net;
	old_sk = g_daemon_sock;
	g_daemon_net = NULL;
	g_daemon_sock = NULL;
	if (net && sk && maybe_get_net(net)) {
		g_daemon_net = net;
		g_daemon_sock = sk;
		sock_hold(sk);
	}
	spin_unlock_bh(&fwx_net_lock);

	if (old_sk)
		sock_put(old_sk);
	if (old_net)
		put_net(old_net);
}

void fwx_daemon_net_clear(struct net *net)
{
	struct net *old_net = NULL;
	struct sock *old_sk = NULL;

	spin_lock_bh(&fwx_net_lock);
	if (g_daemon_net == net) {
		old_net = g_daemon_net;
		old_sk = g_daemon_sock;
		g_daemon_net = NULL;
		g_daemon_sock = NULL;
	}
	spin_unlock_bh(&fwx_net_lock);

	if (old_sk)
		sock_put(old_sk);
	if (old_net)
		put_net(old_net);
}

struct sock *fwx_daemon_sock_get(void)
{
	struct sock *sk = NULL;

	spin_lock_bh(&fwx_net_lock);
	if (g_daemon_sock) {
		sk = g_daemon_sock;
		sock_hold(sk);
	}
	spin_unlock_bh(&fwx_net_lock);
	return sk;
}

static int fwx_pernet_init(struct net *net)
{
	struct fwx_net_priv *priv;
	int err;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->net = net;
	INIT_LIST_HEAD(&priv->list);

	err = af_conn_procfs_init_net(net);
	if (err)
		goto conn_fail;
	err = af_client_procfs_init_net(net);
	if (err)
		goto client_fail;
	err = af_active_app_procfs_init_net(net);
	if (err)
		goto active_app_fail;
	err = af_active_host_procfs_init_net(net);
	if (err)
		goto active_host_fail;

	priv->nl_sock = fwx_netlink_create_sock(net);
	if (!priv->nl_sock) {
		pr_err("fwx: netlink create failed for netns %p\n", net);
		err = -ENOMEM;
		goto netlink_fail;
	}

	err = fwx_hooks_register_net(net);
	if (err)
		goto hooks_fail;
	err = af_client_hooks_register_net(net);
	if (err)
		goto client_hooks_fail;

	spin_lock_bh(&fwx_net_lock);
	list_add_tail(&priv->list, &fwx_net_list);
	spin_unlock_bh(&fwx_net_lock);

	AF_INFO("fwx: netns %p installed\n", net);
	return 0;

client_hooks_fail:
	fwx_hooks_unregister_net(net);
hooks_fail:
	netlink_kernel_release(priv->nl_sock);
	priv->nl_sock = NULL;
netlink_fail:
	af_active_host_procfs_fini_net(net);
active_host_fail:
	af_active_app_procfs_fini_net(net);
active_app_fail:
	af_client_procfs_fini_net(net);
client_fail:
	af_conn_procfs_fini_net(net);
conn_fail:
	kfree(priv);
	return err;
}

static void fwx_pernet_exit(struct net *net)
{
	struct fwx_net_priv *priv;

	spin_lock_bh(&fwx_net_lock);
	list_for_each_entry(priv, &fwx_net_list, list) {
		if (priv->net == net)
			break;
	}
	if (&priv->list == &fwx_net_list)
		priv = NULL;
	else
		list_del(&priv->list);
	spin_unlock_bh(&fwx_net_lock);

	if (!priv)
		return;

	af_client_hooks_unregister_net(net);
	fwx_hooks_unregister_net(net);
	fwx_daemon_net_clear(net);
	if (priv->nl_sock)
		netlink_kernel_release(priv->nl_sock);
	af_active_host_procfs_fini_net(net);
	af_active_app_procfs_fini_net(net);
	af_client_procfs_fini_net(net);
	af_conn_procfs_fini_net(net);
	kfree(priv);
	AF_INFO("fwx: netns %p removed\n", net);
}

static struct pernet_operations fwx_pernet_ops = {
	.init = fwx_pernet_init,
	.exit = fwx_pernet_exit,
};

int fwx_netns_init(void)
{
	return register_pernet_subsys(&fwx_pernet_ops);
}

void fwx_netns_exit(void)
{
	unregister_pernet_subsys(&fwx_pernet_ops);
}
