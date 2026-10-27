/* keyinject — 用 uinput 伪造一个按键设备并按下指定键码, 仅用于真机自测
 * 用法: keyinject <code> <waitSeconds>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/uinput.h>

static void emit(int fd, int type, int code, int val) {
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type; ev.code = code; ev.value = val;
    write(fd, &ev, sizeof ev);
}

int main(int argc, char **argv) {
    int code = argc > 1 ? atoi(argv[1]) : 304;
    int wait = argc > 2 ? atoi(argv[2]) : 5;
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) { perror("open /dev/uinput"); return 1; }
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_KEYBIT, code);

    struct uinput_user_dev u;
    memset(&u, 0, sizeof u);
    snprintf(u.name, sizeof u.name, "radar-test-pad");
    u.id.bustype = BUS_USB;
    u.id.vendor = 0x1234;
    u.id.product = 0x5678;
    u.id.version = 1;
    if (write(fd, &u, sizeof u) != sizeof u) { perror("write uinput"); return 1; }
    if (ioctl(fd, UI_DEV_CREATE) < 0) { perror("UI_DEV_CREATE"); return 1; }
    printf("virtual pad created, key=%d, wait=%ds\n", code, wait);
    fflush(stdout);

    sleep(wait);
    emit(fd, EV_KEY, code, 1);
    emit(fd, EV_SYN, SYN_REPORT, 0);
    usleep(120000);
    emit(fd, EV_KEY, code, 0);
    emit(fd, EV_SYN, SYN_REPORT, 0);
    usleep(200000);
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    printf("pressed %d and destroyed\n", code);
    return 0;
}
