#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdlib.h>

#define INT_BUF 32

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_active_threads = 0;
static int g_max_threads = 1;

typedef struct {
    int *arr;
    int left;
    int right;
    int threshold;
} task_t;

static int my_strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void print_str(int fd, const char *s) {
    write(fd, s, my_strlen(s));
}

static void print_int(int fd, long v) {
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

static int my_atoi(const char *s, int *out) {
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

static int str_eq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static void swap_int(int *a, int *b) {
    int t = *a;
    *a = *b;
    *b = t;
}

static int partition(int *arr, int left, int right) {
    int mid = left + (right - left) / 2;
    if (arr[mid] < arr[left])   swap_int(&arr[mid], &arr[left]);
    if (arr[right] < arr[left]) swap_int(&arr[right], &arr[left]);
    if (arr[right] < arr[mid])  swap_int(&arr[right], &arr[mid]);
    int pivot = arr[mid];
    swap_int(&arr[mid], &arr[right]);

    int i = left - 1;
    for (int j = left; j < right; j++) {
        if (arr[j] <= pivot) {
            i++;
            swap_int(&arr[i], &arr[j]);
        }
    }
    swap_int(&arr[i + 1], &arr[right]);
    return i + 1;
}

static void seq_quicksort(int *arr, int left, int right) {
    while (left < right) {
        int pivot = partition(arr, left, right);
        if (pivot - left < right - pivot) {
            seq_quicksort(arr, left, pivot - 1);
            left = pivot + 1;
        } else {
            seq_quicksort(arr, pivot + 1, right);
            right = pivot - 1;
        }
    }
}

static void paral_quicksort(int *arr, int left, int right, int threshold);

static void *worker(void *arg) {
    task_t *t = (task_t *)arg;
    paral_quicksort(t->arr, t->left, t->right, t->threshold);
    free(t);
    return NULL;
}

static void paral_quicksort(int *arr, int left, int right, int threshold) {
    if (right - left < threshold) {
        seq_quicksort(arr, left, right);
        return;
    }

    int pivot = partition(arr, left, right);

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
            paral_quicksort(arr, left, pivot - 1, threshold);
            paral_quicksort(arr, pivot + 1, right, threshold);
            return;
        }
        t->arr = arr;
        t->left = left;
        t->right = pivot - 1;
        t->threshold = threshold;

        pthread_t tid;
        if (pthread_create(&tid, NULL, worker, t) == 0) {
            paral_quicksort(arr, pivot + 1, right, threshold);
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

    paral_quicksort(arr, left, pivot - 1, threshold);
    paral_quicksort(arr, pivot + 1, right, threshold);
}

static int *generate_array(int n, unsigned int seed) {
    int *arr = malloc((size_t)n * sizeof(int));
    if (!arr) {
        print_str(2, "Error: malloc failed\n");
        _exit(1);
    }
    srand(seed);
    for (int i = 0; i < n; i++) {
        arr[i] = rand() - RAND_MAX / 2;
    }
    return arr;
}

static int is_sorted(const int *arr, int n) {
    for (int i = 1; i < n; i++)
        if (arr[i - 1] > arr[i]) return 0;
    return 1;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

int main(int argc, char **argv) {
    int n = 1000000;
    int max_threads = 4;
    int threshold = 10000;
    int seed_in = 42;
    int skip_seq = 0;
    int tmp = 0;

    for (int i = 1; i < argc; i++) {
        if (str_eq(argv[i], "-n") && i + 1 < argc && my_atoi(argv[++i], &tmp)) n = tmp;
        else if (str_eq(argv[i], "-t") && i + 1 < argc && my_atoi(argv[++i], &tmp)) max_threads = tmp;
        else if (str_eq(argv[i], "-m") && i + 1 < argc && my_atoi(argv[++i], &tmp)) threshold = tmp;
        else if (str_eq(argv[i], "-s") && i + 1 < argc && my_atoi(argv[++i], &tmp)) seed_in = tmp;
        else if (str_eq(argv[i], "-q")) skip_seq = 1;
        else {
            print_str(2, "Usage: play3 [-n size] [-t threads] [-m threshold] [-s seed] [-q]\n");
            return 1;
        }
    }
    if (n <= 0 || max_threads <= 0 || threshold < 1) {
        print_str(2, "Error: invalid parameters\n");
        return 1;
    }
    g_max_threads = max_threads;

    unsigned int seed = (unsigned int)seed_in;

    double ts = 0.0;
    int *arr_seq = NULL;

    if (!skip_seq) {
        arr_seq = generate_array(n, seed);
        double t0 = now_ms();
        seq_quicksort(arr_seq, 0, n - 1);
        double t1 = now_ms();
        ts = t1 - t0;

        if (!is_sorted(arr_seq, n)) {
            print_str(2, "Error: sequential sort failed\n");
            return 2;
        }
    }

    int *arr_par = generate_array(n, seed);

    pthread_mutex_lock(&g_mutex);
    g_active_threads = 0;
    pthread_mutex_unlock(&g_mutex);

    double t2 = now_ms();
    paral_quicksort(arr_par, 0, n - 1, threshold);
    double t3 = now_ms();
    double tp = t3 - t2;

    if (!is_sorted(arr_par, n)) {
        print_str(2, "Error: parallel sort failed\n");
        return 3;
    }

    double speedup = (ts > 0.0) ? (ts / tp) : 0.0;
    double efficiency = (speedup > 0.0) ? (speedup / (double)max_threads) : 0.0;

    print_str(1, "n=");
    print_int(1, n);
    print_str(1, "max_threads=");
    print_int(1, max_threads);
    print_str(1, "threshold=");
    print_int(1, threshold);
    print_str(1, "sequential_ms=");
    print_int(1, (long)(ts * 1000));
    print_str(1, "parallel_ms=");
    print_int(1, (long)(tp * 1000));
    print_str(1, "speedup_x1000=");
    print_int(1, (long)(speedup * 1000));
    print_str(1, "efficiency_x1000=");
    print_int(1, (long)(efficiency * 1000));

    free(arr_seq);
    free(arr_par);
    return 0;
}
