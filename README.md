# ommega-skroot

Ommega 的 **SKRoot Pro 版**模块。同一套 A 端业务（远程 TEE 出证 + 本机伪装），
换成 SKRoot Pro 的模块外壳，单独一份仓库维护。

跟 ommega 主仓库的关系：业务二进制（`keymint` / `ommega-inject` / `resetprop` /
`ommega-policy`）仍由 ommega 构建，这里只负责"取过来、摆进去、拉起来、给面板"。

## 现状（先说清楚）

已验证的：

- 模块壳 `libommega_skroot.so` 能用 NDK 交叉编出来（arm64-v8a，静态链 SKRoot SDK）；
- 能打成 SKRoot 认的 zip（`libommega_skroot.so` + `webroot/` + `OmmegaCore/`），
  业务二进制、keybox、脚本都齐，构建脚本会自检文件名和模块名片；
- 全部 shell 脚本过了 `dash -n` 语法检查。

**没验证的**（没有 SKRoot Pro 实机，没有 SKP 密钥，装不上去）：

- 模块能不能被管理器正常安装、加载、跑起来；
- keystore2 的 ptrace 能不能打进去（SELinux 这一关按官方源码看是过了的，剩下是
  `PR_SET_DUMPABLE` / 厂商 LSM 那一关），见 `docs/PORTING.md`；
- `insmod pathmask*.ko`、`mount --bind` 改权限 XML、`chcon` 这些在 SKRoot 下是否被允许；
- WebUI 端口、token 注入、`/api/exec` 在真机 WebView 里的表现。

没有实机之前，README 里不会写"支持某机型"，安装包也不进 Release。

## 跟 Magisk/KernelSU 版的差别

| 事情 | Magisk/KernelSU 版 | SKRoot Pro 版 |
| --- | --- | --- |
| 启动入口 | `post-fs-data.sh` + `service.sh` 两阶段 | 模块壳一次入口，自己切 `--boot` / `--late` 两段 |
| 模块目录 | `/data/adb/modules/ommega` | `/data/<root_key 前16位>/modules/<id32>/OmmegaCore` |
| 面板 | 管理器托管的 WebUI + `window.ksu` 桥 | 模块自带 HTTP WebUI + `webroot/ksu-shim.js` 把 `ksu` 接到 `/api/exec` |
| resetprop | 模块自带的 `resetprop-rs` | 优先用 SKRoot 自带那份（`/data/<key17>/resetprop`），再落回自带的 |
| sepolicy | `ksud sepolicy apply` / `magiskpolicy` | 先试模块自带的 `ommega-policy`，没有就只记日志（SKRoot 没有 magiskpolicy） |
| PathMask | `insmod` 官方 `.ko` | 仍然 `insmod`，失败由 `kmod-loader.sh` 记状态；内核级替代方案见 TODO |
| 模块状态 | 改 `module.prop` | SKRoot 的名片编在 `.so` 里，`module.prop` 只给面板读版本 |

## 目录

```
ommega-skroot/
├─ build.py                  # 编 .so + 取二进制 + 打 zip
├─ module/                   # 模块壳（C++）
│  ├─ ommega_skroot.cpp      # 入口 + 两阶段启动 + WebUI 回调
│  ├─ util.cpp               # 跑命令 / JSON 转义 / 随机 token
│  ├─ webui.cpp              # WebUI 接口（/api/exec、/api/packages、/api/status）
│  └─ jni/                   # ndk-build 工程
├─ core/                     # 打进 OmmegaCore/ 的东西（脚本 + 资源）
│  ├─ service.sh / boot.sh   # 两阶段入口（从 ommega 的 post-fs-data/service 拆出来）
│  ├─ daemon-injector ...    # 其余脚本沿用 ommega
│  ├─ webroot/               # 面板（index.html + ksu-shim.js）
│  └─ pathmask/*.ko
├─ vendor/kernel_module_kit/ # SKRoot SDK（头文件 + 静态库，来自官方 Pro 目录）
└─ docs/
   ├─ ARCHITECTURE.md        # 加载链、权限模型、各组件职责
   └─ PORTING.md             # 逐项适配对照表（已做 / 待验证 / 未实现）
```

## 构建

需要：

- Android NDK 26d+（本机默认 `D:\android-ndk-r27d`，也可 `--ndk` 或 `ANDROID_NDK_HOME`）；
- 一份 A 端发布包（含 `libs/arm64-v8a/`），或者已经解出来的那个目录。

```sh
python build.py                     # 自动找最新的 ommega-a-release-*.zip
python build.py --a-zip path/to/ommega-a-release-1.7.14-xxxx.zip
python build.py --a-bin-dir path/to/libs/arm64-v8a
python build.py --skip-build        # 只用上次编好的 .so
```

产物：`dist/ommega-skroot-<version>.zip` 和同名 `.sha256`。

## 安装

在 SKRoot Pro 管理器里刷入 `dist/ommega-skroot-<version>.zip`（需要设备的 SKP 密钥）。
装好后从模块卡片打开 WebUI，面板跟 KSU 版一致。

卸载：管理器里卸载模块。状态目录 `/data/adb/ommega` 和 `/data/misc/ommega` 不会自动删，
里面是配置和 keybox，要清自己确认后再删。

## 许可

代码沿用 ommega 的 AGPL-3.0（见 `LICENSE`）。
`vendor/kernel_module_kit/` 是 SKRoot 官方发布的模块开发 SDK（头文件 + 静态库），
版权归其作者，只作为构建依赖放在这里；`core/pathmask/*.ko` 来自 ommega 仓库里
PathMask 上游的产物，说明见 `core/pathmask/UPSTREAM.md`。
