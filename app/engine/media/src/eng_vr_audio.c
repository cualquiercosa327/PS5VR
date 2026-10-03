/*
 * eng_vr_audio.c - head-tracked spatial audio for PS5VR (see eng_vr_audio.h).
 *
 * Every source is rendered through 8 virtual loudspeakers fixed to the head
 * (0, 45, ... 315 degrees, clockwise from straight ahead), and each speaker
 * reaches each ear through the spherical-head model of Brown & Duda (1998,
 * "A structural model for binaural sound synthesis"): an interaural delay
 * (Woodworth) and a one-pole/one-zero head-shadow filter.
 *
 *  - Channel-based audio (stereo, 5.1, 7.1): each channel is a source at its
 *    standard angle in the video's space; the head's rotation moves it across
 *    the head-fixed speakers (pairwise constant-power panning), so the sound
 *    stays where it is in the scene when the viewer turns.
 *  - First-order ambisonics (AmbiX: ACN order W Y Z X, SN3D - YouTube's 360
 *    spatial audio): the sound field is rotated into the head's frame, then
 *    decoded to the same speakers.
 */
#include "eng_vr_audio.h"
#include "eng_boot_log.h"

#include <math.h>
#include <string.h>

#define VS        8                  /* virtual speakers */
#define RATE      48000.0f
#define HEAD_A    0.0875f            /* head radius, m */
#define SOUND_C   343.0f
#define DLINE     64                 /* delay line, samples (max ITD ~32) */

typedef struct {
    float n0, n1, d1;                /* head shadow: y = n0 x + n1 x1 - d1 y1 */
    float x1, y1;
    int   di;                        /* delay: integer part ... */
    float df;                        /* ... and fraction */
} ear_t;

static ear_t s_ear[VS][2];
static float s_line[VS][DLINE];      /* speaker feeds, for the delays */
static int   s_pos;
static int   s_ready;
static volatile float s_head[4] = {0, 0, 0, 1};
static volatile int   s_tracked;
static volatile int   s_enabled = 1;

void eng_vr_audio_set_enabled(int on)
{
    s_enabled = on;
}

static float wrap180(float d)
{
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

static void init_filters(void)
{
    const float d2r = 3.14159265f / 180.0f;
    const float tau = 2.0f * HEAD_A / SOUND_C, T = 1.0f / RATE;
    const float amin = 0.1f, thmin = 150.0f;
    for (int i = 0; i < VS; i++) {
        const float az = 45.0f * (float)i;
        for (int e = 0; e < 2; e++) {
            const float ear_az = e ? 90.0f : -90.0f;
            const float th = fabsf(wrap180(az - ear_az));           /* 0 = facing the ear */
            const float alpha = (1.0f + amin / 2) + (1.0f - amin / 2) * cosf(th / thmin * 3.14159265f);
            const float n0 = 1.0f + 2.0f * alpha * tau / T, n1 = 1.0f - 2.0f * alpha * tau / T;
            const float d0 = 1.0f + 2.0f * tau / T, d1 = 1.0f - 2.0f * tau / T;
            ear_t *x = &s_ear[i][e];
            x->n0 = n0 / d0;
            x->n1 = n1 / d0;
            x->d1 = d1 / d0;
            x->x1 = x->y1 = 0.0f;
            /* Woodworth: nothing for the near side, the arc round the head for the far */
            const float t = th < 90.0f ? (HEAD_A / SOUND_C) * (1.0f - cosf(th * d2r))
                                       : (HEAD_A / SOUND_C) * (1.0f + (th - 90.0f) * d2r);
            const float d = t * RATE;
            x->di = (int)d;
            x->df = d - (float)x->di;
        }
    }
    memset(s_line, 0, sizeof s_line);
    s_ready = 1;
}

void eng_vr_audio_set_head(const float q[4], int tracked)
{
    s_head[0] = q[0];
    s_head[1] = q[1];
    s_head[2] = q[2];
    s_head[3] = q[3];
    s_tracked = tracked;
}

/* Rows of R^T for the head rotation R (head -> video space): world -> head. */
static void head_matrix(float m[3][3])
{
    float x = s_head[0], y = s_head[1], z = s_head[2], w = s_head[3];
    if (!s_tracked || !s_enabled) {
        x = y = z = 0.0f;
        w = 1.0f;
    }
    /* R's columns are the head's axes in video space; R^T rows = those columns */
    m[0][0] = 1 - 2 * (y * y + z * z); m[0][1] = 2 * (x * y + z * w);     m[0][2] = 2 * (x * z - y * w);
    m[1][0] = 2 * (x * y - z * w);     m[1][1] = 1 - 2 * (x * x + z * z); m[1][2] = 2 * (y * z + x * w);
    m[2][0] = 2 * (x * z + y * w);     m[2][1] = 2 * (y * z - x * w);     m[2][2] = 1 - 2 * (x * x + y * y);
}

/* Azimuth (clockwise, degrees) of a video-space direction, seen from the head. */
static float head_azimuth(const float m[3][3], float az_world)
{
    const float r = az_world * 3.14159265f / 180.0f;
    const float w[3] = {sinf(r), 0.0f, -cosf(r)};          /* x right, y up, z back */
    const float hx = m[0][0] * w[0] + m[0][1] * w[1] + m[0][2] * w[2];
    const float hz = m[2][0] * w[0] + m[2][1] * w[1] + m[2][2] * w[2];
    return atan2f(hx, -hz) * 180.0f / 3.14159265f;
}

/* Channel angles (clockwise) for FFmpeg's 7.1 order: FL FR FC LFE BL BR SL SR. */
static const float k_az71[8] = {-30, 30, 0, 0, -150, 150, -90, 90};

void eng_vr_audio_render(const float *in, int ch, int ambisonic, int frames, float *out)
{
    if (!s_ready)
        init_filters();
    if (!s_enabled && ch == 2 && !ambisonic) {          /* plain stereo, untouched */
        memcpy(out, in, (size_t)frames * 2 * sizeof(float));
        return;
    }
    float m[3][3];
    head_matrix(m);

    /* gain of each input channel into each virtual speaker */
    float g[8][VS];
    memset(g, 0, sizeof g);
    float amb[3][3];                 /* ambisonic rotation (X,Y,Z amb axes) */
    if (ambisonic) {
        /* amb (X front, Y left, Z up) <-> video (x right, y up, z back):
         * video = (-Y, Z, -X); head = M video; amb_head = (-hz, -hx, hy). */
        static const float C[3][3] = {{0, -1, 0}, {0, 0, 1}, {-1, 0, 0}};   /* amb -> video */
        static const float Ci[3][3] = {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}};  /* video -> amb */
        float t[3][3];
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                t[r][c] = m[r][0] * C[0][c] + m[r][1] * C[1][c] + m[r][2] * C[2][c];
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                amb[r][c] = Ci[r][0] * t[0][c] + Ci[r][1] * t[1][c] + Ci[r][2] * t[2][c];
    } else {
        for (int c = 0; c < ch && c < 8; c++) {
            if (ch == 8 && c == 3) {                     /* LFE: everywhere, gently */
                for (int i = 0; i < VS; i++)
                    g[c][i] = 0.12f;
                continue;
            }
            const float az = (ch == 2) ? (c ? 30.0f : -30.0f) : (ch == 1 ? 0.0f : k_az71[c]);
            float p = head_azimuth(m, az);
            if (p < 0.0f)
                p += 360.0f;
            const float s = p / 45.0f;
            const int i0 = (int)s % VS, i1 = (i0 + 1) % VS;
            const float t = s - floorf(s);
            g[c][i0] += cosf(t * 1.5707963f);
            g[c][i1] += sinf(t * 1.5707963f);
        }
    }
    /* level: the ear filters lift the near side; surround is summed down */
    const float master = ambisonic ? 0.9f : (ch > 2 ? 0.55f : 0.8f);

    for (int n = 0; n < frames; n++) {
        const float *x = in + (size_t)n * ch;
        float feed[VS];
        if (ambisonic) {
            const float W = x[0], Y = x[1], Z = x[2], X = x[3];
            const float Xh = amb[0][0] * X + amb[0][1] * Y + amb[0][2] * Z;
            const float Yh = amb[1][0] * X + amb[1][1] * Y + amb[1][2] * Z;
            for (int i = 0; i < VS; i++) {
                /* speaker i at clockwise azimuth 45 i; ambisonic azimuth is anticlockwise */
                static const float cs[VS] = {1, 0.70710678f, 0, -0.70710678f, -1, -0.70710678f, 0, 0.70710678f};
                static const float sn[VS] = {0, -0.70710678f, -1, -0.70710678f, 0, 0.70710678f, 1, 0.70710678f};
                feed[i] = 0.25f * (W + 1.5f * (Xh * cs[i] + Yh * sn[i]));
            }
        } else {
            for (int i = 0; i < VS; i++) {
                float v = 0.0f;
                for (int c = 0; c < ch && c < 8; c++)
                    v += g[c][i] * x[c];
                feed[i] = v;
            }
        }
        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < VS; i++) {
            s_line[i][s_pos] = feed[i];
            for (int e = 0; e < 2; e++) {
                ear_t *ea = &s_ear[i][e];
                const int a = (s_pos - ea->di) & (DLINE - 1), b = (a - 1) & (DLINE - 1);
                const float d = s_line[i][a] + (s_line[i][b] - s_line[i][a]) * ea->df;
                const float y = ea->n0 * d + ea->n1 * ea->x1 - ea->d1 * ea->y1;
                ea->x1 = d;
                ea->y1 = y;
                if (e)
                    r += y;
                else
                    l += y;
            }
        }
        s_pos = (s_pos + 1) & (DLINE - 1);
        l *= master;
        r *= master;
        /* soft knee above -3 dBFS instead of clipping */
        out[2 * n] = fabsf(l) < 0.7f ? l : copysignf(0.7f + 0.3f * tanhf((fabsf(l) - 0.7f) / 0.3f), l);
        out[2 * n + 1] = fabsf(r) < 0.7f ? r : copysignf(0.7f + 0.3f * tanhf((fabsf(r) - 0.7f) / 0.3f), r);
    }
    /* A level line every ~2 s: the ear balance against the head's yaw. */
    static int s_blocks;
    static double s_l, s_r;
    for (int n = 0; n < frames; n++) {
        s_l += (double)out[2 * n] * out[2 * n];
        s_r += (double)out[2 * n + 1] * out[2 * n + 1];
    }
    if (++s_blocks >= 47) {
        const float yaw = atan2f(-m[2][0], m[0][0]) * 57.29578f;   /* head yaw, + = right */
        eng_boot_log("vr audio: %dch%s yaw %.0f  L %.3f R %.3f (%+.1f dB)", ch,
                     ambisonic ? " amb" : "", (double)yaw, sqrt(s_l / (s_blocks * frames)),
                     sqrt(s_r / (s_blocks * frames)), 10.0 * log10((s_r + 1e-12) / (s_l + 1e-12)));
        s_blocks = 0;
        s_l = s_r = 0.0;
    }
}
