/* SKRoot Pro 版 WebUI 的 ksu 桥。
 *
 * 面板（index.html）整段交互都写成 KernelSU 的 window.ksu.exec 契约：
 *   ksu.exec(cmd, opts, cbId)  ->  跑完调用 window[cbId](errno, stdout, stderr)
 * SKRoot 没有 ksu 桥，模块自带一个 HTTP WebUI（就是当前页面这个端口），
 * 所以这里把 ksu 接过去：exec -> POST /api/exec。
 *
 * 真 KSU 环境（面板被 KSU 管理器托管）下不覆盖已有的 ksu。
 */
(function () {
  if (window.ksu && typeof window.ksu.exec === "function") return;

  function token() { return window.OMMEGA_WEBUI_TOKEN || ""; }

  function post(path, body) {
    return fetch(path, {
      method: "POST",
      headers: {
        "Content-Type": "text/plain; charset=utf-8",
        "X-Ommega-Token": token()
      },
      body: body || ""
    }).then(function (r) { return r.json(); });
  }

  var api = {};

  api.exec = function (cmd, opts, cbId) {
    var done = function (errno, stdout, stderr) {
      var fn = window[cbId];
      if (typeof fn === "function") {
        try { fn(errno, stdout, stderr); } catch (e) {}
      }
    };
    post("/api/exec", cmd).then(function (r) {
      done(r && typeof r.errno === "number" ? r.errno : 0,
           (r && r.stdout) || "", (r && r.stderr) || "");
    }).catch(function (e) {
      done(1, "", String((e && e.message) || e));
    });
  };

  /* KSU 的这个接口是同步返回 JSON 字符串，fetch 做不到，只能同步 XHR。
     它只在「作用范围」页展开时调一次（几千条包名），卡一下可以接受。 */
  api.getPackagesInfo = function (pkgsJson) {
    var all = window.__ommegaPackages;
    if (!all) {
      try {
        var xhr = new XMLHttpRequest();
        xhr.open("POST", "/api/packages", false);
        xhr.setRequestHeader("Content-Type", "text/plain; charset=utf-8");
        xhr.setRequestHeader("X-Ommega-Token", token());
        xhr.send("");
        all = JSON.parse(xhr.responseText || "[]");
      } catch (e) {
        all = [];
      }
      window.__ommegaPackages = all;
    }
    var want = null;
    try { want = JSON.parse(pkgsJson || "null"); } catch (e) {}
    if (!want || !want.length) return JSON.stringify(all);
    var map = {};
    all.forEach(function (p) { map[p.packageName] = p; });
    var out = [];
    want.forEach(function (n) { if (map[n]) out.push(map[n]); });
    return JSON.stringify(out);
  };

  window.ksu = api;
})();
