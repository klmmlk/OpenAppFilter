
// SPDX-License-Identifier: GPL-2.0-or-later
/* 
 * Copyright(c) 2026 destan19(TT) <www.fanchmwrt.com>  
*/
#ifndef __AF_CLIENT_FS_H__
#define __AF_CLIENT_FS_H__

struct net;
int af_client_procfs_init_net(struct net *net);
void af_client_procfs_fini_net(struct net *net);
void af_client_proc_dirs_cleanup(void);
int create_client_proc_dir(af_client_info_t *client);
void remove_client_proc_dir(af_client_info_t *client);

#endif
