#include "devpilot/process.hpp"
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <stdexcept>

ProcessResult run_process(const std::vector<std::string>& argv,
                          const std::filesystem::path& cwd,
                          std::chrono::seconds timeout,
                          std::size_t max_output) {
    using clock = std::chrono::steady_clock;
    int fds[2];
    if (pipe(fds) != 0) throw std::runtime_error("pipe() failed");

    auto start = clock::now();
    pid_t pid = fork();
    if (pid < 0) throw std::runtime_error("fork() failed");

    if (pid == 0) {                       // child process
        setpgid(0, 0);
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[0]);
        close(fds[1]);
        if (chdir(cwd.c_str()) != 0) _exit(126);
        std::vector<char*> args;
        for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
        args.push_back(nullptr);
        execvp(args[0], args.data());
        _exit(127);
    }

    setpgid(pid, pid);
    close(fds[1]);

    ProcessResult res;
    char buf[4096];
    for (;;) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            timeout - (clock::now() - start));
        if (left.count() <= 0) {
            res.timed_out = true;
            kill(-pid, SIGKILL);
            break;
        }
        pollfd pfd{fds[0], POLLIN, 0};
        int rc = poll(&pfd, 1, static_cast<int>(left.count()));
        if (rc < 0 && errno == EINTR) continue;
        if (rc <= 0) continue;
        ssize_t n = read(fds[0], buf, sizeof buf);
        if (n <= 0) break;
        if (res.output.size() < max_output)
            res.output.append(buf, static_cast<std::size_t>(n));
    }
    close(fds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) res.exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) res.exit_code = 128 + WTERMSIG(status);
    res.seconds = std::chrono::duration<double>(clock::now() - start).count();
    return res;
}
