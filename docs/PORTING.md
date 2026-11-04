# 逐项适配对照表

状态只有三种：**已做**（写完、编过、能自检）、**待验证**（代码在，没设备跑过）、
**未做**（刻意先不做，列了后续方案）。

| # | Magisk/KernelSU 版的依赖 | SKRoot Pro 版怎么处理 | 状态 |
| --- | --- | --- | --- |
| 1 | `post-fs-data.sh`（早期属性 / 权限 / 早启动） | 拆成 `OmmegaCore/boot.sh`，由模块壳 `service.sh --boot` 同步调用 | 已做（脚本逻辑不变） |
| 2 | `service.sh`（late_start 起守护） | 同一份脚本 `--late` 分支，模块壳 fork 到后台 | 已做 |
| 3 | 模块目录 `/data/adb/modules/ommega` | `${0%/*}` 推导，实际是 `/data/<key16>/modules/<id32>/OmmegaCore` | 已做 |
| 4 | 管理器托管的 WebUI + `window.ksu` 桥 | 模块自带 WebUI + `ksu-shim.js` → `/api/exec` | 已做（真机 WebView 未验） |
| 5 | 面板里写死的模块路径 | 模块壳注入 `window.OMMEGA_MODDIR`，页面用它 | 已做 |
| 6 | 面板读 `module.prop` 拿版本 | 构建时生成 `OmmegaCore/module.prop` | 已做 |
| 7 | `resetprop`（模块自带 resetprop-rs） | 先扫 `/data/*/resetprop`（SKRoot 自带），再用自带的 | 已做（SKRoot 那份的用法未验） |
| 8 | `ksud sepolicy apply` / `magiskpolicy` | `sepolicy_apply.sh` 原样保留，落回 `ommega-policy`；都失败只记日志 | 待验证（`ommega-policy` 在 shell 域能不能 load policy） |
| 9 | `sepolicy.rule` 由管理器开机重放 | 无自动重放，靠 #8 + keep-alive 里的 `ensure_sepolicy` | 待验证 |
| 10 | `insmod pathmask*.ko` 隐藏 SoterService | `kmod-loader.sh` 原样保留，失败写 `conf/pathmask.state` | 待验证（SKRoot 下 insmod 未测） |
| 11 | `mount --bind` 改权限 XML（hide_strongbox / hide_device_ids） | `boot.sh` 原逻辑保留，失败只记日志 | 待验证（shell 域 mount 权限） |
| 12 | `chcon`（给 payload/二进制打 system_file 标签） | 脚本里都是 `chcon ... || true`，失败不阻塞 | 待验证 |
| 13 | ptrace 注入 keystore2（`ommega-inject`） | 二进制不变；SELinux 关按源码看已经过了（root-like 全放行），剩下是 dumpable/LSM | **待验证（只剩内核非 SELinux 的门）** |
| 14 | 早启动抢 keystore2 的解锁窗口 | `--boot` 同步跑，但模块加载时机在 zygote 前后取决于机型 | 待验证 |
| 15 | `am start-service` 拉 SOTER 宿主 | 原逻辑保留（root 下 `am` 可用） | 待验证 |
| 16 | adbr 公网调试通道 | `adbr-daemon.sh` + `libs/arm64-v8a/adbrd` 原样带 | 待验证 |
| 17 | 安装时 `customize.sh`（迁移旧数据、建软链） | SKRoot 没有安装脚本；`layout.sh` 每次启动补软链/权限 | 已做（迁移逻辑没搬） |
| 18 | 卸载时 `uninstall.sh` | 没有卸载回调，状态目录保留；文档里写清楚手动清 | 未做（刻意） |
| 19 | `module.prop` 显示运行状态 | `service.sh` 的 `update_status` 仍写 `OmmegaCore/module.prop`，SKRoot 管理器不显示 | 已做（仅面板可见） |
| 20 | 内核级隐藏（PathMask 的等价物） | 还没写；官方 `module_hide_data_dir` 有 filldir64 hook 的现成实现 | 未做（见下） |

## 未做项的具体方案

### 20. 用内核 hook 替代 PathMask

官方 Pro 示例里 `module_hide_data_dir` 做了 `filldir64` / `iterate_dir` /
`compat_filldir` / MTK `hbt_filldir64` 四个 hook，按 inode+目录项名字过滤。
可以照它把"隐藏 `/system/priv-app/SoterService`"做到模块壳里，好处是：

- 不再依赖 `insmod`（SKRoot 下未知）；
- 不留 `/proc/modules` 里的一条 `pathmask`；
- 和 A 端原来的 `pathmask_takeover` 冲突处理不再需要。

代价是要在内核态写过滤逻辑，且不同内核系列要选对 hook 点（官方示例已经处理了这一点）。
没有实机之前不落这段代码——内核 hook 写错了会直接影响开机。

### 13. ptrace 注入还剩什么门

先前担心的 SELinux 这条，看官方 patch 源码基本已经不用担心：

- `patch_pro_selinux.cpp` / `patch_avc_denied.cpp`：把 `avc_denied()` 里每个 `ret`
  换成跳到代码洞，洞里判定 root-like（`euid==0 && securebits==0 && 全 capability`），
  是就返回 0 放行；
- `root.c` 提权时把 cred 的 uid/gid/caps 拉到满。

所以模块壳 fork 出去的子进程只要继承了 uid 0 + 全 caps，对 keystore2 的
`process_vm_writev` / ptrace 就不会被 SELinux 拦。剩下要真机看的是：

1. keystore2 自己有没有 `PR_SET_DUMPABLE=0`（有的话 `ptrace_may_access` 直接失败）；
2. 厂商有没有额外 LSM/hook 拦跨进程读写（skroot-zygisk 在某 OPPO 上遇到过
   `/proc/<pid>/mem` 全 I/O error）；
3. 注入器本身在 SKRoot 的 `patch_do_execve` / seccomp 环境里行为是否变化。

如果这三条里有卡住的，再退到 SKRoot 自带的注入器（官方 TEESimulator / TrickyStore
移植示例走的就是 crash_dump 域那条）。
