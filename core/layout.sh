#!/system/bin/sh
# 开机幂等补一件事：`/data/adb/ommega/ommegadata` 软链。
#
# 面板「保存并生效」要把配置写进 /data/misc/ommega/，写的是这个软链；
# 它只在 customize.sh 里建过，而手动解压 / 热更新 / 直接把新文件盖进老模块目录
# 都不会跑 customize.sh，缺了它面板一保存就 No such file or directory。
#
# 这里**不再**搬二进制、不再往 bin/ 复制 resetprop：运行文件只认模块目录
# `libs/<abi>/`，`bin/` 那套热更新布局已废弃。

STATE_DIR=/data/adb/ommega
DATA_ROOT=/data/misc/ommega

# 软链目标必须就是数据根。老版本建过指向 /data/misc/keystore/ommega 的软链，
# 那种不会报错，只会让面板写的配置落到程序不读的地方（远程模式永远开不起来），
# 所以这里不光补缺，指向不对也直接重建。
mkdir -p "$DATA_ROOT" 2>/dev/null
if [ ! -L "$STATE_DIR/ommegadata" ] || [ "$(readlink "$STATE_DIR/ommegadata" 2>/dev/null)" != "$DATA_ROOT" ]; then
  rm -f "$STATE_DIR/ommegadata" 2>/dev/null
  ln -s "$DATA_ROOT" "$STATE_DIR/ommegadata" 2>/dev/null
fi

# 组 system 可写（keystore 域要写 remote_identity / config），other 留 x 让 app 域读。
# 目录可能是别人建的，所以每次开机都正一次。
#
# owner 必须是 keystore：keystore2 里的 TA 要往这里落 remote_identity、keymint 要
# atomic replace config.toml，两者都是「在目录里建临时文件再 rename」，光有文件
# 写权限不够，得目录可写。这个目录是 root 脚本建的，main.rs 降权后那次 chown
# 改不动它，所以只能在 root 这边做。
mkdir -p "$DATA_ROOT/logs" 2>/dev/null
chown keystore:keystore "$DATA_ROOT" 2>/dev/null
chown keystore:keystore "$DATA_ROOT/logs" 2>/dev/null
chmod 0771 "$DATA_ROOT" 2>/dev/null
# logs 用 sticky：app 域（SOTER 宿主不是 keystore uid）的 payload 要往这里写日志，
# 不开 other 写它就落不下档；sticky 位拦住删掉 / 改名别人的文件。
chmod 1777 "$DATA_ROOT/logs" 2>/dev/null

# ---- SKRoot Pro 兼容 ----
# keymint 是 1.6.9 的二进制，两个路径是编译期写死的：
#   /data/adb/modules/ommega/libs/<abi>/keymint   —— 守护拿它找自己
#   /data/misc/keystore/ommega/...                —— rpc.sock / 数据
# SKRoot 下模块装在 /data/<key>/modules/<id32>/，这两个路径都不存在，靠软链补。
#
# 另外 keymint 启动后会把 uid 降到 keystore，但 SELinux 域还是 shell ——
# SKRoot 的 avc 放行只认 root-like（uid 0 + 全 caps），降权后就没了，
# 表现在建 rpc.sock 被 `avc: denied { create } unix_stream_socket` 直接拒。
# 所以 SKRoot 下让它保持 root（rpc.sock 的客户端是我们自己的注入器，不涉及 peer 校验）。
if [ "${SKROOT_MODE:-0}" = "1" ]; then
  # keymint 保持 root：降权后它绑不了 socket（avc 只对 root-like 放行），
  # binder 线程池初始化也会直接 abort。AAID 那一步改成本地从 packages.list 造，
  # 不再依赖只有 Keystore/Credstore 能调的 system provider。
  export OMMEGA_KEEP_ROOT=1
  mkdir -p /data/adb/modules /data/misc/keystore 2>/dev/null
  ln -sfn "$MODROOT" /data/adb/modules/ommega 2>/dev/null
  ln -sfn "$MODDIR/libs" "$MODROOT/libs" 2>/dev/null
  ln -sfn "$DATA_ROOT" /data/misc/keystore/ommega 2>/dev/null
  mkdir -p "$DATA_ROOT/logs" 2>/dev/null
  [ -f "$DATA_ROOT/logs/keymint.log" ] || : > "$DATA_ROOT/logs/keymint.log" 2>/dev/null
  chown keystore:keystore "$DATA_ROOT/logs/keymint.log" 2>/dev/null
  chmod 0666 "$DATA_ROOT/logs/keymint.log" 2>/dev/null
  chmod 1777 "$DATA_ROOT/logs" 2>/dev/null
fi
