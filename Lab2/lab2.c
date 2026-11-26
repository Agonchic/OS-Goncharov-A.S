#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdlib.h>

#define INT_BUF 32
#define LINE_BUF 65536

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_active_threads = 0;
static int g_max_threads = 1;

typedef struct {
    int *arr;
    int left;
    int right;
    int threshold;
} task_t;

static int slen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void pstr(int fd, const char *s) {
    write(fd, s, slen(s));
}

static void pint(int fd, long v) {
    char buf[INT_BUF];
    int i = INT_BUF - 1;
    int neg = 0;
    buf[i--] = '\n';
    if (v == 0) {
        buf[i--] = '0';
    } else {
        if (v < 0) { neg = 1; v = -v; }
        while (v > 0) {
            buf[i--] = '0' + (v % 10);
            v /= 10;
        }
        if (neg) buf[i--] = '-';
    }
    write(fd, buf + i + 1, INT_BUF - 1 - i);
}

static int atoi_(const char *s, int *out) {
    int sign = 1;
    int val = 0;
    int i = 0;
    if (!s || !s[0]) return 0;
    if (s[0] == '-') { sign = -1; i = 1; }
    if (!s[i]) return 0;
    for (; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9') return 0;
        val = val * 10 + (s[i] - '0');
    }
    *out = sign * val;
    return 1;
}

static int streq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static void swp(int *a, int *b) {
    int t = *a;
    *a = *b;
    *b = t;
}

static int part(int *arr, int left, int right) {
    int mid = left + (right - left) / 2;
    if (arr[mid] < arr[left])   swp(&arr[mid], &arr[left]);
    if (arr[right] < arr[left]) swp(&arr[right], &arr[left]);
    if (arr[right] < arr[mid])  swp(&arr[right], &arr[mid]);
    int pivot = arr[mid];
    swp(&arr[mid], &arr[right]);

    int i = left - 1;
    for (int j = left; j < right; j++) {
        if (arr[j] <= pivot) {
            i++;
            swp(&arr[i], &arr[j]);
        }
    }
    swp(&arr[i + 1], &arr[right]);
    return i + 1;
}

static void qsort_seq(int *arr, int left, int right) {
    while (left < right) {
        int pivot = part(arr, left, right);
        if (pivot - left < right - pivot) {
            qsort_seq(arr, left, pivot - 1);
            left = pivot + 1;
        } else {
            qsort_seq(arr, pivot + 1, right);
            right = pivot - 1;
        }
    }
}

static void qsort_par(int *arr, int left, int right, int threshold);

static void *worker(void *arg) {
    task_t *t = (task_t *)arg;
    qsort_par(t->arr, t->left, t->right, t->threshold);
    free(t);
    return NULL;
}

static void qsort_par(int *arr, int left, int right, int threshold) {
    if (right - left < threshold) {
        qsort_seq(arr, left, right);
        return;
    }

    int pivot = part(arr, left, right);

    int can_spawn = 0;
    pthread_mutex_lock(&g_mutex);
    if (g_active_threads < g_max_threads - 1) {
        g_active_threads++;
        can_spawn = 1;
    }
    pthread_mutex_unlock(&g_mutex);

    if (can_spawn) {
        task_t *t = malloc(sizeof(task_t));
        if (!t) {
            pthread_mutex_lock(&g_mutex);
            g_active_threads--;
            pthread_mutex_unlock(&g_mutex);
            qsort_par(arr, left, pivot - 1, threshold);
            qsort_par(arr, pivot + 1, right, threshold);
            return;
        }
        t->arr = arr;
        t->left = left;
        t->right = pivot - 1;
        t->threshold = threshold;

        pthread_t tid;
        if (pthread_create(&tid, NULL, worker, t) == 0) {
            qsort_par(arr, pivot + 1, right, threshold);
            pthread_join(tid, NULL);
            pthread_mutex_lock(&g_mutex);
            g_active_threads--;
            pthread_mutex_unlock(&g_mutex);
            return;
        }
        pthread_mutex_lock(&g_mutex);
        g_active_threads--;
        pthread_mutex_unlock(&g_mutex);
        free(t);
    }

    qsort_par(arr, left, pivot - 1, threshold);
    qsort_par(arr, pivot + 1, right, threshold);
}

static int *gen_arr(int n, unsigned int seed) {
    int *arr = malloc((size_t)n * sizeof(int));
    if (!arr) {
        pstr(2, "Error: malloc failed\n");
        _exit(1);
    }
    srand(seed);
    for (int i = 0; i < n; i++) {
        arr[i] = rand() - RAND_MAX / 2;
    }
    return arr;
}

static int sorted(const int *arr, int n) {
    for (int i = 1; i < n; i++)
        if (arr[i - 1] > arr[i]) return 0;
    return 1;
}

static double nowms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static void parr(int fd, const int *arr, int n) {
    for (int i = 0; i < n; i++) {
        pint(fd, arr[i]);
    }
}

static int rline(int fd, char *buf, int size) {
    int total = 0;
    while (total < size - 1) {
        ssize_t n = read(fd, buf + total, size - 1 - total);
        if (n <= 0) break;
        total += (int)n;
        if (buf[total - 1] == '\n') break;
    }
    buf[total] = '\0';
    return total;
}

int main(int argc, char **argv) {
    int n = 1000000;
    int max_threads = 4;
    int threshold = 10000;
    int seed_in = 42;
    int skip_seq = 0;
    int mode_random = 1;
    int mode_input = 0;
    int tmp = 0;

    for (int i = 1; i < argc; i++) {
        if (streq(argv[i], "-n") && i + 1 < argc && atoi_(argv[++i], &tmp)) n = tmp;
        else if (streq(argv[i], "-t") && i + 1 < argc && atoi_(argv[++i], &tmp)) max_threads = tmp;
        else if (streq(argv[i], "-m") && i + 1 < argc && atoi_(argv[++i], &tmp)) threshold = tmp;
        else if (streq(argv[i], "-s") && i + 1 < argc && atoi_(argv[++i], &tmp)) { seed_in = tmp; mode_random = 1; mode_input = 0; }
        else if (streq(argv[i], "-q")) skip_seq = 1;
        else if (streq(argv[i], "-d")) { mode_input = 1; mode_random = 0; }
        else {
            pstr(2, "Usage: ");
            pstr(2, argv[0]);
            pstr(2, " [-n size] [-t threads] [-m threshold] [-s seed | -d] [-q]\n");
            return 1;
        }
    }
    if (n <= 0 || max_threads <= 0 || threshold < 1) {
        pstr(2, "Error: invalid parameters\n");
        return 1;
    }
    g_max_threads = max_threads;

    int *arr_seq = NULL;
    int *arr_par = NULL;
    double ts = 0.0;
    double tp = 0.0;

    if (mode_input) {
        char line[LINE_BUF];
        pstr(1, "Enter array (numbers separated by spaces):\n");
        int len = rline(0, line, LINE_BUF);
        if (len <= 0) {
            pstr(2, "Error: empty input\n");
            return 1;
        }
        int count = 0;
        int capacity = 1024;
        int *tmp_arr = malloc((size_t)capacity * sizeof(int));
        if (!tmp_arr) {
            pstr(2, "Error: malloc failed\n");
            return 1;
        }
        int i = 0;
        while (line[i]) {
            while (line[i] == ' ' || line[i] == '\t' || line[i] == '\n' || line[i] == '\r') i++;
            if (!line[i]) break;
            int val = 0;
            int sign = 1;
            if (line[i] == '-') { sign = -1; i++; }
            int start = i;
            while (line[i] >= '0' && line[i] <= '9') {
                val = val * 10 + (line[i] - '0');
                i++;
            }
            if (i == start) {
                pstr(2, "Error: invalid number in input\n");
                free(tmp_arr);
                return 1;
            }
            if (count >= capacity) {
                capacity *= 2;
                int *new_arr = malloc((size_t)capacity * sizeof(int));
                if (!new_arr) {
                    pstr(2, "Error: malloc failed\n");
                    free(tmp_arr);
                    return 1;
                }
                for (int k = 0; k < count; k++) new_arr[k] = tmp_arr[k];
                free(tmp_arr);
                tmp_arr = new_arr;
            }
            tmp_arr[count++] = sign * val;
        }
        if (count == 0) {
            pstr(2, "Error: no numbers in input\n");
            free(tmp_arr);
            return 1;
        }
        n = count;

        arr_seq = malloc((size_t)n * sizeof(int));
        arr_par = malloc((size_t)n * sizeof(int));
        if (!arr_seq || !arr_par) {
            pstr(2, "Error: malloc failed\n");
            free(tmp_arr);
            if (arr_seq) free(arr_seq);
            if (arr_par) free(arr_par);
            return 1;
        }
        for (int k = 0; k < n; k++) {
            arr_seq[k] = tmp_arr[k];
            arr_par[k] = tmp_arr[k];
        }
        free(tmp_arr);

        pstr(1, "Input array:\n");
        parr(1, arr_seq, n);

        double t0 = nowms();
        qsort_seq(arr_seq, 0, n - 1);
        double t1 = nowms();
        ts = t1 - t0;

        if (!sorted(arr_seq, n)) {
            pstr(2, "Error: sequential sort failed\n");
            return 2;
        }
    } else {
        unsigned int seed = (unsigned int)seed_in;

        if (!skip_seq) {
            arr_seq = gen_arr(n, seed);
            double t0 = nowms();
            qsort_seq(arr_seq, 0, n - 1);
            double t1 = nowms();
            ts = t1 - t0;

            if (!sorted(arr_seq, n)) {
                pstr(2, "Error: sequential sort failed\n");
                return 2;
            }
        }
        arr_par = gen_arr(n, seed);
    }

    pthread_mutex_lock(&g_mutex);
    g_active_threads = 0;
    pthread_mutex_unlock(&g_mutex);

    double t2 = nowms();
    qsort_par(arr_par, 0, n - 1, threshold);
    double t3 = nowms();
    tp = t3 - t2;

    if (!sorted(arr_par, n)) {
        pstr(2, "Error: parallel sort failed\n");
        return 3;
    }

    if (mode_input) {
        pstr(1, "Sorted array:\n");
        parr(1, arr_par, n);
    }

    double speedup = (ts > 0.0) ? (ts / tp) : 0.0;
    double efficiency = (speedup > 0.0) ? (speedup / (double)max_threads) : 0.0;

    pstr(1, "n=");
    pint(1, n);
    pstr(1, "max_threads=");
    pint(1, max_threads);
    pstr(1, "threshold=");
    pint(1, threshold);
    pstr(1, "sequential_ms=");
    pint(1, (long)(ts * 1000));
    pstr(1, "parallel_ms=");
    pint(1, (long)(tp * 1000));
    pstr(1, "speedup_x1000=");
    pint(1, (long)(speedup * 1000));
    pstr(1, "efficiency_x1000=");
    pint(1, (long)(efficiency * 1000));

    if (arr_seq) free(arr_seq);
    if (arr_par) free(arr_par);
    return 0;
}
