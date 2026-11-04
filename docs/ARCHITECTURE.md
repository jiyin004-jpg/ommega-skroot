# 架构：SKRoot Pro 版 Ommega A 端

## 1. 加载链

```
init
 └─ SKRoot 提权内核（patch 过的）
     └─ autorun（约 1MB 的 ELF，由内核 patch 在开机早期拉起）
         ├─ 内置模块：module_selinux（patch kernel switch）、module_tombstone_purge …
         └─ 批量加载用户模块：dlopen + dlsym("__skroot_module_main_18fdf393885363712349ef3de5411bd5")
             ├─ skroot_module_main(root_key, module_private_dir)   ← 同步，要快
             │   ├─ fork + exec: sh OmmegaCore/service.sh --boot    ← 同步等（≤8s）
             │   └─ fork + exec: sh OmmegaCore/service.sh --late    ← 后台
             └─ SKROOT_MODULE_WEB_UI 注册的 HTTP 服务（面板，点开时才起）
```

要点：

- 模块目录名由 root_key 派生（`/data/<root_key 前 16 字符>/modules/<id32>/`），
  路径不固定，脚本一律用 `${0%/*}` 推导，不能写死。
- 模块 zip 里除 `lib<name>.so` 和 `webroot/` 之外的文件，安装时会被解压到模块目录，
  统一给 0777。我们的业务文件都在 `OmmegaCore/` 子目录里，模块壳从
  `module_private_dir` 拼路径。
- 模块名片（名字/版本/描述/id32）编在 `.so` 里，没有 `module.prop` 参与加载；
  `OmmegaCore/module.prop` 只给面板读版本用。
- 模块入口符号是 SDK 宏生成的混淆名；autorun 打完会打一行
  `[Module End] name=..., skroot_module_main=..., elapsed=... ms`，跑失败是
  `[Module Error] name=..., error=...`。所以入口里别干重活，我们只 fork。
- 面板不是常驻的：管理器点“打开”时才 `start_module_web_ui_server_async(root_key, id32, &port)`，
  端口由 SDK 分配。所以 WebUI handler 不能假设自己和模块入口在同一个 dlopen 实例里，
  token 在 `onPrepareCreate` 里还要兜一层。

## 2. 两阶段启动

SKRoot 模块只有一次入口。A 端原本依赖 Magisk 的 `post-fs-data`（很早）和
`service`（late_start）两个时机，所以模块壳自己切成两段：

| 阶段 | 谁调 | 干什么 | 时机 |
| --- | --- | --- | --- |
| `--boot` | `skroot_module_main` 同步 fork+wait | 属性伪装、sepolicy、能力声明 bind、目录权限、早启动 keymint/injector | 模块加载时（官方说 zygote 前） |
| `--late` | 同一个入口 fork 到后台 | keymint 守护、注入器守护、SOTER、adbr、keep-alive 循环 | 模块加载后，脚本内部自己等框架就绪 |

`service.sh` 是唯一入口，按参数分派：`--boot` 转 `boot.sh`，其它走 late 逻辑，
`--restart` 只重拉守护（面板"重新启动"用）。

风险点：SKRoot 的加载时机是否真的在 zygote 之前，各机型可能不一样。如果落到 zygote
之后，`--boot` 里的属性写入对已 fork 的进程无效（这是 Android 属性区的性质，不是本模块的
问题），面板上的"伪装参数"就要靠 `--late` 里再写一次 + 软重启，或者接受只对新进程生效。

## 3. 权限模型

SKRoot Pro 有两条提权路径，别混在一起：

- **App 走 `/system/bin/su`**：uid 0，但 SELinux 域是 shell（`u:r:shell:s0`，
  skroot-zygisk 在一加/OPPO 上实测）。这是给第三方 App 用的那条。
- **内核 patch 这条（模块宿主、CVE 提权）**：uid 0 + 全 capability；ghostlock 提权
  （`cve2026_43499_ghostlock_src_v2/src/root.c`）除了把 cred 的 uid/gid 清零、caps 拉满，
  还会改写 cred 的 SELinux sid。更关键的是：SKRoot 对 `avc_denied()` 做了“零触碰”patch——
  把所有 `ret` 换成跳到代码洞，洞里判定当前进程是不是 **root-like**
  （`euid==0 && securebits==0 && capability 全满`），是就直接返回 0（放行），
  否则按原逻辑拒绝。也就是说 **root-like 进程在 SKRoot 下基本等于 SELinux 免疫**。

依据（官方公开源码）：

- `Lite_version/…/patch_tool/patch_pro_selinux.cpp`、`patch_avc_denied.cpp`、
  `patch_current_avc_check.cpp`；
- `Pro(众测开放中)/src/PermissionManager/cve2026_43499_ghostlock_src_v2/src/root.c`
  里的 `patch_cred_identity` / `patch_cred_sid`。

对 ommega A 端的含义：模块壳 fork 出来的脚本和守护继承模块宿主的 uid/caps，理论上是
root-like，所以读 `/data/misc/ommega`、`chcon`、`mount --bind`、`insmod`、ptrace 这些
SELinux 检查应该都被 avc patch 放过去。**仍然要看的是内核里非 SELinux 的门**：
keystore2 的 `PR_SET_DUMPABLE`、厂商自己加的 LSM/hook、`/proc/pid/mem` 被封那类
（skroot-zygisk 在某 OPPO 上遇到过 mem 全 I/O error），这些只能真机验。

## 4. 面板怎么接过来的

KSU 版的 `webroot/index.html` 只认 `window.ksu.exec(cmd, opts, cbId)`，
回调形式是 `window[cbId](errno, stdout, stderr)`。

SKRoot 的 WebUI 是模块自己的 HTTP 服务（CivetServer），所以：

1. 模块壳在返回 `index.html` 时往 `<head>` 里注入
   `window.OMMEGA_WEBUI_TOKEN` / `window.OMMEGA_MODDIR`；
2. `core/webroot/ksu-shim.js` 定义 `window.ksu`：
   - `exec` → `POST /api/exec`，拿到 JSON 再按 KSU 的契约回调；
   - `getPackagesInfo` → 同步 XHR `POST /api/packages`（面板这个接口本来是同步的，
     fetch 做不到，只在"作用范围"页展开时调一次）；
3. 模块壳的 `/api/exec` 用 `sh -c` 执行，返回 `{errno, stdout, stderr}`，
   用 `X-Ommega-Token` 校验（token 每次加载随机生成，只在页面里出现）。

面板里原来写死的 `/data/adb/modules/ommega` 改成从 `window.OMMEGA_MODDIR` 取，
值就是 `OmmegaCore` 的真实路径。

## 5. 状态与日志

跟 Magisk 版保持一致（方便两边对照排查）：

- 状态：`/data/adb/ommega/{conf,run,db}`
- 数据根：`/data/misc/ommega`（面板通过 `/data/adb/ommega/ommegadata` 软链写）
- 日志：`/data/misc/ommega/logs/{daemon,injector,keymint}.log`（开关在面板）

`/data/adb` 在 SKRoot 下是否存在还没实测；官方 TEESimulator / TrickyStore 的移植示例
也把配置放 `/data/adb/tricky_store`，所以按存在处理。
