# 更新日志

## 1.0.3

- 运行期不再往 /data/local/tmp 写文件：sepolicy 应用日志与面板的临时清单都改到数据目录；
- 同步 A 端 1.7.14 二进制。

## 1.0.2

- 面板新增「SOTER 走远程」开关：SOTER 请求发不发远端，与「远程 keymint」互不影响；
- 同步 A 端 1.7.14 的二进制，SOTER 转发不再被远程关着时挡回本机。

## 1.0.1

- 修复部分机型上带证明的密钥生成失败、无法取得证书链的问题；
- 证书中的应用标识补充包名、版本号与签名摘要，与系统实现保持一致；
- 应用标识表仅在应用安装或卸载后重建，日常运行不产生额外开销。

## 1.0.0

第一个 SKRoot Pro 版（A 端）：

- 模块壳 `libommega_skroot.so`：入口分 `--boot` / `--late` 两段拉起 OmmegaCore 脚本；
- 面板搬过来：模块自带 WebUI，`ksu-shim.js` 把面板的 `window.ksu` 接到 `/api/exec`；
- A 端二进制沿用 ommega 发布包里的那套（keymint / ommega-inject / resetprop / ommega-policy / adbrd）；
- 属性伪装、sepolicy、PathMask、SOTER、adbr 的脚本沿用 Magisk 版，按 SKRoot 环境做了取样和兜底；
- `build.py` 一条命令出 zip。
