# 更新日志

## 1.0.5

- 修复 SKRoot 下 SELinux 规则没真正进内核的问题：`ksud sepolicy apply` 在这类环境里返回 0，但规则一条都没落，之后 payload 在 keystore2 里权限不足（连自己的日志都写不进），出证请求全部透传给系统 keymint，应用侧表现为 `-74 ATTESTATION_KEYS_NOT_PROVISIONED`。现在 `SKROOT_MODE=1` 时直接调模块自带的 `ommega-policy --live --apply`，ksud / magiskpolicy 两条候选不再参与。

## 1.0.4

- 工程模式的「Attestation 全量功能测试」现在能通过；
- 本地出证时按调用方要求带上 ID 认证信息，和系统原生行为一致；
- 同步 A 端 1.7.14 二进制。
