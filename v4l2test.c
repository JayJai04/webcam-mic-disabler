#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <linux/videodev2.h>

static int xioctl(int fd, unsigned long req, void *arg) {
    int r;
    do { r = ioctl(fd, req, arg); } while (r < 0 && errno == EINTR);
    return r;
}

static void fourcc(char *out, unsigned f) {
    out[0] = f & 0xff; out[1] = (f >> 8) & 0xff;
    out[2] = (f >> 16) & 0xff; out[3] = (f >> 24) & 0xff;
    out[4] = 0;
}

int main(int argc, char **argv) {
    const char *dev = argc > 1 ? argv[1] : "/dev/video1";
    int width  = argc > 2 ? atoi(argv[2]) : 640;
    int height = argc > 3 ? atoi(argv[3]) : 480;
    unsigned want = (argc > 4 && strcmp(argv[4], "YUYV") == 0)
                    ? V4L2_PIX_FMT_YUYV : V4L2_PIX_FMT_MJPEG;
    int nframes = argc > 5 ? atoi(argv[5]) : 5;
    int fd = open(dev, O_RDWR | O_NONBLOCK);
    if (fd < 0) { perror("open"); return 1; }

    struct v4l2_capability cap;
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) { perror("QUERYCAP"); return 1; }
    printf("driver=%s card=%s bus=%s\n", cap.driver, cap.card, cap.bus_info);

    printf("formats:\n");
    for (unsigned i = 0; ; i++) {
        struct v4l2_fmtdesc f = { .index = i, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE };
        if (xioctl(fd, VIDIOC_ENUM_FMT, &f) < 0) break;
        char cc[5]; fourcc(cc, f.pixelformat);
        printf("  %u: %-16s [%s]\n", i, f.description, cc);
    }

    const char *pnames[] = { "MJPEG", "YUYV" };
    unsigned pixfmts[2] = { V4L2_PIX_FMT_MJPEG, V4L2_PIX_FMT_YUYV };
    for (int p = 0; p < 2; p++) {
        printf("sizes for %s:\n", pnames[p]);
        for (unsigned i = 0; ; i++) {
            struct v4l2_frmsizeenum fs = { .index = i, .pixel_format = pixfmts[p] };
            if (xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fs) < 0) break;
            if (fs.type == V4L2_FRMSIZE_TYPE_DISCRETE)
                printf("  %ux%u\n", fs.discrete.width, fs.discrete.height);
            else
                printf("  stepwise %u..%u x %u..%u\n", fs.stepwise.min_width,
                       fs.stepwise.max_width, fs.stepwise.min_height, fs.stepwise.max_height);
        }
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = want;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) { perror("S_FMT(MJPEG)"); return 1; }
    char cc[5]; fourcc(cc, fmt.fmt.pix.pixelformat);
    printf("negotiated: %ux%u fmt=%s sizeimage=%u\n",
           fmt.fmt.pix.width, fmt.fmt.pix.height, cc, fmt.fmt.pix.sizeimage);

    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = 30;
    if (xioctl(fd, VIDIOC_S_PARM, &parm) < 0) perror("S_PARM");

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 4; req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) { perror("REQBUFS"); return 1; }

    struct { void *start; size_t length; } bufs[8];
    for (unsigned i = 0; i < req.count; i++) {
        struct v4l2_buffer b;
        memset(&b, 0, sizeof(b));
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; b.memory = V4L2_MEMORY_MMAP; b.index = i;
        if (xioctl(fd, VIDIOC_QUERYBUF, &b) < 0) { perror("QUERYBUF"); return 1; }
        bufs[i].length = b.length;
        bufs[i].start = mmap(NULL, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
        if (bufs[i].start == MAP_FAILED) { perror("mmap"); return 1; }
        if (xioctl(fd, VIDIOC_QBUF, &b) < 0) { perror("QBUF"); return 1; }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) { perror("STREAMON"); return 1; }
    printf("streaming... capturing %d frames\n", nframes);

    int got = 0, failures = 0;
    while (got < nframes && failures < 5) {
        fd_set fds;
        FD_ZERO(&fds); FD_SET(fd, &fds);
        struct timeval tv = { .tv_sec = 2 };
        int r = select(fd + 1, &fds, NULL, NULL, &tv);
        if (r <= 0) { fprintf(stderr, "wait: no frame within 2s\n"); failures++; continue; }
        struct v4l2_buffer b;
        memset(&b, 0, sizeof(b));
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; b.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd, VIDIOC_DQBUF, &b) < 0) {
            fprintf(stderr, "DQBUF failed: %s (errno %d)\n", strerror(errno), errno);
            failures++; continue;
        }
        if (got % 100 == 0) printf("frame %d: seq=%u bytes=%u\n", got, b.sequence, b.bytesused);
        got++;
        xioctl(fd, VIDIOC_QBUF, &b);
    }

    xioctl(fd, VIDIOC_STREAMOFF, &type);
    close(fd);
    printf("%s (%d frames)\n", got >= nframes ? "CAPTURE OK" : "CAPTURE FAILED", got);
    return got >= nframes ? 0 : 1;
}
