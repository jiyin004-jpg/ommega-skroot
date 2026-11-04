// WebUI：把 A 端原来跑在 KernelSU WebUI 里的面板搬过来。
// 面板本身只认 window.ksu.exec，所以这里提供一个 /api/exec，
// 由 webroot/ksu-shim.js 在浏览器侧接成 ksu 接口。
#pragma once
#include <string>

#include "third_party/civetweb-1.16/include/civetweb.h"

namespace ommega {

struct WebUiContext {
    std::string mod_dir;      // module_private_dir（/data/<key>/modules/<id32>）
    std::string root_key;     // 只用它调 get_root，不落盘
    std::string token;        // POST 接口的校验串，页面加载时注入
    std::string version;
    uint32_t port = 0;
};

// 全局 context 在 ommega_skroot.cpp 里定义，handler 直接读。
WebUiContext& webui_context();

std::string inject_page(const std::string& html, const WebUiContext& ctx);
std::string status_json(const WebUiContext& ctx);
std::string packages_json();
const char* mime_for(const std::string& path);
std::string header_value(struct mg_connection* conn, const char* name);

} // namespace ommega
