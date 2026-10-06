# 在 PVE 宿主机上运行 oaf.ko,LXC 容器(ImmortalWrt)跑用户态 —— "歪路"部署指南

## 原理

OpenAppFilter 的核心是内核模块 `oaf.ko`(netfilter PRE_ROUTING/FORWARD hook + conntrack 标记 + DPI)。
LXC 容器共享宿主机内核,容器内加载不了 ImmortalWrt 内核的模块。

本移植做了两件事,让**一个** `oaf.ko` 同时服务宿主机和所有 LXC 容器:

1. **按网络命名空间(netns)注册**:hook、netlink socket、`/proc/net/*` 状态文件全部通过
   `register_pernet_subsys()` 在每个 netns 里各装一份。容器是有独立 netns 的,所以:
   - 容器内转发的流量(你的 LAN 设备 → ImmortalWrt 容器 → 外网)会经过**容器 netns 里**的 hook,被正常识别/拦截;
   - 容器里的 `oafd` 守护进程能直接看到自己的 `/proc/net/af_client` 等文件、netlink 也能对上号。
2. **netlink 自适应 netns**:内核记录守护进程(INIT 消息)所在的 netns,上报(每客户端访问记录)发给正确的 socket。

宿主机(OpenWrt/单 netns)场景行为与原版完全一致,原来的 OpenWrt 编译不受影响。

改动文件:`oaf/src/fwx_netns.{c,h}`(新增)、`fwx_main.c`、`fwx_client.c`、`fwx_client_fs.c`、
`fwx_conntrack.c`、`fwx_log.c`(sysctl 表终止符修复)、`oaf/src/Makefile`。

## 拓扑要求

ImmortalWrt 容器必须是**真正参与路由**的角色(LAN 客户端网关/旁路由),流量进容器 netns 后
才会被 hook 看到。宿主机自己的流量不会过滤(hook 在容器 netns 里匹配 `br-lan`,宿主机没有该接口)。

---

## 第一步:PVE 宿主机上编译

```bash
# 把整个源码目录传到 PVE 宿主机(或任意能装 PVE headers 的 Debian)
scp -r OpenAppFilter root@pve:/root/
ssh root@pve
cd /root/OpenAppFilter
bash pve/build.sh        # 自动装 pve-headers / proxmox-headers + build-essential,编译出 oaf/src/oaf.ko
```

支持 PVE 8(内核 6.8)与 PVE 9(内核 6.14)。若 `make` 报内核 API 错误,把报错贴出来再适配。

## 第二步:宿主机加载模块

```bash
bash pve/install.sh
```

脚本会:加载 `nf_conntrack`/`nf_reject_ipv4` → 安装 `oaf.ko` 到
`/lib/modules/$(uname -r)/extra/` → 写 `/etc/modules-load.d/oaf.conf`(开机自动加载)→
**打印 `/dev/fwx` 的主设备号(MAJOR)**,并给出要加到容器配置的三行。

验证:

```bash
dmesg | tail | grep -i fwx
cat /proc/sys/fwx/version     # 应显示 6.0.1
ls /proc/net/ | grep af_      # af_conn / af_active_app / af_active_host
```

## 第三步:配置并重启 LXC 容器

在 **PVE 宿主机** 上编辑 `/etc/pve/lxc/<vmid>.conf`(用上一步打印的 MAJOR 替换 511):

```
lxc.cgroup2.devices.allow: c 511:0 rwm
lxc.mount.entry: /dev/fwx dev/fwx none bind,optional,create=file 0 0
lxc.mount.entry: /proc/sys/fwx proc/sys/fwx none bind,optional,create=dir 0 0
```

三行的含义:
- 第 1 行:允许容器打开内核模块注册的字符设备(设备号是动态分配的);
- 第 2 行:把宿主机的 `/dev/fwx` 节点挂进容器 —— 容器里的 oafd 用它下发规则(JSON API);
- 第 3 行:把全局 sysctl 目录 `/proc/sys/fwx` 以可写方式挂进容器 ——
  oafd 启动时要写 `work_mode`、`lan_ifname`、`lan_ip` 等(容器默认 /proc/sys 只读,必须 bind)。

普通(非特权)容器也可以,不需要 mknod。之后**重启容器**(pernet 注册会在 netns 创建时自动装 hook,
模块先加载、容器后启动是最稳的顺序)。

## 第四步:容器(ImmortalWrt)内安装用户态

容器里只需要两个 ipk,**不要装内核模块包**:

```bash
opkg install open-app-filter*.ipk
opkg install luci-app-oaf*.ipk --force-depends   # 若它依赖 kmod-oaf 则忽略该依赖
```

- `open-app-filter` 提供 `oafd` 守护进程 + 特征库(`/etc/appfeature/`),正常走 opkg;
- 绝对不要安装 `kmod-oaf`/`oaf` 包(装了也只是 insmod 失败,无害但混淆判断)。

容器内确认:

```bash
sysctl net.netfilter.nf_conntrack_acct      # 必须 = 1,不是就: echo 1 > /proc/sys/net/netfilter/nf_conntrack_acct
ls /dev/fwx /proc/sys/fwx /proc/net/af_client   # 三个都应存在
/etc/init.d/appfilter restart
cat /proc/sys/fwx/feature_count             # > 0 说明特征库经 netlink 加载成功
```

> `nf_conntrack_acct=1` 是 DPI 统计的硬前提(内核按 conntrack 计数器判断流的新旧)。
> 可写进容器 `/etc/sysctl.conf` 持久化。

## 第五步:验证过滤

1. LuCI → 应用过滤(OpenAppFilter)页面能看到客户端列表、实时访问记录(数据来自容器内 proc 文件);
2. 给某台设备勾选一个应用(如抖音)启用拦截;
3. 该设备打开目标 App → 连接应被断开(`tcp_rst=1` 时会发 RST);
4. 宿主机侧复核:`cat /proc/net/af_conn | head` 能看到带 appid 的连接(诊断用)。

若 LuCI 面板空白:先看容器里 `/proc/sys/fwx/feature_count` 是否 > 0(>0 说明内核↔容器链路通了,
问题在 LuCI/ubus;=0 说明特征库没下发,查 `/dev/fwx` 是否可写:`echo test > /dev/fwx`)。

---

## 已知边界

- **每个 netns 各装一份 hook**:宿主机 netns 里 hook 也会注册,但 `gateway` 模式匹配 `br-lan`
  直接放行,宿主机流量不受影响;bypass 模式因 lan_ip=0 同样放行。
- **特权/非特权容器都可以**,靠 `lxc.mount.entry` 传设备与 sysctl,不需要 mknod 权限。
- 容器重启、宿主机重启后:模块由 `/etc/modules-load.d/oaf.conf` 自动加载;容器内 appfilter 由
  procd 自启。顺序颠倒(容器先起)也没关系,pernet 注册对已存在的 netns 同样生效。
- 卸载:`bash pve/uninstall.sh`。
- `nf_send_reset` 依赖宿主机 `nf_reject_ipv4`(PVE 内核自带);内核 ≥6.12 的 sysctl API
  变化已在代码里用版本宏处理。

## 排错速查

| 现象 | 检查 |
|---|---|
| `insmod: Invalid module format` | 模块和当前内核不匹配,重跑 `build.sh`(确认 `uname -r` 没变) |
| 容器内没有 `/proc/net/af_client` | 宿主机模块没加载,或容器是模块加载**之前**创建且未重启(重启容器即可) |
| feature_count = 0 | 容器内 `/dev/fwx` 不可写(cgroup allow 没配好)/ netlink 被占;`dmesg` 看 fwx 报错 |
| 面板有数据但拦不住 | 容器内 `sysctl net.netfilter.nf_conntrack_acct` 是否为 1;`fwx/work_mode` 是否 0(gateway) |
| 写 `/proc/sys/fwx/*` 报只读 | `lxc.mount.entry` 第 3 行没生效(先在宿主机 insmod,再重启容器) |
