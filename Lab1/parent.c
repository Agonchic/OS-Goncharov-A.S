#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>

#define BUF_SIZE 1024

static void print_str(int fd, const char *s) {
    write(fd, s, strlen(s));
}

int main(void) {
    char filename[256];
    char buf[BUF_SIZE];
    size_t filename_len = 0;
    ssize_t n = 0;
    ssize_t leftover_start = 0;
    ssize_t leftover_len = 0;
    int got_newline = 0;
    print_str(STDOUT_FILENO, "Enter filename: ");

    while (!got_newline && (n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                got_newline = 1;
                leftover_start = i + 1;
                leftover_len = n - (i + 1);
                break;
            }
            if (buf[i] != '\r' && filename_len < sizeof(filename) - 1) {
                filename[filename_len++] = buf[i];
            }
        }
    }

    if (n < 0) {
        print_str(STDERR_FILENO, "Error: read failed\n");
        return 1;
    }
    filename[filename_len] = '\0';
    if (filename_len == 0) {
        print_str(STDERR_FILENO, "Error: empty filename\n");
        return 1;
    }

    int p1[2];
    int p2[2];
    if (pipe(p1) == -1 || pipe(p2) == -1) {
        print_str(STDERR_FILENO, "Error: pipe failed\n");
        return 1;
    }
    pid_t pid = fork();
    if (pid == -1) {
        print_str(STDERR_FILENO, "Error: fork failed\n");
        return 1;
    }

    if (pid == 0) {
        close(p1[1]);
        close(p2[0]);

        if (dup2(p1[0], STDIN_FILENO) == -1 || dup2(p2[1], STDOUT_FILENO) == -1) {
            print_str(STDERR_FILENO, "Error: dup2 failed\n");
            _exit(1);
        }

        close(p1[0]);
        close(p2[1]);

        char *args[3];
        int idx = 0;
        args[idx++] = "./child";
        args[idx++] = filename;
        args[idx] = NULL;

        execv("./child", args);

        print_str(STDERR_FILENO, "Error: execv failed\n");
        _exit(1);
    }

    close(p1[0]);
    close(p2[1]);

    char status_ch = 0;
    if (read(p2[0], &status_ch, 1) <= 0 || status_ch != '1') {
        print_str(STDERR_FILENO, "Error: child process failed open file\n");
        close(p1[1]);
        close(p2[0]);
        waitpid(pid, NULL, 0);
        return 1;
    }

    if (leftover_len > 0) {
        if (write(p1[1], buf + leftover_start, leftover_len) != leftover_len) {
            print_str(STDERR_FILENO, "Error: write to pipe failed\n");
            close(p1[1]);
            close(p2[0]);
            waitpid(pid, NULL, 0);
            return 1;
        }
    }

    while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
        if (write(p1[1], buf, n) != n) {
            print_str(STDERR_FILENO, "Error: write to pipe failed\n");
            break;
        }
    }

    if (n < 0) {
        print_str(STDERR_FILENO, "Error: read from stdin failed\n");
    }

    close(p1[1]);
    close(p2[0]);
    if (waitpid(pid, NULL, 0) == -1) {
        print_str(STDERR_FILENO, "Error: waitpid failed\n");
        return 1;
    }
    return 0;
}

