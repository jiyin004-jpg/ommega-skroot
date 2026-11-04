#include "util.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ommega {

std::string join_path(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

std::string dirname_of(const std::string& path) {
    auto p = path.find_last_of('/');
    if (p == std::string::npos) return ".";
    if (p == 0) return "/";
    return path.substr(0, p);
}

bool read_small_file(const std::string& path, std::string& out, size_t max_bytes) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    if (n < 0) return false;
    if ((size_t)n > max_bytes) n = (std::streamoff)max_bytes;
    f.seekg(0, std::ios::beg);
    out.resize((size_t)n);
    if (n > 0) f.read(&out[0], n);
    out.resize((size_t)f.gcount());
    return true;
}

std::string json_escape(const std::string& s) {
    std::string r;
    r.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
            case '"': r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\b': r += "\\b"; break;
            case '\f': r += "\\f"; break;
            case '\n': r += "\\n"; break;
            case '\r': r += "\\r"; break;
            case '\t': r += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    r += buf;
                } else {
                    r += (char)c;
                }
        }
    }
    return r;
}

std::string random_hex(size_t n) {
    std::string out;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return out;
    std::vector<unsigned char> buf(n);
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, buf.data() + got, n - got);
        if (r <= 0) { close(fd); return std::string(); }
        got += (size_t)r;
    }
    close(fd);
    static const char* hex = "0123456789abcdef";
    out.reserve(n * 2);
    for (unsigned char b : buf) {
        out += hex[b >> 4];
        out += hex[b & 0xf];
    }
    return out;
}

CmdResult run_shell(const std::string& cmd, int timeout_ms, size_t max_bytes) {
    CmdResult res;
    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    if (pipe(out_pipe) != 0) return res;
    if (pipe(err_pipe) != 0) {
        close(out_pipe[0]); close(out_pipe[1]);
        return res;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // 子进程：把两个 pipe 挂到 1/2，然后 exec sh
        setsid();
        ::close(out_pipe[0]);
        ::close(err_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        ::close(out_pipe[1]);
        ::close(err_pipe[1]);
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) { dup2(devnull, STDIN_FILENO); close(devnull); }
        execl("/system/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
        _exit(127);
    }
    ::close(out_pipe[1]);
    ::close(err_pipe[1]);
    if (pid < 0) {
        ::close(out_pipe[0]); ::close(err_pipe[0]);
        return res;
    }

    // 非阻塞读两个管道；stderr 只留前面一段，stdout 就是面板要的东西。
    fcntl(out_pipe[0], F_SETFL, fcntl(out_pipe[0], F_GETFL) | O_NONBLOCK);
    fcntl(err_pipe[0], F_SETFL, fcntl(err_pipe[0], F_GETFL) | O_NONBLOCK);

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    const size_t err_keep = 64u << 10;
    bool out_open = true, err_open = true;
    bool timed_out = false;
    char buf[8192];

    while (out_open || err_open) {
        if (timeout_ms > 0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long long elapsed = (long long)(now.tv_sec - start.tv_sec) * 1000 +
                                (now.tv_nsec - start.tv_nsec) / 1000000;
            if (elapsed > timeout_ms) { timed_out = true; break; }
        }
        struct pollfd fds[2];
        int nfds = 0;
        if (out_open) fds[nfds++] = {out_pipe[0], POLLIN, 0};
        if (err_open) fds[nfds++] = {err_pipe[0], POLLIN, 0};
        int pr = poll(fds, (nfds_t)nfds, 200);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) continue;
        for (int i = 0; i < nfds; i++) {
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            bool is_out = fds[i].fd == out_pipe[0];
            ssize_t n = read(fds[i].fd, buf, sizeof(buf));
            if (n > 0) {
                if (is_out) {
                    if (res.out.size() < max_bytes) {
                        size_t take = max_bytes - res.out.size();
                        res.out.append(buf, (size_t)(n < (ssize_t)take ? n : (ssize_t)take));
                    }
                } else {
                    if (res.errout.size() < err_keep) {
                        size_t take = err_keep - res.errout.size();
                        res.errout.append(buf, (size_t)(n < (ssize_t)take ? n : (ssize_t)take));
                    }
                }
            } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
                if (is_out) { out_open = false; ::close(out_pipe[0]); out_pipe[0] = -1; }
                else { err_open = false; ::close(err_pipe[0]); err_pipe[0] = -1; }
            }
        }
    }
    if (out_pipe[0] >= 0) ::close(out_pipe[0]);
    if (err_pipe[0] >= 0) ::close(err_pipe[0]);

    if (timed_out) {
        kill(-pid, SIGKILL);
        kill(pid, SIGKILL);
        int st = 0;
        waitpid(pid, &st, 0);
        res.err = -2;
        if (res.errout.empty()) res.errout = "command timed out";
        return res;
    }

    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    if (WIFEXITED(st)) res.err = WEXITSTATUS(st);
    else if (WIFSIGNALED(st)) res.err = 128 + WTERMSIG(st);
    else res.err = -1;
    return res;
}

bool run_shell_ok(const std::string& cmd, int timeout_ms) {
    CmdResult r = run_shell(cmd, timeout_ms);
    return r.err == 0;
}

} // namespace ommega
