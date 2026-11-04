#include "webui.hpp"

#include <cstring>
#include <sstream>
#include <unistd.h>

#include "util.hpp"

namespace ommega {

WebUiContext& webui_context() {
    static WebUiContext ctx;
    return ctx;
}

const char* mime_for(const std::string& path) {
    auto ends = [&](const char* s) {
        size_t n = strlen(s);
        return path.size() >= n && path.compare(path.size() - n, n, s) == 0;
    };
    if (ends(".html") || ends(".htm")) return "text/html; charset=utf-8";
    if (ends(".js")) return "application/javascript; charset=utf-8";
    if (ends(".css")) return "text/css; charset=utf-8";
    if (ends(".json")) return "application/json; charset=utf-8";
    if (ends(".svg")) return "image/svg+xml";
    if (ends(".png")) return "image/png";
    if (ends(".jpg") || ends(".jpeg")) return "image/jpeg";
    if (ends(".webp")) return "image/webp";
    if (ends(".ico")) return "image/x-icon";
    if (ends(".txt") || ends(".log") || ends(".conf")) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

// 面板的入口页：把 token 和真实模块目录塞进去。
// 页面里写死的 /data/adb/modules/ommega 在 SKRoot 下不存在，靠这两个变量纠正。
std::string inject_page(const std::string& html, const WebUiContext& ctx) {
    // 面板里的 MOD 指向 OmmegaCore（脚本/injector.toml/resetprop 都它下面），
    // 不是 SKRoot 的模块私有目录本身。
    const std::string core = join_path(ctx.mod_dir, "OmmegaCore");
    std::string head =
        "<script>window.OMMEGA_SKROOT=1;"
        "window.OMMEGA_WEBUI_TOKEN=\"" + json_escape(ctx.token) + "\";"
        "window.OMMEGA_MODDIR=\"" + json_escape(core) + "\";"
        "</script>";
    const std::string marker = "<head>";
    size_t p = html.find(marker);
    if (p == std::string::npos) return head + html;
    return html.substr(0, p + marker.size()) + head + html.substr(p + marker.size());
}

// 请求头取值：CivetWeb 把请求头挂在 mg_request_info->http_headers 上。
std::string header_value(struct mg_connection* conn, const char* name) {
    const struct mg_request_info* ri = mg_get_request_info(conn);
    if (!ri) return {};
    for (int i = 0; i < ri->num_headers; i++) {
        if (strcasecmp(ri->http_headers[i].name, name) == 0)
            return ri->http_headers[i].value ? ri->http_headers[i].value : "";
    }
    return {};
}

std::string status_json(const WebUiContext& ctx) {
    auto alive = [](const char* name) {
        CmdResult r = run_shell(std::string("pidof ") + name + " 2>/dev/null", 3000);
        return r.err == 0 && !r.out.empty();
    };
    std::string s = "{";
    s += "\"version\":\"" + json_escape(ctx.version) + "\",";
    s += "\"modDir\":\"" + json_escape(ctx.mod_dir) + "\",";
    s += "\"port\":" + std::to_string(ctx.port) + ",";
    s += "\"pid\":" + std::to_string((long)getpid()) + ",";
    s += "\"uid\":" + std::to_string((long)getuid()) + ",";
    s += "\"running\":{";
    s += "\"keymint\":" + std::string(alive("keymint") ? "true" : "false") + ",";
    s += "\"injector\":" + std::string(alive("ommega-inject") ? "true" : "false");
    s += "}}";
    return s;
}

// 面板「作用范围」页要列出可勾选的第三方应用。
// KSU 的 ksu.getPackagesInfo 走 PackageManager，我们这里只能读
// /data/system/packages.list（root 可读），label 一律用包名兜底。
std::string packages_json() {
    std::string list;
    if (!read_small_file("/data/system/packages.list", list, 8u << 20)) return "[]";
    std::istringstream in(list);
    std::string line;
    std::string out = "[";
    bool first = true;
    while (std::getline(in, line)) {
        // 格式: name uid debuggable dataDir seinfo gids ...
        std::istringstream ls(line);
        std::string name;
        long uid = -1;
        ls >> name >> uid;
        if (name.empty() || uid < 0) continue;
        if (uid < 10000) continue;   // 只留第三方
        if (!first) out += ",";
        first = false;
        out += "{\"packageName\":\"" + json_escape(name) + "\",\"label\":\"" +
               json_escape(name) + "\",\"uid\":" + std::to_string(uid) + "}";
    }
    out += "]";
    return out;
}

} // namespace ommega
