#include "app/app.h"
#include "core/config.h"
#include "notify/sound.h"
#include <mmsystem.h>
#include <math.h>

#define RATE 44100
#define MAX_SAMPLES (RATE * 2)

struct partial {
    float freq;
    float amp;
};

struct voice {
    float start;
    float len;
    float decay;
    partial p[3];
};

struct preset {
    const wchar_t *id;
    float gain;
    voice v[3];
};

static const preset presets[] = {
    {L"chime", 0.30f, {
        {0.00f, 0.42f, 4.2f, {{880, 1.0f}, {1760, 0.22f}, {2640, 0.06f}}},
        {0.13f, 0.46f, 3.8f, {{1174.7f, 0.9f}, {2349.3f, 0.20f}, {}}},
    }},
    {L"soft", 0.26f, {
        {0.00f, 0.40f, 3.0f, {{523.25f, 1.0f}, {1046.5f, 0.16f}, {}}},
    }},
    {L"tick", 0.18f, {
        {0.00f, 0.09f, 26.0f, {{1320, 1.0f}, {2640, 0.28f}, {}}},
    }},
    {L"bell", 0.28f, {
        {0.00f, 0.90f, 2.2f, {{987.77f, 1.0f}, {1975.5f, 0.34f}, {2963, 0.10f}}},
    }},
    {L"drop", 0.28f, {
        {0.00f, 0.30f, 5.0f, {{1318.5f, 1.0f}, {2637, 0.18f}, {}}},
        {0.16f, 0.42f, 4.0f, {{659.25f, 1.0f}, {1318.5f, 0.18f}, {}}},
    }},
};

#define NPRESETS ((int)(sizeof presets / sizeof presets[0]))

static const wchar_t *names[NPRESETS + 1];

static HWAVEOUT dev;
static WAVEHDR hdr;
static short *pcm;

const wchar_t *const *sound_builtins(int *count)
{
    names[0] = L"none";
    for (int i = 0; i < NPRESETS; i++)
        names[i + 1] = presets[i].id;
    *count = NPRESETS + 1;
    return names;
}

void sound_stop(void)
{
    if (!dev)
        return;
    waveOutReset(dev);
    waveOutUnprepareHeader(dev, &hdr, sizeof hdr);
    waveOutClose(dev);
    dev = 0;
}

static void play_pcm(short *s, int frames, int channels, int rate)
{
    sound_stop();
    WAVEFORMATEX wf = {};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = (WORD)channels;
    wf.nSamplesPerSec = rate;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = (WORD)(channels * 2);
    wf.nAvgBytesPerSec = rate * wf.nBlockAlign;
    if (waveOutOpen(&dev, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL)) {
        dev = 0;
        return;
    }
    hdr = {};
    hdr.lpData = (char *)s;
    hdr.dwBufferLength = frames * wf.nBlockAlign;
    waveOutPrepareHeader(dev, &hdr, sizeof hdr);
    waveOutWrite(dev, &hdr, sizeof hdr);
}

static int render(const preset *p, float gain)
{
    int total = 0;
    for (int v = 0; v < 3; v++) {
        const voice *vo = &p->v[v];
        if (vo->len <= 0)
            continue;
        int end = (int)((vo->start + vo->len) * RATE);
        if (end > total)
            total = end;
    }
    if (total > MAX_SAMPLES)
        total = MAX_SAMPLES;
    for (int i = 0; i < total; i++)
        pcm[i] = 0;

    for (int v = 0; v < 3; v++) {
        const voice *vo = &p->v[v];
        if (vo->len <= 0)
            continue;
        int off = (int)(vo->start * RATE);
        int n = (int)(vo->len * RATE);
        int attack = RATE / 160;
        for (int i = 0; i < n && off + i < total; i++) {
            float t = (float)i / RATE;
            float env = expf(-vo->decay * t);
            if (i < attack)
                env *= 0.5f - 0.5f * cosf(3.14159265f * i / attack);
            float tail = (float)(n - i) / n;
            if (tail < 0.12f)
                env *= tail / 0.12f;
            float a = 0;
            for (int k = 0; k < 3; k++)
                if (vo->p[k].amp > 0)
                    a += vo->p[k].amp * sinf(6.2831853f * vo->p[k].freq * t);
            float s = a * env * gain;
            int q = pcm[off + i] + (int)(s * 32767);
            pcm[off + i] = (short)(q > 32767 ? 32767 : q < -32768 ? -32768 : q);
        }
    }
    return total;
}

static int load_wav(const wchar_t *file, int *frames, int *channels, int *rate)
{
    HANDLE f = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    static BYTE raw[MAX_SAMPLES * 4];
    DWORD n = 0;
    ReadFile(f, raw, sizeof raw, &n, 0);
    CloseHandle(f);
    if (n < 44 || *(DWORD *)raw != 0x46464952)
        return 0;

    int bits = 0, ok = 0;
    DWORD i = 12;
    while (i + 8 <= n) {
        DWORD id = *(DWORD *)(raw + i), sz = *(DWORD *)(raw + i + 4);
        BYTE *body = raw + i + 8;
        if (id == 0x20746d66 && i + 8 + 16 <= n) {
            if (*(WORD *)body != WAVE_FORMAT_PCM)
                return 0;
            *channels = *(WORD *)(body + 2);
            *rate = *(int *)(body + 4);
            bits = *(WORD *)(body + 14);
        } else if (id == 0x61746164 && bits == 16) {
            DWORD avail = n - (i + 8);
            if (sz > avail)
                sz = avail;
            int count = sz / 2;
            if (count > MAX_SAMPLES)
                count = MAX_SAMPLES;
            float g = cfg.volume / 100.0f;
            short *src = (short *)body;
            for (int k = 0; k < count; k++)
                pcm[k] = (short)(src[k] * g);
            *frames = count / (*channels ? *channels : 1);
            ok = 1;
            break;
        }
        i += 8 + sz + (sz & 1);
    }
    return ok && *channels >= 1 && *channels <= 2 && *rate >= 8000;
}

void sound_play(const wchar_t *id)
{
    if (!cfg.sound || !id || !id[0] || !lstrcmpiW(id, L"none"))
        return;
    if (!pcm) {
        pcm = (short *)HeapAlloc(GetProcessHeap(), 0, MAX_SAMPLES * 2);
        if (!pcm)
            return;
    }

    for (int i = 0; i < NPRESETS; i++)
        if (!lstrcmpiW(id, presets[i].id)) {
            int n = render(&presets[i], presets[i].gain * cfg.volume / 100.0f);
            play_pcm(pcm, n, 1, RATE);
            return;
        }

    int frames = 0, ch = 1, rate = RATE;
    if (load_wav(id, &frames, &ch, &rate))
        play_pcm(pcm, frames, ch, rate);
}
