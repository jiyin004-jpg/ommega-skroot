// 模块壳用的小工具：跑命令拿输出、JSON 转义、随机 token、读小文件。
// 只在用户态用，不碰内核。
#pragma once
#include <string>
#include <vector>

namespace ommega {

struct CmdResult {
    int err = -1;             // 退出码；-1 表示 fork/执行本身失败，-2 表示超时被杀
    std::string out;          // stdout
    std::string errout;       // stderr
};

// 用 /system/bin/sh -c 跑一条命令，stdout / stderr 分开收。
// timeout_ms <= 0 表示不限时；输出超过 max_bytes 的部分直接丢掉（防止面板把大文件读爆内存）。
CmdResult run_shell(const std::string& cmd, int timeout_ms = 60000, size_t max_bytes = 8u << 20);

// 只关心成功与否、不需要输出时用它。
bool run_shell_ok(const std::string& cmd, int timeout_ms = 60000);

std::string json_escape(const std::string& s);

// 从 /dev/urandom 取 n 字节，返回十六进制串；失败返回空。
std::string random_hex(size_t n);

// 整个文件读进来，超过 max_bytes 就截断（读失败返回 false）。
bool read_small_file(const std::string& path, std::string& out, size_t max_bytes = 4u << 20);

std::string dirname_of(const std::string& path);
std::string join_path(const std::string& a, const std::string& b);

} // namespace ommega
