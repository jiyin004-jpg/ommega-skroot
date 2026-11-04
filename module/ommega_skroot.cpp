// Ommega 的 SKRoot Pro 版模块壳。
//
// 和 Magisk/KernelSU 版的区别只有"怎么被拉起来"这一层：
//   * Magisk 版靠 post-fs-data.sh + service.sh 两阶段；
//   * SKRoot 模块只有一次 skroot_module_main，没有 post-fs-data，
//     所以这里自己分两段：--boot 同步跑（属性/权限/能力声明），--late 丢后台跑（守护）。
//
// A 端干活的二进制（keymint / ommega-inject）还是同一套，放在 OmmegaCore/libs/<abi>/ 下，
// 脚本也基本沿用，改的几处见 core/service.sh 顶部的注释。

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "kernel_module_kit_umbrella.h"
#include "util.hpp"
#include "webui.hpp"

#define OMMEGA_MOD_VERSION "1.0.0"
#define CORE_SUBDIR "OmmegaCore"

using namespace ommega;

namespace {

std::string core_file(const std::string& name) {
    return join_path(webui_context().mod_dir, join_path(CORE_SUBDIR, name));
}

void log_line(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("[ommega-skroot] ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    fflush(stdout);
}

// 把 service.sh 拉起来。mode = "--boot" / "--late"。
// 子进程已经继承了 root 和 SELinux 域，不需要再把 root_key 传下去。
bool spawn_core(const char* mode, bool wait_briefly) {
    const std::string script = core_file("service.sh");
    if (access(script.c_str(), R_OK) != 0) {
        log_line("core script missing: %s", script.c_str());
        return false;
    }
    pid_t pid = fork();
    if (pid < 0) {
        log_line("fork failed: %s", strerror(errno));
        return false;
    }
    if (pid == 0) {
        setsid();
        setenv("OMMEGA_SKROOT", "1", 1);
        setenv("OMMEGA_MODDIR", webui_context().mod_dir.c_str(), 1);
        // 先接上 /dev/null，再把 3 以上的 fd 全关了。
        // 模块宿主是 autorun/管理器进程，手里可能挂着 socket（adbr 的也在里面），
        // 不关的话 late 阶段起的守护会替宿主捧着连接，宿主一退就把对端带崩。
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            if (devnull > 2) close(devnull);
        }
        int maxfd = (int)sysconf(_SC_OPEN_MAX);
        if (maxfd < 0 || maxfd > 4096) maxfd = 4096;
        for (int fd = 3; fd < maxfd; fd++) ::close(fd);
        execl("/system/bin/sh", "sh", script.c_str(), mode, (char*)nullptr);
        _exit(127);
    }
    if (wait_briefly) {
        int st = 0;
        // --boot 只做属性/权限那点事，正常几百毫秒就完；给 8 秒兜底，
        // 超时就让它自己继续跑 —— 模块入口不能长时间卡住开机。
        for (int i = 0; i < 80; i++) {
            pid_t r = waitpid(pid, &st, WNOHANG);
            if (r == pid) {
                if (WIFEXITED(st) && WEXITSTATUS(st) == 0) return true;
                log_line("boot stage exited with %d", WIFEXITED(st) ? WEXITSTATUS(st) : -1);
                return false;
            }
            usleep(100 * 1000);
        }
        log_line("boot stage still running after 8s, leaving it");
    }
    return true;
}

// 面板里所有需要 root 的动作都走这里：拿一条 shell 命令，跑完把
// errno/stdout/stderr 原样回给页面，跟 KernelSU 的 ksu.exec 回调契约一致。
class OmmegaWebUi : public kernel_module::WebUIHttpHandler {
public:
    void onPrepareCreate(const char* root_key, const char* module_private_dir, uint32_t port) override {
        auto& ctx = webui_context();
        if (module_private_dir) ctx.mod_dir = module_private_dir;
        if (root_key) ctx.root_key = root_key;
        // WebUI 是管理器点“打开”时才 start_module_web_ui_server_async 拉起的（端口由 SDK 分配），
        // 完全可能不在模块入口那个 dlopen 实例/进程里，所以 token 在这里再兜一层。
        if (ctx.token.empty()) ctx.token = random_hex(16);
        if (ctx.version.empty()) ctx.version = OMMEGA_MOD_VERSION;
        ctx.port = port;
        log_line("webui ready on port %u, dir=%s", port, ctx.mod_dir.c_str());
    }

    bool handleGet(CivetServer*, struct mg_connection* conn, const std::string& path,
                   const std::string&) override {
        auto& ctx = webui_context();
        if (path == "/" || path == "/index.html") {
            std::string html;
            if (!read_small_file(join_path(ctx.mod_dir, "webroot/index.html"), html)) {
                kernel_module::webui::send_text(conn, 500, "webroot/index.html missing");
                return true;
            }
            kernel_module::webui::send_html(conn, 200, inject_page(html, ctx));
            return true;
        }
        if (path == "/api/status") {
            kernel_module::webui::send_json(conn, 200, status_json(ctx));
            return true;
        }
        // 静态资源：只允许 webroot 下一层相对路径，挡住 ../ 穿越
        if (path.find("..") != std::string::npos) {
            kernel_module::webui::send_text(conn, 403, "forbidden");
            return true;
        }
        std::string body;
        std::string file = join_path(ctx.mod_dir, "webroot" + path);
        if (!read_small_file(file, body, 16u << 20)) {
            kernel_module::webui::send_text(conn, 404, "not found");
            return true;
        }
        kernel_module::webui::send_bytes(conn, 200, mime_for(path), body.data(), body.size());
        return true;
    }

    bool handlePost(CivetServer*, struct mg_connection* conn, const std::string& path,
                    const std::string& body) override {
        auto& ctx = webui_context();

        // 只校验会动系统的接口；静态资源和 status 不需要。
        if (path == "/api/exec" || path == "/api/restart") {
            if (!ctx.token.empty() && header_value(conn, "X-Ommega-Token") != ctx.token) {
                kernel_module::webui::send_json(conn, 403, "{\"errno\":13,\"stdout\":\"\",\"stderr\":\"bad token\"}");
                return true;
            }
        }

        if (path == "/api/exec") {
            std::string cmd = body;
            if (cmd.empty()) {
                kernel_module::webui::send_json(conn, 200, "{\"errno\":0,\"stdout\":\"\",\"stderr\":\"\"}");
                return true;
            }
            CmdResult r = run_shell(cmd, 120000);
            std::string json = "{";
            json += "\"errno\":" + std::to_string(r.err) + ",";
            json += "\"stdout\":\"" + json_escape(r.out) + "\",";
            json += "\"stderr\":\"" + json_escape(r.errout) + "\"}";
            kernel_module::webui::send_json(conn, 200, json);
            return true;
        }
        if (path == "/api/packages") {
            kernel_module::webui::send_json(conn, 200, packages_json());
            return true;
        }
        if (path == "/api/restart") {
            bool ok = spawn_core("--late", false);
            kernel_module::webui::send_json(conn, 200, ok ? "OK" : "FAILED");
            return true;
        }
        if (path == "/api/version") {
            kernel_module::webui::send_text(conn, 200, ctx.version);
            return true;
        }
        kernel_module::webui::send_text(conn, 404, "not found");
        return true;
    }
};

} // namespace

int skroot_module_main(const char* root_key, const char* module_private_dir) {
    auto& ctx = webui_context();
    ctx.mod_dir = module_private_dir ? module_private_dir : "";
    ctx.root_key = root_key ? root_key : "";
    ctx.version = OMMEGA_MOD_VERSION;
    ctx.token = random_hex(16);
    if (ctx.token.empty()) ctx.token = "ommega-fallback-token";

    log_line("starting, version=%s", OMMEGA_MOD_VERSION);
    log_line("module dir = %s", ctx.mod_dir.c_str());
    log_line("uid=%d euid=%d gid=%d", (int)getuid(), (int)geteuid(), (int)getgid());

    // 兜底：万一宿主不是以 root 身份 dlopen（正常不会），用 SDK 提一次。
    if (geteuid() != 0 && !ctx.root_key.empty()) {
        KModErr err = skroot_env::get_root(ctx.root_key.c_str());
        log_line("get_root -> %s", to_string(err).c_str());
    }

    // 资源目录：模块安装时框架已经解压好并给了 0777
    struct stat st{};
    const std::string core_dir = join_path(ctx.mod_dir, CORE_SUBDIR);
    if (stat(core_dir.c_str(), &st) != 0) {
        log_line("core dir missing: %s", core_dir.c_str());
    } else {
        ::chmod(core_dir.c_str(), 0755);
    }

    // 阶段一：开机早期（zygote 之前）能做的就地做完
    spawn_core("--boot", true);

    // 阶段二：需要 Binder/框架/网络的东西丢后台，等几秒再跑
    if (spawn_core("--late", false)) {
        log_line("late stage spawned");
    }
    return 0;
}

// ---- 模块名片 ----
SKROOT_MODULE_NAME("Ommega")
SKROOT_MODULE_VERSION(OMMEGA_MOD_VERSION)
SKROOT_MODULE_DESC("远程 TEE 出证转发 + 本机伪装（SKRoot Pro 版）")
SKROOT_MODULE_AUTHOR("jiyin004")
SKROOT_MODULE_ID32("ommegaskrootpro00000000000000000")
SKROOT_MODULE_WEB_UI(OmmegaWebUi)
