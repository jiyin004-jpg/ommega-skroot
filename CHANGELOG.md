# 更新日志

## 1.0.0

第一个 SKRoot Pro 版（A 端）：

- 模块壳 `libommega_skroot.so`：入口分 `--boot` / `--late` 两段拉起 OmmegaCore 脚本；
- 面板搬过来：模块自带 WebUI，`ksu-shim.js` 把面板的 `window.ksu` 接到 `/api/exec`；
- A 端二进制沿用 ommega 发布包里的那套（keymint / ommega-inject / resetprop / ommega-policy / adbrd）；
- 属性伪装、sepolicy、PathMask、SOTER、adbr 的脚本沿用 Magisk 版，按 SKRoot 环境做了取样和兜底；
- `build.py` 一条命令出 zip。
