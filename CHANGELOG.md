# 更新日志

## 1.0.6

- 修掉 `soterta.sh` 的单实例锁：部分环境里 `flock` 对任何 fd 都回 EBADF（不是「被别人占」），旧写法 `flock -n 9 || exit 0` 让脚本每一轮都静默退出，SOTER 一次都没接管过。现在先探测 flock 能不能用，不能用退回 `mkdir` 目录锁。
- 补上 SOTER 软件 TA 注册服务名需要的 SELinux 规则：`vendor.qti.hardware.soter.ISoter/default` 在 servicemanager 里映射到 `vendor_hal_soteraidl_service`，还要允许 servicemanager 读调用方的 `/proc/<pid>/attr/current`（`getpidcon` 失败会让 sctx 变成空串，检查照样报 “SELinux denied for service.”，permissive 下也一样）。
- 同步 A 端 1.7.15 的 keymint，并带上远程回落的两处修复：远程不可用而调用方自带证明密钥时不再硬失败丢 -1000，改为落本地用 keybox 出证；本地模式下遇到私钥在 B 端的那把证明密钥也不再拿它去本地签名。

## 1.0.5

- 修复 SKRoot 下 SELinux 规则没真正进内核的问题：`ksud sepolicy apply` 在这类环境里返回 0，但规则一条都没落，之后 payload 在 keystore2 里权限不足（连自己的日志都写不进），出证请求全部透传给系统 keymint，应用侧表现为 `-74 ATTESTATION_KEYS_NOT_PROVISIONED`。现在 `SKROOT_MODE=1` 时直接调模块自带的 `ommega-policy --live --apply`，ksud / magiskpolicy 两条候选不再参与。

## 1.0.4

- 工程模式的「Attestation 全量功能测试」现在能通过；
- 本地出证时按调用方要求带上 ID 认证信息，和系统原生行为一致；
- 同步 A 端 1.7.14 二进制。
