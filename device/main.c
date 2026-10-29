/*
 * 龙珠雷达 Dragon Radar — TrimUI Brick Pro (tg5040) 掌机端
 *
 * 屏幕 1024x768, SDL2 输出, 纯软件光栅化(自带抗锯齿), 零第三方依赖。
 * 手机端(红米K70)通过热点 TCP 9000 下发本人经纬度与龙珠列表, 本机负责
 * 绘制怀表雷达 / 滴滴提示音 / 20m 锁定长鸣 / 摇杆缩放 / 已找到清单。
 *
 * 编译(交叉): 见 build.sh   /   预览自检: 定义 PREVIEW 编译为本机程序
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef PREVIEW
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/file.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define W 1024
#define H 768

/* ---------------- 几何常量(与审过的原型一致) ---------------- */
#define CX        512.0f
#define CY        408.0f   /* 表盘整体下移: 圆的上沿落在顶部表冠(按钮造型)下方 */
#define R_OUT     354.0f   /* 上留 54px 让开表冠, 下留 6px 贴边 */
#define R_SCR     319.0f
#define KNOB_X    512.0f
#define KNOB_Y     45.0f   /* 下移: 旋钮下缘压进表壳 12px, 与表盘连成一体 */

#ifndef BTN_MODE
#define BTN_MODE  0x13c
#endif
#ifndef BTN_EAST
#define BTN_EAST  0x131
#endif
/* 掌机物理键位(实测键码, 与 linux BTN_* 命名相反, 勿混用):
 *   物理 B(下) = 304 / 物理 A(右) = 305 / 物理 X(上) = 307 / 物理 Y(左) = 308 */
#define PHYS_B    304
#define PHYS_A    305
#define PHYS_X    307
#define PHYS_Y    308
#ifndef BTN_B
#define BTN_B     0x131
#endif
#ifndef BTN_SELECT
#define BTN_SELECT 0x13a
#endif
#ifndef KEY_BACK
#define KEY_BACK  158
#endif

#define MAXBALL   7
#define FOUND_R   20.0   /* 找到半径(米) */
#define TONE_MS   5000   /* 锁定长鸣 */
#define BEEP_MS     62   /* 单次滴滴时长 */

/* ---------------- 8x16 像素字体 ---------------- */
static const unsigned char font8x16[][16] = {
    ['0' - 32] = {0x00,0x3C,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00,0x00},
    ['1' - 32] = {0x00,0x18,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00},
    ['2' - 32] = {0x00,0x3E,0x63,0x03,0x03,0x06,0x06,0x0C,0x18,0x30,0x30,0x60,0x60,0x7F,0x00,0x00},
    ['3' - 32] = {0x00,0x3E,0x63,0x03,0x03,0x06,0x1C,0x06,0x03,0x03,0x03,0x03,0x63,0x3E,0x00,0x00},
    ['4' - 32] = {0x00,0x06,0x0E,0x1E,0x36,0x66,0x66,0xC6,0x7F,0x06,0x06,0x06,0x06,0x06,0x00,0x00},
    ['5' - 32] = {0x00,0x7F,0x60,0x60,0x60,0x7E,0x03,0x03,0x03,0x03,0x03,0x03,0x63,0x3E,0x00,0x00},
    ['6' - 32] = {0x00,0x1C,0x30,0x60,0x60,0x7E,0x63,0x63,0x63,0x63,0x63,0x63,0x63,0x3E,0x00,0x00},
    ['7' - 32] = {0x00,0x7F,0x63,0x03,0x06,0x06,0x0C,0x0C,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00},
    ['8' - 32] = {0x00,0x3E,0x63,0x63,0x63,0x63,0x3E,0x3E,0x63,0x63,0x63,0x63,0x63,0x3E,0x00,0x00},
    ['9' - 32] = {0x00,0x3E,0x63,0x63,0x63,0x63,0x63,0x3F,0x03,0x03,0x03,0x06,0x0C,0x38,0x00,0x00},
    ['A' - 32] = {0x00,0x18,0x18,0x3C,0x24,0x24,0x24,0x3C,0x3C,0x42,0x42,0x42,0x7E,0x42,0x00,0x00},
    ['B' - 32] = {0x00,0x7C,0x42,0x42,0x42,0x42,0x7C,0x7C,0x42,0x42,0x42,0x42,0x42,0x7C,0x00,0x00},
    ['C' - 32] = {0x00,0x3C,0x66,0x42,0x42,0x40,0x40,0x40,0x40,0x40,0x42,0x42,0x66,0x3C,0x00,0x00},
    ['D' - 32] = {0x00,0x78,0x44,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x44,0x78,0x00,0x00},
    ['E' - 32] = {0x00,0x7E,0x40,0x40,0x40,0x40,0x7C,0x7C,0x40,0x40,0x40,0x40,0x40,0x7E,0x00,0x00},
    ['F' - 32] = {0x00,0x7E,0x40,0x40,0x40,0x40,0x7C,0x7C,0x40,0x40,0x40,0x40,0x40,0x40,0x00,0x00},
    ['G' - 32] = {0x00,0x3C,0x66,0x42,0x42,0x40,0x40,0x4E,0x42,0x42,0x42,0x42,0x66,0x3C,0x00,0x00},
    ['H' - 32] = {0x00,0x42,0x42,0x42,0x42,0x42,0x7E,0x7E,0x42,0x42,0x42,0x42,0x42,0x42,0x00,0x00},
    ['I' - 32] = {0x00,0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00},
    ['K' - 32] = {0x00,0x41,0x42,0x44,0x48,0x50,0x60,0x60,0x50,0x48,0x44,0x42,0x41,0x41,0x00,0x00},
    ['L' - 32] = {0x00,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x7E,0x00,0x00},
    ['M' - 32] = {0x00,0x42,0x66,0x66,0x5A,0x5A,0x5A,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x00,0x00},
    ['N' - 32] = {0x00,0x42,0x62,0x62,0x52,0x52,0x52,0x4A,0x4A,0x4A,0x4A,0x46,0x46,0x46,0x00,0x00},
    ['O' - 32] = {0x00,0x3C,0x66,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x66,0x3C,0x00,0x00},
    ['P' - 32] = {0x00,0x7C,0x42,0x42,0x42,0x42,0x7C,0x7C,0x40,0x40,0x40,0x40,0x40,0x40,0x00,0x00},
    ['R' - 32] = {0x00,0x7C,0x42,0x42,0x42,0x42,0x7C,0x78,0x44,0x42,0x42,0x42,0x42,0x41,0x00,0x00},
    ['S' - 32] = {0x00,0x3E,0x63,0x60,0x60,0x60,0x3E,0x0F,0x03,0x03,0x03,0x03,0x63,0x3E,0x00,0x00},
    ['T' - 32] = {0x00,0x7F,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00},
    ['U' - 32] = {0x00,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x66,0x3C,0x00,0x00},
    ['V' - 32] = {0x00,0x42,0x42,0x42,0x42,0x42,0x42,0x66,0x66,0x3C,0x3C,0x18,0x18,0x18,0x00,0x00},
    ['W' - 32] = {0x00,0x42,0x42,0x42,0x42,0x42,0x5A,0x5A,0x5A,0x5A,0x66,0x66,0x66,0x42,0x00,0x00},
    ['X' - 32] = {0x00,0x42,0x66,0x66,0x3C,0x3C,0x18,0x18,0x3C,0x3C,0x66,0x66,0x42,0x42,0x00,0x00},
    ['Y' - 32] = {0x00,0x42,0x42,0x66,0x66,0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00},
    ['Z' - 32] = {0x00,0x7F,0x03,0x06,0x0C,0x18,0x18,0x18,0x30,0x60,0x60,0x60,0x40,0x7F,0x00,0x00},
    [' ' - 32] = {0},
    ['.' - 32] = {0,0,0,0,0,0,0,0,0,0,0,0,0x18,0x18,0x00,0x00},
    [':' - 32] = {0,0,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
    ['%' - 32] = {0x00,0x60,0x61,0x03,0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x41,0x43,0x03,0x00,0x00},
    ['/' - 32] = {0x00,0x03,0x03,0x06,0x06,0x06,0x0C,0x0C,0x18,0x18,0x30,0x30,0x30,0x60,0x00,0x00},
    ['-' - 32] = {0,0,0,0,0,0,0x7E,0x7E,0,0,0,0,0,0,0,0},
    ['=' - 32] = {0,0,0,0,0,0x7E,0x7E,0,0,0x7E,0x7E,0,0,0,0,0},
    ['+' - 32] = {0,0,0,0,0x18,0x18,0x7E,0x7E,0x18,0x18,0,0,0,0,0,0},
    ['(' - 32] = {0x00,0x0E,0x18,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x18,0x0E,0x00,0x00},
    [')' - 32] = {0x00,0x70,0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x18,0x70,0x00,0x00},
    [128 - 32] = {0x00,0x0C,0x1E,0x33,0x30,0x30,0x30,0x30,0x30,0x33,0x1E,0x0C,0x00,0x00,0x00,0x00}, /* 度符号° */
    [129 - 32] = {0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0xFF,0x7E,0x3C,0x18,0x00,0x00}, /* 下箭头↓ */
    [130 - 32] = {0x00,0x18,0x3C,0x7E,0xFF,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00}, /* 上箭头↑ */
};

/* ---------------- 颜色 ---------------- */
#define C_BLACK   0xFF000000u
static const uint32_t C_SHELL      = 0xFFEFEFEFu;
static const uint32_t C_SHELL_LINE = 0xFF707070u;
static const uint32_t C_SHELL_RING = 0xFFBCBCBCu;
static const uint32_t C_SHELL_DARK = 0xFF969696u;
static const uint32_t C_SCR        = 0xFF009900u;
static const uint32_t C_SCR_EDGE   = 0xFF006B00u;
static const uint32_t C_SCR_HI     = 0xFF0AA70Au;
static const uint32_t C_GRID       = 0xFF003300u;
static const uint32_t C_DOT        = 0xFFFFFFCCu;
static const uint32_t C_DOT_GLOW   = 0xFFFFFF66u;
static const uint32_t C_LABEL      = 0xFFFFFF66u;
static const uint32_t C_TRI        = 0xFFCC0000u;
static const uint32_t C_HUD        = 0xFF8DFFBEu;
static const uint32_t C_HUD_DIM    = 0xFF96D7B9u;
static const uint32_t C_HUD_FAINT  = 0xFF7FAF97u;
static const uint32_t C_BIG        = 0xFF4DFF9Au;
static const uint32_t C_ONLINE     = 0xFF3DFF8Bu;
static const uint32_t C_OFFLINE    = 0xFFFF4040u;   /* 断链警示红 */

/* ---------------- 帧缓冲与图元 ---------------- */
static uint32_t FB[W * H];
static uint32_t BG[W * H];
static uint32_t *gTarget = FB;

/* 圆形裁剪(屏幕区) */
static float gClipX = 0, gClipY = 0, gClipR2 = 0;

static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline int   clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }
static inline uint32_t RGBA(int r, int g, int b) {
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static inline void px(int x, int y, uint32_t c, float a) {
    if (a <= 0.003f) return;
    if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
    if (gClipR2 > 0) {
        float dx = (float)x + 0.5f - gClipX, dy = (float)y + 0.5f - gClipY;
        if (dx * dx + dy * dy > gClipR2) return;
    }
    uint32_t *p = &gTarget[y * W + x];
    if (a >= 0.997f) { *p = c; return; }
    uint32_t d = *p;
    int dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
    int sr = (c >> 16) & 255, sg = (c >> 8) & 255, sb = c & 255;
    int r = (int)(dr + (sr - dr) * a + 0.5f);
    int g = (int)(dg + (sg - dg) * a + 0.5f);
    int b = (int)(db + (sb - db) * a + 0.5f);
    *p = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void fillCircle(float cx, float cy, float r, uint32_t c, float a) {
    int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1);
    int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
            float cov = clampf(r + 0.5f - sqrtf(dx * dx + dy * dy), 0.0f, 1.0f);
            if (cov > 0) px(x, y, c, a * cov);
        }
}

static void strokeCircle(float cx, float cy, float r, float w, uint32_t c, float a) {
    float half = w * 0.5f, out = r + half + 1;
    int x0 = (int)floorf(cx - out), x1 = (int)ceilf(cx + out);
    int y0 = (int)floorf(cy - out), y1 = (int)ceilf(cy + out);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy);
            float cov = clampf(half + 0.5f - fabsf(d - r), 0.0f, 1.0f);
            if (cov > 0) px(x, y, c, a * cov);
        }
}

/* 水平/垂直线(格子线), 只做单方向抗锯齿, 端点由裁剪圆修掉 */
static void hline(float x0, float x1, float y, float w, uint32_t c, float a) {
    int ix0 = (int)floorf(x0), ix1 = (int)ceilf(x1);
    int y0 = (int)floorf(y - w * 0.5f - 1), y1 = (int)ceilf(y + w * 0.5f + 1);
    for (int yy = y0; yy <= y1; yy++) {
        float cov = clampf(w * 0.5f + 0.5f - fabsf((float)yy + 0.5f - y), 0.0f, 1.0f);
        if (cov <= 0) continue;
        for (int x = ix0; x <= ix1; x++) px(x, yy, c, a * cov);
    }
}
static void vline(float x, float y0, float y1, float w, uint32_t c, float a) {
    int iy0 = (int)floorf(y0), iy1 = (int)ceilf(y1);
    int x0 = (int)floorf(x - w * 0.5f - 1), x1 = (int)ceilf(x + w * 0.5f + 1);
    for (int xx = x0; xx <= x1; xx++) {
        float cov = clampf(w * 0.5f + 0.5f - fabsf((float)xx + 0.5f - x), 0.0f, 1.0f);
        if (cov <= 0) continue;
        for (int y = iy0; y <= iy1; y++) px(xx, y, c, a * cov);
    }
}

/* 任意方向粗线: 沿线段撒小圆, 避免大包围盒逐像素测试 */
static void lineAA(float x0, float y0, float x1, float y1, float w, uint32_t c, float a) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.5f) return;
    int steps = (int)(len / 0.5f) + 1;
    float r = w * 0.5f;
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        fillCircle(x0 + dx * t, y0 + dy * t, r, c, a);
    }
}

static void triFill(float ax, float ay, float bx, float by, float cx, float cy,
                    uint32_t col, float alpha) {
    float minx = fminf(ax, fminf(bx, cx)) - 1, maxx = fmaxf(ax, fmaxf(bx, cx)) + 1;
    float miny = fminf(ay, fminf(by, cy)) - 1, maxy = fmaxf(ay, fmaxf(by, cy)) + 1;
    for (int y = (int)floorf(miny); y <= (int)ceilf(maxy); y++)
        for (int x = (int)floorf(minx); x <= (int)ceilf(maxx); x++) {
            int hit = 0;
            for (int sy = 0; sy < 2; sy++)
                for (int sx = 0; sx < 2; sx++) {
                    float sx0 = (float)x + 0.25f + sx * 0.5f;
                    float sy0 = (float)y + 0.25f + sy * 0.5f;
                    float d1 = (bx - ax) * (sy0 - ay) - (by - ay) * (sx0 - ax);
                    float d2 = (cx - bx) * (sy0 - by) - (cy - by) * (sx0 - bx);
                    float d3 = (ax - cx) * (sy0 - cy) - (ay - cy) * (sx0 - cx);
                    if ((d1 >= 0 && d2 >= 0 && d3 >= 0) || (d1 <= 0 && d2 <= 0 && d3 <= 0)) hit++;
                }
            if (hit) px(x, y, col, alpha * (hit * 0.25f));
        }
}

/* 旋转后的四边形(自动拆两个三角形) */
static void quadFill(float cx, float cy, float ang,
                     float px0, float py0, float px1, float py1,
                     float px2, float py2, float px3, float py3,
                     uint32_t col, float alpha) {
    float ca = cosf(ang), sa = sinf(ang);
    float xs[4], ys[4];
    float rx[4] = {px0, px1, px2, px3}, ry[4] = {py0, py1, py2, py3};
    for (int i = 0; i < 4; i++) {
        xs[i] = cx + rx[i] * ca - ry[i] * sa;
        ys[i] = cy + rx[i] * sa + ry[i] * ca;
    }
    triFill(xs[0], ys[0], xs[1], ys[1], xs[2], ys[2], col, alpha);
    triFill(xs[0], ys[0], xs[2], ys[2], xs[3], ys[3], col, alpha);
}

/* 直角矩形 */
static void rectFill(float x, float y, float w, float h, uint32_t c, float a) {
    for (int yy = (int)floorf(y); yy < (int)ceilf(y + h); yy++)
        for (int xx = (int)floorf(x); xx < (int)ceilf(x + w); xx++) {
            float cx = clampf((float)xx + 1 - x, 0, 1) * clampf(x + w - (float)xx, 0, 1);
            float cy = clampf((float)yy + 1 - y, 0, 1) * clampf(y + h - (float)yy, 0, 1);
            float cov = cx * cy;
            if (cov > 0) px(xx, yy, c, a * cov);
        }
}

/* ---------------- 文字(8x16 位图, 整数放大) ---------------- */
static int txtW(const char *s, int sc) { return (int)strlen(s) * 8 * sc; }

static void txt(float x, float y, const char *s, int sc, uint32_t col, float a) {
    int ox = (int)x, oy = (int)y;
    for (const char *p = s; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch < 32 || ch > 159) ch = ' ';
        const unsigned char *g = font8x16[ch - 32];
        for (int row = 0; row < 16; row++) {
            unsigned char bits = g[row];
            if (!bits) continue;
            for (int col2 = 0; col2 < 8; col2++) {
                if (!(bits & (0x80 >> col2))) continue;
                int bx = ox + col2 * sc, by = oy + row * sc;
                for (int yy = 0; yy < sc; yy++)
                    for (int xx = 0; xx < sc; xx++) px(bx + xx, by + yy, col, a);
            }
        }
        ox += 8 * sc;
    }
}
static void txtR(float xRight, float y, const char *s, int sc, uint32_t col, float a) {
    txt(xRight - txtW(s, sc), y, s, sc, col, a);
}
static void txtShadow(float x, float y, const char *s, int sc, uint32_t col, float a,
                      uint32_t glow, float galpha) {
    /* 廉价辉光: 四向偏移叠加 */
    txt(x - 2, y - 2, s, sc, glow, galpha);
    txt(x + 2, y - 2, s, sc, glow, galpha);
    txt(x - 2, y + 2, s, sc, glow, galpha);
    txt(x + 2, y + 2, s, sc, glow, galpha);
    txt(x, y, s, sc, col, a);
}

/* ================= 运行状态 ================= */
typedef struct {
    int    id;
    double lat, lng;
    int    found;
} Ball;

static Ball   balls[MAXBALL];
static int    nballs = 0;
static double meLat = 39.908700, meLng = 116.397500;
static int    haveMe = 0;
static float  meAcc = 0, meHdg = 0;
static int    haveHdg = 0;

/* 量程档位: 20/50/100/200/500 m -> 1km -> 2km (远远看整片区域) */
#define ZOOMN 7
static const int  ZOOMS[ZOOMN] = {20, 50, 100, 200, 500, 1000, 2000};
static int   zi = 3;
static float rngNow = 200.0f;

static long  lastBeep = 0;
static float flash = 0.0f;
/* 滴滴同步闪烁: 记录上一次滴滴的时刻与当前滴滴间隔, 龙珠亮度按这个节拍呼吸 */
static long  blinkT = -1;
static long  blinkPeriod = 600;
static long  bannerT = 0;
static int   bannerNum = 0;
static int   showGrid = 1;
static int   showHud  = 1;     /* 物理 A: 开关四角文字(网格保留) */

static int   linkOk = 0;
static char  phoneIp[64] = "";
static float gpsAccShow = 0;

/* 找到顺序 */
static int   foundSeq[MAXBALL];
static int   nfoundSeq = 0;

#ifdef PREVIEW
static long gFakeNow = 0;
static long nowMs(void) { return gFakeNow; }
#else
static long nowMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
#endif

static inline double rangeNow(void) { return rngNow; }

/* ---- 地理换算(局部平面近似, 10km 内误差可忽略) ---- */
static void relMeters(double lat, double lng, double *ox, double *oy) {
    double k = cos(meLat * M_PI / 180.0);
    *ox = (lng - meLng) * 111320.0 * k;
    *oy = (lat - meLat) * 110540.0;
}
static void offsetLL(double *lat, double *lng, double dx, double dy) {
    *lat += dy / 110540.0;
    *lng += dx / (111320.0 * cos(meLat * M_PI / 180.0));
}
static double ballDist(const Ball *b) {
    double x, y;
    relMeters(b->lat, b->lng, &x, &y);
    return sqrt(x * x + y * y);
}
static int nearestIdx(double *outDist) {
    int best = -1;
    double bd = 1e18;
    for (int i = 0; i < nballs; i++) {
        if (balls[i].found) continue;
        double d = ballDist(&balls[i]);
        if (d < bd) { bd = d; best = i; }
    }
    if (outDist) *outDist = best < 0 ? 0 : bd;
    return best;
}

/* 格子边长: 每档量程一个"漂亮"的米数, 且量程越大格子越小 -> 网格越密
 * (pxm = 313/rng, 对应屏幕格宽 78/63/47/39/31/28/25 px, 格数 8~25 格) */
static const int CELL_BY_ZOOM[ZOOMN] = {5, 10, 15, 25, 50, 90, 160};
static int cellMeters(double rng) {
    int best = 0;
    double bd = 1e18;
    for (int i = 0; i < ZOOMN; i++) {
        double d = fabs(log(rng / (double)ZOOMS[i]));   /* 对数距离取最近档 */
        if (d < bd) { bd = d; best = i; }
    }
    return CELL_BY_ZOOM[best];
}

/* ================= 静态背景(外壳 + 绿屏底) 预烘焙 ================= */
static void bakeBackground(void) {
    gTarget = BG;
    gClipR2 = 0;

    for (int i = 0; i < W * H; i++) BG[i] = C_BLACK;

    /* 中心幽绿辉光 */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float dx = (float)x + 0.5f - CX, dy = (float)y + 0.5f - CY;
            float d = sqrtf(dx * dx + dy * dy);
            if (d >= 680.0f) continue;
            float t = d / 680.0f, a;
            if (t < 0.55f) a = 0.55f + (0.35f - 0.55f) * (t / 0.55f);
            else           a = 0.35f * (1.0f - (t - 0.55f) / 0.45f);
            px(x, y, RGBA(20, 46, 32), a);
        }

    /* 表壳 */
    fillCircle(CX, CY, R_OUT, C_SHELL, 1);
    strokeCircle(CX, CY, R_OUT - 2, 4.0f, C_SHELL_LINE, 1);
    strokeCircle(CX, CY, R_OUT - 13, 9.0f, C_SHELL_RING, 1);
    strokeCircle(CX, CY, R_SCR + 5, 11.0f, C_SHELL_DARK, 1);

    /* 绿屏径向渐变 */
    for (int y = (int)(CY - R_SCR - 1); y <= (int)(CY + R_SCR + 1); y++)
        for (int x = (int)(CX - R_SCR - 1); x <= (int)(CX + R_SCR + 1); x++) {
            float dx = (float)x + 0.5f - CX, dy = (float)y + 0.5f - CY;
            float d = sqrtf(dx * dx + dy * dy);
            if (d > R_SCR + 0.5f) continue;
            float cov = clampf(R_SCR + 0.5f - d, 0.0f, 1.0f);
            float t = clampf((d - 25.0f) / (R_SCR - 25.0f), 0.0f, 1.0f);
            int r, g, b;
            if (t < 0.72f) {
                float k = t / 0.72f;
                r = (int)(10 + (0 - 10) * k);
                g = (int)(167 + (153 - 167) * k);
                b = (int)(10 + (0 - 10) * k);
            } else {
                float k = (t - 0.72f) / 0.28f;
                r = 0;
                g = (int)(153 + (107 - 153) * k);
                b = 0;
            }
            px(x, y, RGBA(r, g, b), cov);
        }

    /* 顶部旋钮(表冠): 下缘压进表壳上沿, 与表盘连成一体(无缝隙, 不留底座方块) */
    {
        float w = 104, h = 46, r = 21;
        float x = KNOB_X - w / 2, y = KNOB_Y - h / 2;
        /* 深色描边(外扩 2px): 与表壳同色填充时仍能看出旋钮轮廓 */
        {
            float o = 2.0f, ox = x - o, oy = y - o, ow = w + 2 * o, oh = h + 2 * o, orr = r + o;
            rectFill(ox + orr, oy, ow - 2 * orr, oh, C_SHELL_DARK, 1);
            rectFill(ox, oy + orr, ow, oh - 2 * orr, C_SHELL_DARK, 1);
            fillCircle(ox + orr, oy + orr, orr, C_SHELL_DARK, 1);
            fillCircle(ox + ow - orr, oy + orr, orr, C_SHELL_DARK, 1);
            fillCircle(ox + orr, oy + oh - orr, orr, C_SHELL_DARK, 1);
            fillCircle(ox + ow - orr, oy + oh - orr, orr, C_SHELL_DARK, 1);
        }
        /* 旋钮本体 */
        rectFill(x + r, y, w - 2 * r, h, C_SHELL, 1);
        rectFill(x, y + r, w, h - 2 * r, C_SHELL, 1);
        fillCircle(x + r, y + r, r, C_SHELL, 1);
        fillCircle(x + w - r, y + r, r, C_SHELL, 1);
        fillCircle(x + r, y + h - r, r, C_SHELL, 1);
        fillCircle(x + w - r, y + h - r, r, C_SHELL, 1);
        strokeCircle(KNOB_X, KNOB_Y + 2, r - 1, 2.0f, C_SHELL_LINE, 1);
        strokeCircle(KNOB_X, KNOB_Y + 2, 19.0f, 2.0f, C_SHELL_DARK, 1);
        rectFill(KNOB_X - 30, KNOB_Y + 2, 60, 11, C_SHELL_RING, 1);
    }

    gTarget = FB;
}

/* ================= 动态绘制 ================= */
static void drawGrid(float pxm) {
    int cellM = cellMeters(rngNow);
    float cellPx = (float)cellM * pxm;
    if (cellPx < 6) return;
    int n = (int)ceilf(R_SCR / cellPx);
    for (int i = -n; i <= n; i++) {
        float x1 = CX + i * cellPx;
        if (x1 > CX - R_SCR && x1 < CX + R_SCR) vline(x1, CY - R_SCR, CY + R_SCR, 3.0f, C_GRID, 1);
        float y1 = CY + i * cellPx;
        if (y1 > CY - R_SCR && y1 < CY + R_SCR) hline(CX - R_SCR, CX + R_SCR, y1, 3.0f, C_GRID, 1);
    }
}

static void drawTrackingLine(int ni, float pxm, long now) {
    if (ni < 0) return;
    double vx, vy;
    relMeters(balls[ni].lat, balls[ni].lng, &vx, &vy);
    float bx = CX + (float)vx * pxm, by = CY - (float)vy * pxm;
    float dx = bx - CX, dy = by - CY;
    float L = sqrtf(dx * dx + dy * dy);
    if (L <= 8 || L >= R_SCR - 10) return;

    /* 虚线 5/16 */
    const float dash = 5, gap = 16;
    for (float t = 0; t < L; t += dash + gap) {
        float t1 = t / L, t2 = fminf((t + dash) / L, 1.0f);
        for (float u = t1; u <= t2; u += 0.004f)
            fillCircle(CX + dx * u, CY + dy * u, 1.5f, C_DOT, 0.55f);
    }
    /* 沿线移动光点 */
    const float sp = 44;
    float off = fmodf((float)now * 0.09f, sp);
    for (float d = off; d < L; d += sp) {
        float t = d / L;
        fillCircle(CX + dx * t, CY + dy * t, 4.0f, C_DOT, 0.8f);
    }
}

/* 龙珠亮度: 在"滴滴"响的那一帧最亮, 之后随间隔快速衰减, 熄灭得很彻底 */
static float blinkPulse(long now) {
    if (blinkT < 0 || blinkPeriod <= 0) return 0.85f;
    float ph = (float)(now - blinkT) / (float)blinkPeriod;
    if (ph < 0.0f) ph = 0.0f;
    if (ph > 1.0f) ph = 1.0f;
    /* 前 15% 间隔保持全亮(和 62ms 滴滴对齐), 之后指数式跌到 0.06 */
    if (ph < 0.15f) return 1.0f;
    float k = (ph - 0.15f) / 0.85f;
    return 0.06f + 0.94f * (1.0f - k) * (1.0f - k);   /* 平方衰减, 底部接近熄灭 */
}

static void drawBalls(float pxm, long now) {
    for (int i = 0; i < nballs; i++) {
        if (balls[i].found) continue;
        double vx, vy;
        relMeters(balls[i].lat, balls[i].lng, &vx, &vy);
        float gx = CX + (float)vx * pxm, gy = CY - (float)vy * pxm;
        float dx = gx - CX, dy = gy - CY;
        float dd = sqrtf(dx * dx + dy * dy);
        int off = 0;
        if (dd > R_SCR - 26) {
            float a = atan2f(dy, dx);
            gx = CX + cosf(a) * (R_SCR - 34);
            gy = CY + sinf(a) * (R_SCR - 34);
            off = 1;
        }
        double d = ballDist(&balls[i]);
        int isFound = 0;
        float pulse = blinkPulse(now);          /* 与滴滴同频呼吸 */

        if (!off) {
            float r = 14.0f * (0.90f + 0.14f * pulse);   /* 亮的时候胀一下, 灭的时候缩一点 */
            /* 伪辉光: 多层同心圆 */
            fillCircle(gx, gy, r * 2.1f, C_DOT_GLOW, 0.05f * pulse);
            fillCircle(gx, gy, r * 1.6f, C_DOT_GLOW, 0.09f * pulse);
            fillCircle(gx, gy, r * 1.25f, C_DOT_GLOW, 0.13f * pulse);
            fillCircle(gx, gy, r, C_DOT, 0.04f + 0.96f * pulse);
            /* 掌机端不显示编号(编号只在手机地图上保留) */
        } else {
            float a = atan2f(dy, dx);
            quadFill(gx, gy, a + (float)(M_PI / 2), 0, -13, 10, 9, 0, 3, -10, 9, C_DOT, 0.42f);
        }
        (void)d;
    }
}

static void drawMe(void) {
    quadFill(CX, CY, meHdg, 0, -23, 16, 17, 0, 10, -16, 17, C_TRI, 1.0f);
}

static void drawBanner(long now) {
    if (!bannerT) return;
    long bt = now - bannerT;
    if (bt < 0 || bt >= 5200) return;
    float k = bt < 300 ? (float)bt / 300.0f : (bt > 4700 ? (float)(5200 - bt) / 500.0f : 1.0f);
    k = clampf(k, 0, 1);
    char title[32];
    snprintf(title, sizeof title, "FOUND %d-STAR", bannerNum);
    float tw = (float)txtW(title, 2) + 56;
    float bx = CX - tw / 2, by = CY + 66;
    rectFill(bx, by, tw, 72, RGBA(0, 40, 0), 0.86f * k);
    /* 闪烁边框 */
    float blink = 0.5f + 0.5f * fabsf(sinf((float)bt / 200.0f));
    hline(bx, bx + tw, by + 1.5f, 3, C_DOT, blink * k);
    hline(bx, bx + tw, by + 70.5f, 3, C_DOT, blink * k);
    vline(bx + 1.5f, by, by + 72, 3, C_DOT, blink * k);
    vline(bx + tw - 1.5f, by, by + 72, 3, C_DOT, blink * k);
    txtShadow(CX - txtW(title, 2) / 2, by + 20, title, 2, C_DOT, k, C_DOT_GLOW, 0.35f * k);
}

static void drawCorners(long now, double nd, int ni) {
    char buf[64];
    int foundCnt = 0;
    for (int i = 0; i < nballs; i++) if (balls[i].found) foundCnt++;
    int inRange = (ni >= 0 && nd <= FOUND_R);

    /* ---- 左上: 标题 + 已找到清单 ---- */
    txt(30, 18, "DRAGON RADAR", 2, C_HUD, 1);
    txt(30, 56, "BULMA TYPE", 1, C_HUD_DIM, 1);
    txt(30, 80, "DRAGON BALL SEEKER", 1, C_HUD_FAINT, 1);
    txt(30, 104, "DESIGNED BY FANFAN STUDIO", 1, C_HUD_FAINT, 0.9f);

    if (nballs) {
        snprintf(buf, sizeof buf, "FOUND %d/%d", foundCnt, nballs);
        txt(30, 128, buf, 1, C_HUD_DIM, 1);
        hline(30, 200, 148, 1.2f, RGBA(120, 200, 160), 0.30f);
        float fy = 158;
        for (int k = 0; k < nfoundSeq; k++) {
            int idx = foundSeq[k];
            if (idx < 0) continue;
            fillCircle(41, fy + 8, 5, C_DOT, 0.92f);
            snprintf(buf, sizeof buf, "%d-STAR", idx + 1);
            txt(58, fy, buf, 1, C_HUD, 1);
            fy += 24;
        }
    }

    /* ---- 右上: 链路 / GPS ---- */
    int offline = !linkOk;                   /* 未连接手机(含开机未连) */
    if (linkOk) {
        snprintf(buf, sizeof buf, "PHONE %s", phoneIp);
    } else {
        snprintf(buf, sizeof buf, "NO LINK");
    }
    /* 断链时红色放大显示(右对齐, 带 2px 红色辉光) */
    if (offline)
        txtShadow(W - 30 - txtW(buf, 2), 20, buf, 2, C_OFFLINE, 1.0f, C_OFFLINE, 0.45f);
    else
        txtR(W - 30, 22, buf, 1, C_HUD_DIM, 1);

    snprintf(buf, sizeof buf, "GPS +-%.0fM   %d/%d BALLS", meAcc, nballs, MAXBALL);
    txtR(W - 30, offline ? 64 : 46, buf, 1, C_HUD_DIM, 1);
    {
        int dotOn = (now % 1600) < 1000;
        float dx = (float)(W - 30 - txtW(buf, 1) - 16);
        float dy = offline ? 72.0f : 54.0f;
        if (offline) fillCircle(dx, dy, 5, C_OFFLINE, dotOn ? 1.0f : 0.22f);
        else         fillCircle(dx, dy, 5, C_ONLINE, dotOn ? 1.0f : 0.22f);
    }

    /* ---- 左下: 距离 (断链时显示等待提示, 避免陈旧数据误导) ---- */
    txt(30, H - 122, "NEAREST", 1, C_HUD_DIM, 1);
    if (offline) {
        snprintf(buf, sizeof buf, "--");
    } else if (ni >= 0) {
        if (nd >= 1000) snprintf(buf, sizeof buf, "%.2f KM", nd / 1000.0);
        else            snprintf(buf, sizeof buf, "%.1f M", nd);
    } else {
        snprintf(buf, sizeof buf, "--");
    }
    txtShadow(28, H - 100, buf, 4, inRange ? 0xFFFFFFFFu : C_BIG, 1, inRange ? 0xFFFFFFFFu : C_BIG, 0.18f);

    if (offline)            snprintf(buf, sizeof buf, "WAIT PHONE...");
    else if (inRange)       snprintf(buf, sizeof buf, "TARGET IN 20M - LOCKED");
    else if (ni >= 0)       snprintf(buf, sizeof buf, "TARGET %d-STAR", ni + 1);
    else                    snprintf(buf, sizeof buf, "NO BALL CONFIGURED");
    txt(30, H - 32, buf, 1, offline ? C_HUD_DIM : C_HUD_FAINT, 1);

    /* ---- 右下: 量程 ---- */
    txtR(W - 30, H - 122, "RANGE", 1, C_HUD_DIM, 1);
    if (ZOOMS[zi] >= 1000) snprintf(buf, sizeof buf, "%d KM", ZOOMS[zi] / 1000);
    else                   snprintf(buf, sizeof buf, "%d M", ZOOMS[zi]);
    txtR(W - 28, H - 104, buf, 2, C_HUD, 1);
    if (cellMeters(rngNow) >= 1000)
        snprintf(buf, sizeof buf, "1 CELL = %d KM", cellMeters(rngNow) / 1000);
    else
        snprintf(buf, sizeof buf, "1 CELL = %d M", cellMeters(rngNow));
    txtR(W - 30, H - 62, buf, 1, C_HUD_FAINT, 1);
    txtR(W - 30, H - 36, "UP/DOWN ZOOM", 1, C_HUD_FAINT, 1);
}

static void renderFrame(long now) {
    memcpy(FB, BG, sizeof(FB));
#ifdef PREVIEW
    gFakeNow = now;
#endif

    float pxm = (R_SCR - 6.0f) / rngNow;
    gClipX = CX; gClipY = CY; gClipR2 = (R_SCR + 1) * (R_SCR + 1);

    if (showGrid) drawGrid(pxm);
    double nd = 0;
    int ni = nearestIdx(&nd);

    /* 断链: 表盘和网格保留, 雷达数据(龙珠/追踪线/红三角)隐藏 */
    int offline = !linkOk;
    if (!offline) {
        drawTrackingLine(ni, pxm, now);
        drawBalls(pxm, now);
        drawMe();
        if (flash > 0.02f) {
            fillCircle(CX + R_SCR - 52, CY - R_SCR + 52, 7 + 5 * flash, C_DOT, flash);
        }
    }
    gClipR2 = 0;

    drawBanner(now);
    if (showHud) drawCorners(now, nd, ni);
}

/* ================= 声音(方波) ================= */
#ifndef PREVIEW
#include <SDL2/SDL.h>
static SDL_AudioDeviceID adev = 0;
static int  aFreq = 44100;
static volatile int aBeepLeft = 0, aBeepTotal = 0;
static double aPhase = 0;
static int  aAmp = 9000;

static void audioCB(void *ud, Uint8 *stream, int len) {
    (void)ud;
    Sint16 *out = (Sint16 *)stream;
    int n = len / 2;
    for (int i = 0; i < n; i++) {
        if (aBeepLeft > 0) {
            aPhase += 1080.0 / (double)aFreq;
            if (aPhase >= 1.0) aPhase -= 1.0;
            float env = 1.0f;
            int done = aBeepTotal - aBeepLeft;
            if (done < 350) env = (float)done / 350.0f;
            if (aBeepLeft < 350) env = (float)aBeepLeft / 350.0f;
            int v = (aPhase < 0.5) ? aAmp : -aAmp;
            out[i] = (Sint16)(v * env);
            aBeepLeft--;
        } else {
            out[i] = 0;
        }
    }
}
static void audioStart(void) {
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = audioCB;
    adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (adev == 0) { fprintf(stderr, "audio open failed: %s\n", SDL_GetError()); return; }
    aFreq = have.freq;
    SDL_PauseAudioDevice(adev, 0);
}
static void beep(void) {
    if (!adev) return;
    if (aBeepLeft > aFreq / 2) return; /* 长鸣期间不打断 */
    aBeepTotal = aBeepLeft = aFreq * BEEP_MS / 1000;
}
static void longTone(void) {
    if (!adev) return;
    aBeepTotal = aBeepLeft = aFreq * TONE_MS / 1000;
}
#else
static void audioStart(void) {}
static void beep(void) {
    static int cnt = 0;
    cnt++;
}
static void longTone(void) {}
#endif

/* ================= 状态更新 ================= */
static void updateState(float dt, long now) {
    double nd = 0;
    int ni = nearestIdx(&nd);

    /* 量程平滑过渡 */
    float target = (float)ZOOMS[zi];
    if (fabsf(rngNow - target) > 0.4f) rngNow += (target - rngNow) * clampf(dt * 9.0f, 0, 1);
    else rngNow = target;

    /* 未连接手机: 不做找到判定也不滴滴(位置和龙珠都是断链前的旧数据) */
    if (!linkOk) { flash = fmaxf(0.0f, flash - dt * 3.2f); return; }

    /* 找到判定(无回滞, 永久) */
    for (int i = 0; i < nballs; i++) {
        if (balls[i].found) continue;
        double d = ballDist(&balls[i]);
        if (d <= FOUND_R) {
            balls[i].found = 1;
            bannerT = now;
            bannerNum = i + 1;
            if (nfoundSeq < MAXBALL) foundSeq[nfoundSeq++] = i;
            longTone();
        }
    }

    /* 滴滴: 距离越近越急 */
    ni = nearestIdx(&nd);
    if (ni >= 0 && nd > FOUND_R) {
        double interval = nd * 8.0;
        if (interval < 90) interval = 90;
        if (interval > 1200) interval = 1200;
        blinkPeriod = (long)interval;     /* 闪烁周期始终跟随当前滴滴间隔 */
        if ((double)(now - lastBeep) > interval) {
            lastBeep = now;
            blinkT = now;                 /* 滴滴那一帧龙珠最亮 */
            beep();
            flash = 1.0f;
        }
    }
    flash = fmaxf(0.0f, flash - dt * 3.2f);
}

/* ================= 手机端通信 ================= */
#ifndef PREVIEW
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <net/if.h>
#include <unistd.h>

static int  sockFd = -1;
static char rxBuf[16384];
static int  rxLen = 0;
static long lastConnectTry = 0;
static long lastStatusSend = 0;
static long lastDataMs = 0;      /* 最近一次收到手机数据的时刻 */
static char devIp[64] = "";
static int  udpFd = -1;
static long lastBeacon = 0;
static char discIp[64] = "";
static int  connFail = 0;       /* 连续连接失败次数 */

/* ---- UDP 广播发现: 掌机喊话, 手机应答自己的 IP ---- */
static void udpPoll(long now) {
    if (udpFd < 0) {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd >= 0) {
            int on = 1;
            setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof on);
            int fl = fcntl(fd, F_GETFL, 0);
            fcntl(fd, F_SETFL, fl | O_NONBLOCK);
            struct sockaddr_in a;
            memset(&a, 0, sizeof a);
            a.sin_family = AF_INET;
            if (bind(fd, (struct sockaddr *)&a, sizeof a) == 0) udpFd = fd;
            else close(fd);
        }
    }
    if (udpFd < 0) return;
    if (now - lastBeacon > 1200) {
        lastBeacon = now;
        struct sockaddr_in a;
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_port = htons(9999);
        a.sin_addr.s_addr = INADDR_BROADCAST;
        sendto(udpFd, "DRAGONRADAR?", 12, 0, (struct sockaddr *)&a, sizeof a);
        /* 再发给本网段定向广播(AP 有时不转发 255.255.255.255) */
        struct ifreq ifr;
        memset(&ifr, 0, sizeof ifr);
        snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "wlan0");
        if (ioctl(udpFd, SIOCGIFBRDADDR, &ifr) == 0 &&
            ((struct sockaddr_in *)&ifr.ifr_broadaddr)->sin_addr.s_addr) {
            a.sin_addr = ((struct sockaddr_in *)&ifr.ifr_broadaddr)->sin_addr;
            sendto(udpFd, "DRAGONRADAR?", 12, 0, (struct sockaddr *)&a, sizeof a);
        }
    }
    for (;;) {
        char buf[128];
        struct sockaddr_in src;
        socklen_t sl = sizeof src;
        int n = (int)recvfrom(udpFd, buf, sizeof buf - 1, 0, (struct sockaddr *)&src, &sl);
        if (n <= 0) break;
        buf[n] = 0;
        if (strncmp(buf, "DRAGONRADAR:", 12) == 0) {
            /* 允许应答刷新: Wi-Fi/热点切换后手机 IP 会变, 旧值会锁死连接 */
            snprintf(discIp, sizeof discIp, "%s", buf + 12);
        }
    }
}

/* 读 /proc/net/route 的默认网关(热点下就是手机) */
static unsigned int defaultGateway(void) {
    FILE *f = fopen("/proc/net/route", "r");
    if (!f) return 0;
    char line[256];
    if (!fgets(line, sizeof line, f)) { fclose(f); return 0; }
    unsigned int gw = 0;
    while (fgets(line, sizeof line, f)) {
        char iface[64];
        unsigned int dest = 1, g = 0;
        if (sscanf(line, "%63s %x %x", iface, &dest, &g) == 3 && dest == 0) { gw = g; break; }
    }
    fclose(f);
    return gw;
}
static void gwToStr(unsigned int gw, char *out, size_t n) {
    snprintf(out, n, "%u.%u.%u.%u", gw & 0xFF, (gw >> 8) & 0xFF, (gw >> 16) & 0xFF, (gw >> 24) & 0xFF);
}
static void resolvePhoneIp(char *out, size_t n) {
    /* 优先级: phone_ip.txt 手动指定 > UDP 发现 > 默认网关 > 热点网段 */
    FILE *f = fopen("phone_ip.txt", "r");
    if (f) {
        char line[64] = "";
        if (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (line[0]) { snprintf(out, n, "%s", line); fclose(f); return; }
        }
        fclose(f);
    }
    if (discIp[0]) { snprintf(out, n, "%s", discIp); return; }
    unsigned int gw = defaultGateway();
    if (gw) { gwToStr(gw, out, n); return; }
    snprintf(out, n, "192.168.43.1");
}
static void netClose(void) {
    if (sockFd >= 0) { close(sockFd); sockFd = -1; }
    rxLen = 0;
    linkOk = 0;
}
static void netTryConnect(long now) {
    if (sockFd >= 0) return;
    if (now - lastConnectTry < 1500) return;
    lastConnectTry = now;
    /* 连续失败 5 次(约 8s)后清空 UDP 发现结果: 允许网关回退重新发现,
     * 防止锁死在一个不可达的 IP 上(如手机热点+流量同开时回复了移动数据地址) */
    if (connFail >= 5) { connFail = 0; discIp[0] = 0; }
    /* 每次都重新解析: UDP 发现应答可能要几秒后才到, 不能启动时锁死网关 */
    {
        char ip[64];
        resolvePhoneIp(ip, sizeof ip);
        snprintf(phoneIp, sizeof phoneIp, "%s", ip);
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return;
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(9000);
    if (inet_pton(AF_INET, phoneIp, &sa.sin_addr) != 1) { close(fd); return; }

    int rc = connect(fd, (struct sockaddr *)&sa, sizeof sa);
    if (rc == 0) {
        sockFd = fd;
        linkOk = 1;
        rxLen = 0;
        lastDataMs = now;      /* 看门狗重新计时 */
    } else if (errno == EINPROGRESS) {
        fd_set wf, ef;
        FD_ZERO(&wf); FD_ZERO(&ef);
        FD_SET(fd, &wf); FD_SET(fd, &ef);
        struct timeval tv = {1, 500000};
        int s = select(fd + 1, NULL, &wf, &ef, &tv);
        if (s > 0) {
            int err = 0;
            socklen_t el = sizeof err;
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el);
            if (err == 0) {
                sockFd = fd;
                linkOk = 1;
                rxLen = 0;
                lastDataMs = now;   /* 看门狗重新计时 */
                goto done;
            }
        }
        close(fd);
    } else {
        close(fd);
    }
done:
    if (sockFd >= 0) connFail = 0;
    else connFail++;
}

/* ---- 极简 JSON 取值 ---- */
static int jsonNum(const char *s, const char *key, double *out) {
    char pat[32];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(s, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p && *p != ':' && *p != ',' && *p != '}') p++;
    if (*p != ':') return 0;
    p++;
    while (*p == ' ') p++;
    char *end = NULL;
    double v = strtod(p, &end);
    if (end == p) return 0;
    *out = v;
    return 1;
}

static void clearAll(void) {
    nballs = 0;
    nfoundSeq = 0;
    bannerT = 0;
}

static void handleLine(char *line) {
    if (!line[0]) return;
    if (strstr(line, "\"clear\"")) {
        clearAll();
        return;
    }
    double v;
    if (jsonNum(line, "la", &v)) { meLat = v; haveMe = 1; }
    if (jsonNum(line, "lo", &v)) { meLng = v; }
    if (jsonNum(line, "acc", &v)) { meAcc = (float)v; }
    if (jsonNum(line, "hdg", &v)) { meHdg = (float)(v * M_PI / 180.0); haveHdg = 1; }
    gpsAccShow = meAcc;

    const char *bp = strstr(line, "\"balls\"");
    if (bp) {
        bp = strchr(bp, '[');
        if (bp) {
            bp++;
            Ball nb[MAXBALL];
            int cnt = 0;
            while (*bp && cnt < MAXBALL) {
                while (*bp == ' ' || *bp == ',') bp++;
                if (*bp != '[') break;
                bp++;
                double la = strtod(bp, (char **)&bp);
                while (*bp == ',' || *bp == ' ') bp++;
                double lo = strtod(bp, (char **)&bp);
                int id = -1;
                while (*bp == ',' || *bp == ' ') bp++;
                if (*bp != ']') id = (int)strtod(bp, (char **)&bp);
                while (*bp && *bp != ']') bp++;
                if (*bp == ']') bp++;
                if (id < 0) id = cnt + 1;
                /* 保留已找到状态 */
                int wasFound = 0;
                for (int i = 0; i < nballs; i++)
                    if (balls[i].id == id && balls[i].found) wasFound = 1;
                nb[cnt].id = id;
                nb[cnt].lat = la;
                nb[cnt].lng = lo;
                nb[cnt].found = wasFound;
                cnt++;
            }
            /* 重建找到顺序 */
            int seq[MAXBALL], ns = 0;
            for (int k = 0; k < nfoundSeq; k++) {
                int oldIdx = foundSeq[k];
                if (oldIdx < 0 || oldIdx >= nballs) continue;
                int oldId = balls[oldIdx].id;
                for (int i = 0; i < cnt; i++)
                    if (nb[i].id == oldId && nb[i].found) {
                        if (ns < MAXBALL) seq[ns++] = i;
                        break;
                    }
            }
            memcpy(balls, nb, sizeof(Ball) * cnt);
            nballs = cnt;
            memcpy(foundSeq, seq, sizeof(int) * ns);
            nfoundSeq = ns;
        }
    }
}

static void netPoll(long now) {
    /* 数据看门狗: 连接着却 8 秒收不到手机数据(手机被杀/RST丢失) → 强制断开重连,
     * 否则掌机会永远停留在假在线状态, 手机重开后一直"等待掌机" */
    if (sockFd >= 0 && lastDataMs > 0 && now - lastDataMs > 8000) {
        netClose();
    }
    if (sockFd < 0) udpPoll(now);
    netTryConnect(now);
    if (sockFd < 0) { linkOk = 0; return; }

    char buf[4096];
    for (;;) {
        int n = (int)recv(sockFd, buf, sizeof buf, 0);
        if (n > 0) {
            lastDataMs = now;
            if (rxLen + n < (int)sizeof rxBuf) {
                memcpy(rxBuf + rxLen, buf, n);
                rxLen += n;
            } else {
                rxLen = 0;
            }
            int start = 0;
            for (int i = 0; i < rxLen; i++) {
                if (rxBuf[i] == '\n') {
                    rxBuf[i] = 0;
                    handleLine(rxBuf + start);
                    start = i + 1;
                }
            }
            if (start > 0) {
                memmove(rxBuf, rxBuf + start, rxLen - start);
                rxLen -= start;
            }
            linkOk = 1;
        } else if (n == 0) {
            netClose();
            return;
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) { netClose(); return; }
            break;
        }
    }

    /* 回传状态: 让手机端显示"已找到"和连接情况 */
    if (now - lastStatusSend > 500) {
        lastStatusSend = now;
        char out[160];
        int p = snprintf(out, sizeof out, "{\"st\":1,\"found\":[");
        int first = 1;
        for (int i = 0; i < nballs; i++)
            if (balls[i].found) {
                p += snprintf(out + p, sizeof out - p, "%s%d", first ? "" : ",", balls[i].id);
                first = 0;
            }
        snprintf(out + p, sizeof out - p, "]}\n");
        if (send(sockFd, out, strlen(out), MSG_NOSIGNAL) < 0) netClose();
    }
}
#else
static void netPoll(long now) { (void)now; }
static void netClose(void) {}
#endif

/* ================= 预览模式: 输出 BMP 自检 ================= */
#ifdef PREVIEW
static void dumpBmp(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    int rowBytes = (W * 3 + 3) & ~3;
    int dataSize = rowBytes * H;
    unsigned char hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    int fileSize = 54 + dataSize;
    memcpy(hdr + 2, &fileSize, 4);
    int off = 54;
    memcpy(hdr + 10, &off, 4);
    int ihs = 40;
    memcpy(hdr + 14, &ihs, 4);
    int ww = W, hh = H;
    memcpy(hdr + 18, &ww, 4);
    memcpy(hdr + 22, &hh, 4);
    short planes = 1, bpp = 24;
    memcpy(hdr + 26, &planes, 2);
    memcpy(hdr + 28, &bpp, 2);
    memcpy(hdr + 34, &dataSize, 4);
    fwrite(hdr, 1, 54, f);
    unsigned char *row = calloc(rowBytes, 1);
    for (int y = H - 1; y >= 0; y--) {
        for (int x = 0; x < W; x++) {
            uint32_t c = FB[y * W + x];
            row[x * 3 + 0] = (unsigned char)(c & 255);
            row[x * 3 + 1] = (unsigned char)((c >> 8) & 255);
            row[x * 3 + 2] = (unsigned char)((c >> 16) & 255);
        }
        fwrite(row, 1, rowBytes, f);
    }
    free(row);
    fclose(f);
}

static void addBall(double east, double north, int id, int found) {
    double la = meLat + north / 110540.0;
    double ln = meLng + east / (111320.0 * cos(meLat * M_PI / 180.0));
    balls[nballs].id = id;
    balls[nballs].lat = la;
    balls[nballs].lng = ln;
    balls[nballs].found = found;
    nballs++;
}

int main(void) {
    bakeBackground();
    blinkT = 0; blinkPeriod = 1000000;   /* 预览: 停在最亮一帧 */
    meLat = 39.908700;
    meLng = 116.397500;
    meAcc = 4;
    meHdg = 0.6f;
    linkOk = 1;                          /* 预览场景默认模拟已连接 */

    /* 场景1: 200m 量程, 3 颗珠子, 1 颗已找到 */
    addBall(72, 58, 1, 0);
    addBall(-148, 96, 2, 0);
    addBall(230, -120, 3, 0);
    balls[2].found = 0;
    rngNow = 200; zi = 3;
    renderFrame(1200);
    dumpBmp("preview_200.bmp");

    /* 场景2: 20m 量程贴近 */
    nballs = 0;
    addBall(9, 7, 1, 0);
    addBall(-16, -10, 2, 0);
    rngNow = 20; zi = 0;
    renderFrame(2400);
    dumpBmp("preview_20.bmp");

    /* 场景3: 500m + 已找到清单 */
    nballs = 0; nfoundSeq = 0;
    addBall(72, 58, 1, 1); foundSeq[nfoundSeq++] = 0;
    addBall(-148, 96, 2, 1); foundSeq[nfoundSeq++] = 1;
    addBall(420, -180, 3, 0);
    addBall(-900, 700, 4, 0);
    rngNow = 500; zi = 4;
    bannerT = 2000 - 300; bannerNum = 2;
    renderFrame(2000);
    dumpBmp("preview_500.bmp");

    /* 场景4: 全部 7 档量程各出一张, 检查网格疏密是否随量程递增 */
    bannerT = 0;
    for (int z = 0; z < ZOOMN; z++) {
        char name[32];
        zi = z; rngNow = (float)ZOOMS[z];
        renderFrame(3000 + z * 500);
        snprintf(name, sizeof name, "preview_z%d.bmp", z);
        dumpBmp(name);
        printf("  zoom %5d m -> cell %4d m (%5.1f px)\n", ZOOMS[z], cellMeters(rngNow),
               (R_SCR - 6.0) / rngNow * cellMeters(rngNow));
    }

    /* 场景5: 未连接画面: 表盘+网格保留, 无数据, 红色 NO LINK */
    linkOk = 0;
    renderFrame(2600);
    dumpBmp("preview_offline.bmp");

    printf("preview ok: 3 frames + 7 zooms + offline\n");
    return 0;
}
#else
/* ================= 主程序 ================= */
/* 说明: 本机 SDL 走 mali/opengles2 驱动, 窗口被强制成 1280x720 再缩放到
 * 1024x768 面板, 会把 4:3 画面横向拉宽 1.25 倍。因此视频输出绕开 SDL,
 * 直接 mmap /dev/fb0 (1024x768, 32bpp) 做双页翻转; 输入直接读 evdev。
 * SDL 只保留音频子系统。 */
static uint8_t *fbMem = NULL;
static size_t   fbMapLen = 0;
static int fbFd = -1, fbPitch = W * 4, fbPages = 1, fbPageH = H, fbDraw = 0;
static int evFd = -1, quitFlag = 0;

static int fbInit(void) {
    struct fb_var_screeninfo v, orig;
    struct fb_fix_screeninfo f;
    fbFd = open("/dev/fb0", O_RDWR);
    if (fbFd < 0) { perror("open /dev/fb0"); return -1; }
    if (ioctl(fbFd, FBIOGET_VSCREENINFO, &v) < 0) { perror("FBIOGET_VSCREENINFO"); return -1; }
    if (ioctl(fbFd, FBIOGET_FSCREENINFO, &f) < 0) { perror("FBIOGET_FSCREENINFO"); return -1; }
    fbPageH = (int)v.yres;
    fbPitch = f.line_length ? (int)f.line_length : W * 4;
    if ((int)v.xres != W || (int)v.yres != H) {
        fprintf(stderr, "[fb] warn: panel %ux%u (expect %dx%d)\n", v.xres, v.yres, W, H);
    }
    /* 单页直写: 不做 FBIOPAN_DISPLAY / blank 等任何会拿 fb_info 锁的 ioctl,
     * 否则系统自动休眠时会在内核锁上永久阻塞(实测 lock_fb_info 卡死)。
     * mmap 后 memcpy 是纯内存写, 系统休眠/唤醒都不影响, 唤醒后自动恢复显示。 */
    fbPages = 1;
    v.xres_virtual = v.xres;
    v.yres_virtual = v.yres;
    fbMapLen = (size_t)fbPitch * (size_t)v.yres_virtual;
    fbMem = (uint8_t *)mmap(NULL, fbMapLen, PROT_READ | PROT_WRITE, MAP_SHARED, fbFd, 0);
    if (fbMem == MAP_FAILED) { fbMem = NULL; perror("mmap fb0"); return -1; }
    fprintf(stderr, "[fb] %ux%u %ubpp pitch=%d pages=%d (single)\n",
            v.xres, v.yres, v.bits_per_pixel, fbPitch, fbPages);
    return 0;
}

/* ---------- 休眠相关 ----------
 * TrimUI 固件的 keymon 在休眠前(无论空闲超时还是按电源键)会做三件事:
 *     touch /tmp/system_suspend
 *     /usr/trimui/bin/kill_apps.sh <pid>    追到最深层子进程 kill -9
 *     echo mem > /sys/power/state
 * 这个 kill -9 抓不住信号, 应用必死 —— 这就是"掌机休眠后软件自动退出"的原因。
 * 实测 /tmp/stay_alive 拦不住这个 kill; /tmp/stay_awake 能拦但连电源键息屏也一起拦掉,
 * 所以这里都不指望, 真正的解决办法在 launch.sh: 用父进程守着, 唤醒后自动把应用拉回来。
 * 应用自己只做两件事: 拿一把 wake_lock 让运行期间 CPU 不进浅睡(保证 300ms 下发和蜂鸣),
 * 并留下 stay_alive 告诉 keymon 别在切应用时顺手清理我们。 */
static void sleepGuardOn(void) {
    int fd = open("/tmp/stay_alive", O_CREAT | O_WRONLY, 0644);
    if (fd >= 0) close(fd);
    FILE *wf = fopen("/sys/power/wake_lock", "w");
    if (wf) { fputs("dragonradar", wf); fclose(wf); }
}

static void sleepGuardOff(void) {
    unlink("/tmp/stay_alive");
    FILE *wf = fopen("/sys/power/wake_unlock", "w");
    if (wf) { fputs("dragonradar", wf); fclose(wf); }
}

/* kill 命令(非 keymon 的 kill -9)时也要正常收尾, 把标志清掉 */
static void onQuitSignal(int sig) { (void)sig; quitFlag = 1; }

static void fbPresent(void) {
    if (!fbMem) return;
    uint8_t *dst = fbMem + (size_t)fbDraw * (size_t)fbPageH * (size_t)fbPitch;
    if (fbPitch == W * 4) {
        memcpy(dst, FB, (size_t)W * H * 4);
    } else {
        for (int y = 0; y < H; y++)
            memcpy(dst + (size_t)y * fbPitch, FB + (size_t)y * W, (size_t)W * 4);
    }
    (void)fbDraw; (void)fbPages;
}

static void zoomStep(int d) { zi = clampi(zi + d, 0, ZOOMN - 1); }

#define MAXEV 12
static int evFds[MAXEV];
static int nEv = 0;

/* 打开所有支持按键的 event 设备(evdev 会向所有打开的读者广播同一事件,
 * 所以不必猜哪个节点是手柄; 换硬件也不会失效) */
static int inputInit(void) {
    char path[64];
    for (int i = 0; i < 20 && nEv < MAXEV; i++) {
        snprintf(path, sizeof path, "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        unsigned long evb[8];
        memset(evb, 0, sizeof evb);
        ioctl(fd, EVIOCGBIT(0, sizeof evb), evb);
        int hasKey = (evb[EV_KEY / (8 * sizeof(long))] &
                      (1UL << (EV_KEY % (8 * sizeof(long))))) != 0;
        if (!hasKey) { close(fd); continue; }
        char name[128] = "";
        if (ioctl(fd, EVIOCGNAME(sizeof name - 1), name) < 0) name[0] = 0;
        evFds[nEv++] = fd;
        fprintf(stderr, "[in] %s (%s)\n", path, name);
    }
    if (!nEv) fprintf(stderr, "[in] no input device found\n");
    return nEv ? 0 : -1;
}

/* 记录所有按下的键码, 方便现场核对掌机键位 */
static void logKey(int code) {
    FILE *f = fopen("/tmp/keys.log", "a");
    if (!f) return;
    fprintf(f, "%d\n", code);
    fclose(f);
}

static void handleEvent(struct input_event *ev, long now) {
    static int absYv = 0;
    static long lastStep = 0;
    if (ev->type == EV_KEY) {
        if (ev->value != 1) return;
        int c = ev->code;
        logKey(c);
        if (c == KEY_UP || c == KEY_RIGHT || c == BTN_DPAD_UP || c == BTN_DPAD_RIGHT)
            zoomStep(1);
        else if (c == KEY_DOWN || c == KEY_LEFT || c == BTN_DPAD_DOWN || c == BTN_DPAD_LEFT)
            zoomStep(-1);
        else if (c == PHYS_B || c == BTN_SELECT || c == BTN_MODE ||
                 c == KEY_ESC || c == KEY_BACK || c == KEY_BACKSPACE ||
                 c == KEY_HOME)
            quitFlag = 1;                        /* 物理 B = 退出回启动器 */
        /* 注意: KEY_POWER(116, 走 axp2202-pek) 绝不能当退出键。
         * 原来把它列进退出条件, 结果一按电源键本程序就自己退出了,
         * 看起来像"按电源键就闪退", 而系统休眠流程其实还没开始。
         * 电源键交给 keymon 处理(息屏/休眠), 本程序继续在后台活着。 */
        else if (c == PHYS_A || c == KEY_ENTER || c == KEY_SPACE)
            showHud = !showHud;                  /* 物理 A = 开关四角文字, 网格保留 */
        else
            fprintf(stderr, "[key] code=%d\n", c);
    } else if (ev->type == EV_ABS) {
        if (ev->code == ABS_HAT0Y) {
            if (ev->value > 0)      zoomStep(-1);
            else if (ev->value < 0) zoomStep(1);
        } else if (ev->code == ABS_Y) {
            absYv = ev->value;
        }
    }
    if (absYv < -14000 && now - lastStep > 260) { zoomStep(1);  lastStep = now; }
    if (absYv >  14000 && now - lastStep > 260) { zoomStep(-1); lastStep = now; }
}

static void inputPoll(long now) {
    struct input_event ev;
    for (int i = 0; i < nEv; i++) {
        while (read(evFds[i], &ev, sizeof ev) == (ssize_t)sizeof ev)
            handleEvent(&ev, now);
    }
}

int main(void) {
    /* 单实例保护: 两个进程同时写同一块帧缓冲会互相抢页, 画面撕裂闪跳。
     * 用 flock 而不是 pidof 判断, 进程被 kill -9 时内核自动释放锁, 不会残留。 */
    int lockFd = open("/tmp/dragonradar.lock", O_CREAT | O_RDWR, 0644);
    if (lockFd >= 0 && flock(lockFd, LOCK_EX | LOCK_NB) != 0) {
        fprintf(stderr, "[main] 已有实例在运行, 本次启动放弃\n");
        return 0;
    }
    if (SDL_Init(SDL_INIT_AUDIO) < 0)
        fprintf(stderr, "SDL_Init(audio): %s\n", SDL_GetError());
    audioStart();

    if (fbInit() < 0) {
        fprintf(stderr, "fbdev init failed\n");
        return 1;
    }
    /* 休眠不退出: 让 keymon 别杀我们, 并锁住 CPU */
    signal(SIGTERM, onQuitSignal);
    signal(SIGINT, onQuitSignal);
    signal(SIGHUP, SIG_IGN);
    sleepGuardOn();
    inputInit();
    bakeBackground();

    meLat = 39.908700;
    meLng = 116.397500;
    meAcc = 0;

    long last = nowMs();
    while (!quitFlag) {
        long now = nowMs();
        float dt = (float)(now - last) / 1000.0f;
        if (dt > 0.25f) dt = 0.25f;
        last = now;

        inputPoll(now);
        netPoll(now);
        updateState(dt, now);
        renderFrame(now);
        fbPresent();

        usleep(20000);   /* 约 30fps */
    }

    netClose();
    sleepGuardOff();
    if (fbMem) munmap(fbMem, fbMapLen);
    if (fbFd >= 0) close(fbFd);
    SDL_Quit();
    return 0;
}
#endif
