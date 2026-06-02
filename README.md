# openwrt-traffic-monitor 🌐

![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)
![Platform](https://img.shields.io/badge/Platform-OpenWrt%20%7C%20Linux-lightgrey)

openwrt-traffic-monitor 是一款专为 Linux 和 OpenWrt 路由器设计的轻量级、高性能实时网络流量监控守护进程。它原生使用 `libpcap` 捕获网络数据包，可精确计算每个 IP 地址的 RX/TX 收发速率、滑动平均值及峰值，并将其优雅地展示在极具现代感的毛玻璃 (Glassmorphism) 风格大盘上。

## ✨ 核心特性

- **实时高精度**：直接基于 `libpcap` 构建，提供纳秒级的数据包捕获能力。
- **零锁竞争**：采用异步无锁的文件 I/O 架构，确保在千兆级高吞吐网络下绝不丢包。
- **原子级状态更新**：后端采用原子的 `rename()` 操作生成 JSON，确保前端轮询时永远读取到完整无损的数据。
- **内存安全**：零内存泄漏与严格的边界检查。内置 5 分钟超时 LRU 清理机制，自动剔除离线 IP，杜绝内存耗尽。
- **64 位精准统计**：全面采用 64 位整型，彻底免疫 32 位路由器环境下常见的 4GB 流量溢出 (清零) Bug。
- **底层防火墙接管**：集成了 C 语言高性能 HTTP API 服务器 (`firewall_api`)，完美支持 CORS 跨域请求与严格的安全校验。
- **原生 UCI 联动**：通过 Shell 脚本深度集成 OpenWrt 的 UCI 配置系统，实现动态添加和查询基于 IP/端口的防火墙规则。
- **现代化监控大盘**：响应式暗黑风 ECharts 大盘，融合毛玻璃 UI 元素、不仅支持双端流速图表，还提供直观的防火墙管控页面。

---

## 🚀 安装与构建

### 环境依赖
您需要安装 `libpcap` 开发头文件以及 C 编译器。

**对于 Debian/Ubuntu:**
```bash
sudo apt update
sudo apt install build-essential libpcap-dev
```

**对于 OpenWrt (交叉编译):**
若使用 OpenWrt SDK 进行编译，请确保在您的 `menuconfig` 中勾选了 `libpcap` 库。

### 编译
在项目根目录下直接执行 `make` 即可：

```bash
make
make install
```

执行完毕后，将在当前目录生成两个核心二进制可执行文件：`traffic_monitor`（流量分析守护进程）与 `firewall_api`（防火墙控制后端）。同时 `make install` 会将底层执行脚本 `firewall_cmd.sh` 自动部署到 `/www/cgi-bin/` 目录。

### 运行

由于监控程序需要从底层网卡抓取原始数据包，且防火墙 API 需要操作底层 UCI，因此**必须使用 `root` 权限**运行这两个服务：

```bash
# 启动流量监控守护进程 (后台运行)
sudo ./traffic_monitor &

# 启动防火墙 API 控制后端 (后台运行监听 8080 端口)
sudo ./firewall_api &
```

默认情况下，`traffic_monitor` 将监听 `br-lan` 接口，并将流量统计数据以 JSON 格式输出至 `/www/traffic.json`。如有需要，您可以在编译前修改 `src/main.c` 顶部的宏定义。

---

## 📊 大盘部署

监控大盘前端依赖一个轻量级的 Web 服务器（如 OpenWrt 上的 `uhttpd`，或 Linux 上的 `nginx` / `python -m http.server`）来托管静态页面。

1. 将 `web/index.html` 复制到您的 Web 服务器的根目录（例如 OpenWrt 默认的 `/www/`）。
2. 确保后端的 `traffic_monitor` 守护进程正在运行，并正在持续向 `/www/traffic.json` 写入数据。
3. 打开浏览器，访问 `http://<您的路由器IP>/index.html`。

尽情享受令人惊艳的实时数据流吧！

---

## 🛠 项目结构

```
openwrt-traffic-monitor/
├── src/
│   ├── main.c                  # 流量分析 C 语言守护进程核心源码
│   └── firewall/
│       └── firewall.c          # 防火墙 HTTP API C 语言后端源码
├── cgi-bin/
│   └── firewall_cmd.sh         # UCI 防火墙底层联动执行脚本
├── web/
│   └── index.html              # HTML5/ECharts 监控大盘前端 (含防火墙控制)
├── Makefile                    # 多模块编译与部署脚本
├── .gitignore                  # Git 忽略规则配置
├── LICENSE                     # MIT 开源协议
└── README.md                   # 项目说明文档
```

---

## 🤝 参与贡献

我们非常欢迎任何形式的贡献！如果您有新的功能建议（例如支持通过命令行参数动态指定网卡、引入配置文件等），欢迎随时提交 Issue 或 Pull Request。

## 📝 开源协议

本项目为开源软件，遵循 [MIT License](LICENSE) 协议发布。
