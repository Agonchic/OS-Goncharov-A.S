#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#define BUF_SIZE 1024

static int itoa_simple(long long val, char *buf) {
    int zero_idx = 0;
    if (val == 0) {
        buf[zero_idx] = '0';
        return 1;
    }
    int i = 0;
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    for (int j = 0; j < i / 2; j++) {
        char tmp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = tmp;
    }
    return i;
}

static int ftoa_simple(float val, char *buf) {
    int pos = 0;
    if (val < 0.0f) {
        buf[pos++] = '-';
        val = -val;
    }

    long long ipart = (long long)val;
    float fpart = val - (float)ipart;
    long long frac = (long long)(fpart * 1000.0f + 0.5f);

    if (frac >= 1000) {
        ipart += 1;
        frac -= 1000;
    }

    pos += itoa_simple(ipart, buf + pos);
    buf[pos++] = '.';

    buf[pos + 2] = '0' + (frac % 10); frac /= 10;
    buf[pos + 1] = '0' + (frac % 10); frac /= 10;
    buf[pos]     = '0' + (frac % 10);
    pos += 3;

    buf[pos++] = '\n';
    buf[pos] = '\0';
    return pos;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        write(STDOUT_FILENO, "0", 1);
        return 1;
    }

    int file_fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file_fd == -1) {
        write(STDOUT_FILENO, "0", 1);
        return 1;
    }

    if (write(STDOUT_FILENO, "1", 1) != 1) {
        close(file_fd);
        return 1;
    }

    char in_buf[BUF_SIZE];
    char token[64];
    char out[64];
    size_t token_len = 0;
    float sum = 0.0f;
    int count = 0;
    ssize_t n = 0;

    while ((n = read(STDIN_FILENO, in_buf, sizeof(in_buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            char c = in_buf[i];

            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                if (token_len > 0) {
                    token[token_len] = '\0';
                    char *end = NULL;
                    float v = strtof(token, &end);
                    if (end != token) {
                        sum += v;
                        count++;
                    }
                    token_len = 0;
                }

                if (c == '\n') {
                    if (count > 0) {
                        int len = ftoa_simple(sum, out);
                        if (write(file_fd, out, len) != len) {
                            const char err[] = "Error: write to file failed\n";
                            write(STDERR_FILENO, err, sizeof(err) - 1);
                            close(file_fd);
                            return 1;
                        }
                    }
                    sum = 0.0f;
                    count = 0;
                }
            } else {
                if (token_len < sizeof(token) - 1) {
                    token[token_len++] = c;
                }
            }
        }
    }

    if (n < 0) {
        const char err[] = "Error: read from pipe failed\n";
        write(STDERR_FILENO, err, sizeof(err) - 1);
        close(file_fd);
        return 1;
    }

    if (token_len > 0) {
        token[token_len] = '\0';
        char *end = NULL;
        float v = strtof(token, &end);
        if (end != token) {
            sum += v;
            count++;
        }
    }
    if (count > 0) {
        int len = ftoa_simple(sum, out);
        if (write(file_fd, out, len) != len) {
            const char err[] = "Error: write to file failed\n";
            write(STDERR_FILENO, err, sizeof(err) - 1);
            close(file_fd);
            return 1;
        }
    }

    if (close(file_fd) == -1) {
        const char err[] = "Error: close file failed\n";
        write(STDERR_FILENO, err, sizeof(err) - 1);
        return 1;
    }

    return 0;
}


