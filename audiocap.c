#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dlfcn.h>
#include <time.h>

typedef void snd_pcm_t;
static int (*p_open)(snd_pcm_t **, const char *, int, int);
static int (*p_set_params)(snd_pcm_t *, int, int, unsigned, unsigned, int, unsigned);
static long (*p_readi)(snd_pcm_t *, void *, unsigned long);
static int (*p_close)(snd_pcm_t *);
static int (*p_prepare)(snd_pcm_t *);
static const char *(*p_strerror)(int);

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    const char *dev = argc > 1 ? argv[1] : "plughw:2,0";
    int seconds = argc > 2 ? atoi(argv[2]) : 60;

    void *h = dlopen("libasound.so.2", RTLD_NOW);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    p_open      = dlsym(h, "snd_pcm_open");
    p_set_params= dlsym(h, "snd_pcm_set_params");
    p_readi     = dlsym(h, "snd_pcm_readi");
    p_close     = dlsym(h, "snd_pcm_close");
    p_prepare   = dlsym(h, "snd_pcm_prepare");
    p_strerror  = dlsym(h, "snd_strerror");
    if (!p_open || !p_set_params || !p_readi || !p_close || !p_prepare || !p_strerror) {
        fprintf(stderr, "dlsym: open=%p set_params=%p readi=%p close=%p prepare=%p strerror=%p\n",
                (void *)p_open, (void *)p_set_params, (void *)p_readi,
                (void *)p_close, (void *)p_prepare, (void *)p_strerror);
        return 1;
    }

    snd_pcm_t *pcm;
    /* stream 1 = CAPTURE, mode 0 = blocking */
    int err = p_open(&pcm, dev, 1, 0);
    if (err < 0) { fprintf(stderr, "open %s: %s\n", dev, p_strerror(err)); return 1; }

    /* format 2 = S16_LE, access 3 = RW_INTERLEAVED, 1 ch, 48 kHz, resample=1, 500 ms latency */
    err = p_set_params(pcm, 2, 3, 1, 48000, 1, 500000);
    if (err < 0) { fprintf(stderr, "set_params: %s\n", p_strerror(err)); return 1; }
    printf("audio: capturing from %s (mono 48kHz)\n", dev);

    short buf[1024];
    double start = now(), last = start, lasterr = 0;
    int consec = 0;
    long total = 0;
    while (now() - start < seconds) {
        long n = p_readi(pcm, buf, 1024);
        if (n < 0) {
            if (now() - lasterr > 1.0) {
                fprintf(stderr, "audio readi: %s (retrying)\n", p_strerror((int)n));
                lasterr = now();
            }
            if (++consec >= 20) {   /* reopen like a real audio stack would */
                p_close(pcm);
                if (p_open(&pcm, dev, 1, 0) < 0) return 1;
                p_set_params(pcm, 2, 3, 1, 48000, 1, 500000);
                consec = 0;
                continue;
            }
            p_prepare(pcm);
            continue;
        }
        consec = 0;
        total += n;
        if (now() - last >= 5) {
            printf("audio: %.0fs, %ld frames\n", now() - start, total);
            fflush(stdout);
            last = now();
        }
    }
    p_close(pcm);
    printf("AUDIO OK (%ld frames in %ds)\n", total, seconds);
    return 0;
}
