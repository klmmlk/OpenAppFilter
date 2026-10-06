# OpenAppFilter(增强版)

基于 [destan19/OpenAppFilter](https://github.com/destan19/OpenAppFilter) 的应用过滤/家长控制系统。本分支在原版基础上做了多项增强:**支持在 Proxmox VE 宿主机上运行内核模块、过滤 LXC 容器(软路由)内的流量**,并新增了设备实时连接查看和一键"学特征"能力。

上游特性全部保留:基于 DPI 的应用识别(HTTPS SNI / HTTP Host / DNS,不依赖明文 DNS)、按应用/设备/时段的上网管控、访问记录与仪表盘、在线特征库更新。

## 本分支的增强

### 1. 每网络命名空间(per-netns)过滤

原版内核模块只在 `init_net` 注册 netfilter hook,LXC 容器(共享宿主机内核、拥有独立 netns)里的流量完全不被感知。本分支通过 `register_pernet_subsys()` 把 hook、netlink socket 和 `/proc/net` 状态文件**安装进每个网络命名空间**:

- 在 **PVE 宿主机**加载一份 `oaf.ko`,即可过滤**所有 LXC 软路由容器**内转发的流量;
- 容器内的 `oafd` 守护进程与 LuCI 界面照常工作(读到的是自己 netns 的内核状态);
- 单 netns 的原生 OpenWrt 行为与原版完全一致。

```
LAN 设备 ──> [LXC 容器: ImmortalWrt] ──> WAN
              │  oafd + LuCI(用户态)      ▲
              │  规则/记录/界面            │ netfilter hook(容器 netns)
              └──── /dev/fwx + /proc/sys/fwx ──> oaf.ko(跑在 PVE 宿主机内核)
```

### 2. 设备实时连接查看 + 一键学为自定义特征

「特征库」页新增「设备连接」标签页:

- 实时(3 秒)查看任意设备的活跃连接:协议、目标地址:端口、识别出的应用、**Host/SNI 域名**(内核直接标注在连接上)、放行/拦截状态;
- 单条或多选批量把连接**学为自定义特征**:有域名/SNI 的连接学成域名特征(同时匹配 SNI/Host/DNS),纯 IP 流量学成端口特征;
- 学成的特征可**追加到已有自定义应用**(每个应用最多 16 条),自动出现在「自定义应用」和应用过滤规则的选择器里。

### 3. 网关模式连接表

`af_conn` 实时连接表原来只有旁路模式填充,现在网关模式同样填充,并在连接条目上直接记录 DPI 解析出的 SNI/HTTP Host/DNS 域名。

### 4. 其他修复

- 主机名解析(`update_client_hostname`)跨行缓冲区污染导致设备名"串号"的问题;
- 内核 ≥6.12 sysctl 表缺失终止符的编译问题;
- argon 主题下复选框无勾选视觉、表格斑马纹在暗色模式下刺眼等界面问题;
- 暗色模式适配。

## 部署

### 方式 A:PVE 宿主机 + LXC 容器(本分支的主要场景)

前提:PVE 8/9(Debian 内核),ImmortalWrt/OpenWrt LXC 容器作为软路由。

```bash
# PVE 宿主机上
git clone https://github.com/klmmlk/OpenAppFilter.git && cd OpenAppFilter
bash pve/build.sh      # 自动安装内核头文件并编译 oaf.ko
bash pve/install.sh    # 加载模块 + 开机自启,打印 /dev/fwx 设备号
# 按输出把 3 行 lxc.* 配置加入 /etc/pve/lxc/<vmid>.conf,然后重启容器
```

容器内安装用户态包(见下方 CI 产物):`appfilter_*.ipk`、`luci-app-oaf_*.ipk`、`luci-i18n-oaf-zh-cn_*.ipk`(不要安装 kmod 包)。

**注意:每次重载宿主机内核模块后,需要重启一次 LXC 容器**——模块卸载会销毁宿主机的 `/proc/sys/fwx` 目录,容器的 bind 挂载会断链失效。

完整步骤与排错见 [pve/README-PVE.md](pve/README-PVE.md)。

### 方式 B:原生 OpenWrt / ImmortalWrt(与上游相同)

```bash
# 在已编译成功的 OpenWrt 源码根目录
git clone https://github.com/klmmlk/OpenAppFilter.git package/OpenAppFilter
make menuconfig   # 选中 luci-app-oaf(自动带上三个包)
make package/luci-app-oaf/compile package/open-app-filter/compile package/oaf/compile V=s
```

## GitHub Actions 自动构建

推送到 `master` 会自动触发 [Actions 工作流](.github/workflows/build.yml):用 ImmortalWrt 24.10.1 x86_64 SDK 编译三个用户态 ipk,并发布到 rolling release [`container-latest`](https://github.com/klmmlk/OpenAppFilter/releases/tag/container-latest):

- `appfilter_7.0.1-r1_x86_64.ipk` — 守护进程 oafd + rule_manager
- `luci-app-oaf_7.0-r1_all.ipk` — LuCI 界面
- `luci-i18n-oaf-zh-cn_0_all.ipk` — 中文语言包

内核模块需要针对具体内核编译(OpenWrt 内核用 SDK,PVE 内核用 `pve/build.sh`),不随 CI 分发。

## 目录结构

```
├── oaf/                # 内核模块 oaf.ko(netns 适配层 fwx_netns.c 为本分支新增)
├── open-app-filter/    # 用户态守护进程 oafd、rule_manager、特征库文件
├── luci-app-oaf/       # LuCI 界面(仪表盘/过滤规则/访问控制/特征库/设备连接)
├── pve/                # PVE 宿主机构建/安装脚本 + 部署指南
└── .github/workflows/  # CI:ImmortalWrt SDK 编译 ipk 并发布 release
```

## 许可与致谢

- 基于 [destan19/OpenAppFilter](https://github.com/destan19/OpenAppFilter),遵循 GPL-2.0 协议开源;
- 个人可免费使用与二次开发,衍生作品需保留原项目仓库/网站信息;企业使用请联系原作者授权;
- 详情见上游官网 [www.openappfilter.com](http://www.openappfilter.com)。
