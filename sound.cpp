// ============================================================================
//  sound.cpp - a little sound mixer, the effects, the music, and sounds.txt
//
//  Windows' PlaySound can only play one sound at a time, so instead we talk
//  to the sound card directly with the waveOut functions. We keep a few short
//  buffers queued up; whenever the card finishes one, SoundPump() fills it
//  with the next slice of sound - the music and any effects added together -
//  and hands it back. The game loop calls SoundPump() all the time.
//
//  The effects and the tune are plain data (effectDefs[] and tune[]), read
//  from sounds.txt, so the sound designer can change them.
// ============================================================================

#include "cavern.h"
#include <mmsystem.h>
#include <stdio.h>

#define MIX_RATE    22050               // samples per second
#define MIX_BUFS    4                   // buffers queued at once
#define MIX_LEN     512                 // samples per buffer (about 1/43 of a second)
#define CHANNELS    4                   // effects that can play at the same time
#define FX_VOLUME   190
#define MUSIC_VOL   3200

BOOL soundOn = TRUE;
BOOL musicOn = TRUE;

EffectDef effectDefs[NUM_SOUNDS];
Tune      tunes[NUM_TUNES];
BOOL      previewing;
int       previewTune;
int       previewNote;

const char *tuneNames[NUM_TUNES] = { "The Entertainer", "Deep Down", "Mountain King" };

// Names shown in the sound designer, and used in sounds.txt
const char *soundNames[NUM_SOUNDS] =
{
    "Jump", "Key", "Exit opens", "Death", "Cavern cleared", "Game start", "Game over",
    "Boss warning", "Boss gone", "Switch", "Extra life"
};
static const char *soundIds[NUM_SOUNDS] =
{
    "JUMP", "KEY", "EXITOPENS", "DEATH", "CLEARED", "START", "GAMEOVER",
    "BOSSWARNING", "BOSSGONE", "SWITCH", "EXTRALIFE"
};

static HWAVEOUT wave;
static WAVEHDR  hdr[MIX_BUFS];
static short    bufData[MIX_BUFS][MIX_LEN];

// ============================================================================
//  Effects: turned from notes into samples whenever they change
// ============================================================================

typedef struct
{
    signed char *data;                  // samples, -42 to +42
    int          len;
} Effect;

typedef struct
{
    const Effect *fx;                   // NULL when the channel is free
    int           pos;
} Channel;

static Effect  effects[NUM_SOUNDS];
static Channel chans[CHANNELS];

// Square waves: the classic 8-bit beeper sound
void RebuildEffect(int n)
{
    const EffectDef *d = &effectDefs[n];

    // Stop it first, in case it's playing: its samples are about to change
    for (int c = 0; c < CHANNELS; c++)
        if (chans[c].fx == &effects[n])
            chans[c].fx = NULL;
    free(effects[n].data);

    int samples = 0;
    for (int i = 0; i < d->count; i++)
        samples += MIX_RATE / 1000 * d->notes[i].ms;

    signed char *p = (signed char *)malloc(samples > 0 ? samples : 1);
    effects[n].data = p;
    effects[n].len  = p ? samples : 0;
    if (p == NULL)
        return;

    double phase = 0;
    for (int i = 0; i < d->count; i++)
    {
        int len = MIX_RATE / 1000 * d->notes[i].ms;
        for (int s = 0; s < len; s++)
        {
            double f   = d->notes[i].startHz + (double)(d->notes[i].endHz - d->notes[i].startHz) * s / len;
            double env = 1.0 - 0.7 * s / len;           // each note fades a little
            int    amp = (f > 0) ? (int)(42 * env) : 0;
            phase += f / MIX_RATE;
            if (phase >= 1.0) phase -= 1.0;
            *p++ = (signed char)(phase < 0.5 ? amp : -amp);
        }
    }
}

void Play(int effect)
{
    if (!soundOn || wave == NULL || simulating)
        return;

    // Use a free channel, or else take over the one that has played longest
    int best = 0;
    for (int c = 0; c < CHANNELS; c++)
    {
        if (chans[c].fx == NULL)
        {
            best = c;
            break;
        }
        if (chans[c].pos > chans[best].pos)
            best = c;
    }
    chans[best].fx  = &effects[effect];
    chans[best].pos = 0;
}

void StopSounds(void)
{
    for (int c = 0; c < CHANNELS; c++)
        chans[c].fx = NULL;
}

// ============================================================================
//  The music. Each cavern has one of three tunes: The Entertainer (Scott
//  Joplin, 1902), Deep Down (written for Cavern Runner), and In the Hall of
//  the Mountain King (Grieg, 1875), which Manic Miner played. In the game the
//  tune speeds up as your air runs out.
// ============================================================================

static int    noteIndex = -1;
static int    noteLeft, noteLen;        // in samples
static double musicPhase, musicFreq;
static int    lastCavern = -1, lastState = -1;

void PreviewTune(int t, int from)
{
    previewTune = t;
    noteIndex   = from - 1;
    noteLeft    = 0;
    previewing  = (tunes[t].len > 0);
}

void StopPreview(void)
{
    previewing = FALSE;
}

static int MusicSample(void)
{
    const Tune *t = &tunes[previewing ? previewTune : cav->music];
    if (t->len == 0)
        return 0;

    if (noteLeft <= 0)
    {
        noteIndex++;
        if (noteIndex >= t->len)
        {
            noteIndex = 0;
            if (previewing)             // the designer plays it once through
            {
                previewing = FALSE;
                return 0;
            }
        }
        previewNote = noteIndex;

        // In the game: faster as the air runs out, up to 1.8 times
        double speed = previewing ? 1.0 : 1.0 + 0.8 * (1.0 - (double)air / AIR_MAX);
        noteLen = noteLeft = (int)((double)MIX_RATE * t->tempo / 1000 * t->notes[noteIndex].len / speed);
        musicFreq = 440.0 * pow(2.0, (t->notes[noteIndex].note - 69) / 12.0);
    }
    noteLeft--;

    // Each note sounds for three quarters of its length: crisp and bouncy
    if (noteLeft < noteLen / 4)
        return 0;
    musicPhase += musicFreq / MIX_RATE;
    if (musicPhase >= 1.0) musicPhase -= 1.0;
    return musicPhase < 0.5 ? MUSIC_VOL : -MUSIC_VOL;
}

// ============================================================================
//  The mixer
// ============================================================================

static void Mix(short *out, int n)
{
    // Start the tune from the top whenever a cavern (re)starts
    if (state == ST_PLAYING && (lastState != ST_PLAYING || lastCavern != cavNum))
    {
        noteIndex = -1;
        noteLeft  = 0;
    }
    lastState  = state;
    lastCavern = cavNum;

    BOOL music = previewing || (musicOn && state == ST_PLAYING && !paused && active);

    for (int i = 0; i < n; i++)
    {
        int s = 0;
        for (int c = 0; c < CHANNELS; c++)
        {
            Channel *ch = &chans[c];
            if (ch->fx == NULL)
                continue;
            if (ch->pos < ch->fx->len)
                s += ch->fx->data[ch->pos] * FX_VOLUME;
            if (++ch->pos >= ch->fx->len)
                ch->fx = NULL;
        }
        if (music)
            s += MusicSample();

        if (s > 32767)  s = 32767;      // keep within what 16 bits can hold
        if (s < -32768) s = -32768;
        out[i] = (short)s;
    }
}

void SoundPump(void)
{
    if (wave == NULL)
        return;
    for (int i = 0; i < MIX_BUFS; i++)
        if (!(hdr[i].dwFlags & WHDR_INQUEUE))   // finished playing (or never started)
        {
            Mix(bufData[i], MIX_LEN);
            waveOutWrite(wave, &hdr[i], sizeof(WAVEHDR));
        }
}

void SoundInit(void)
{
    WAVEFORMATEX fmt;
    memset(&fmt, 0, sizeof fmt);
    fmt.wFormatTag      = WAVE_FORMAT_PCM;
    fmt.nChannels       = 1;
    fmt.nSamplesPerSec  = MIX_RATE;
    fmt.wBitsPerSample  = 16;
    fmt.nBlockAlign     = 2;
    fmt.nAvgBytesPerSec = MIX_RATE * 2;

    if (waveOutOpen(&wave, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
    {
        wave = NULL;                    // no sound card: the game still works, silently
        return;
    }

    for (int i = 0; i < MIX_BUFS; i++)
    {
        memset(&hdr[i], 0, sizeof hdr[i]);
        hdr[i].lpData         = (LPSTR)bufData[i];
        hdr[i].dwBufferLength = MIX_LEN * sizeof(short);
        waveOutPrepareHeader(wave, &hdr[i], sizeof(WAVEHDR));
    }
}

void SoundShutdown(void)
{
    if (wave == NULL)
        return;
    waveOutReset(wave);                 // stop, and hand back all the buffers
    for (int i = 0; i < MIX_BUFS; i++)
        waveOutUnprepareHeader(wave, &hdr[i], sizeof(WAVEHDR));
    waveOutClose(wave);
    wave = NULL;
}

// ============================================================================
//  sounds.txt
//
//      EFFECT JUMP
//      300 750 110         start pitch, end pitch (Hz, 0 = silence), length (ms)
//      END
//
//      TUNE 1
//      TEMPO 95            milliseconds per step
//      62 1                note number (60 = middle C) and length in steps
//      END
// ============================================================================

static void ParseSounds(const char *text)
{
    char line[256];
    int  effect = -1;
    int  inTune = -1;                   // which tune we're reading, or -1

    while (*text)
    {
        text = NextLine(text, line, sizeof line);
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;

        if (StartsWith(p, "END"))
        {
            effect = -1;
            inTune = -1;
        }
        else if (StartsWith(p, "EFFECT"))
        {
            p += 6;
            while (*p == ' ' || *p == '\t') p++;
            effect = -1;
            for (int i = 0; i < NUM_SOUNDS; i++)
                if (StartsWith(p, soundIds[i]) && (p[lstrlen(soundIds[i])] <= ' '))
                {
                    effect = i;
                    effectDefs[i].count = 0;
                }
        }
        else if (StartsWith(p, "TUNE"))
        {
            // TUNE 1 to TUNE 3. A TUNE with no number comes from an older
            // version, which only had the Mountain King.
            int n = atoi(p + 4);
            inTune = (n >= 1 && n <= NUM_TUNES) ? n - 1 : 2;
            tunes[inTune].len = 0;
        }
        else if (inTune >= 0 && StartsWith(p, "TEMPO"))
        {
            int t = atoi(p + 5);
            if (t >= 40 && t <= 1000)
                tunes[inTune].tempo = t;
        }
        else if (effect >= 0 && *p >= '0' && *p <= '9')
        {
            int a, b, c;
            if (sscanf(p, "%d %d %d", &a, &b, &c) == 3 && effectDefs[effect].count < MAX_FX_NOTES &&
                a >= 0 && b >= 0 && c > 0 && c <= 5000)
            {
                SoundNote *note = &effectDefs[effect].notes[effectDefs[effect].count++];
                note->startHz = a;
                note->endHz   = b;
                note->ms      = c;
            }
        }
        else if (inTune >= 0 && *p >= '0' && *p <= '9')
        {
            Tune *t = &tunes[inTune];
            int a, b;
            if (sscanf(p, "%d %d", &a, &b) == 2 && t->len < MAX_TUNE &&
                a >= 24 && a <= 108 && b >= 1 && b <= 32)
            {
                t->notes[t->len].note = (unsigned char)a;
                t->notes[t->len].len  = (unsigned char)b;
                t->len++;
            }
        }
    }
}

// Put one effect (or, for NUM_SOUNDS + t, tune t) back to the standard version
void StandardSound(int which)
{
    static EffectDef keepFx[NUM_SOUNDS];
    static Tune      keepTunes[NUM_TUNES];
    memcpy(keepFx, effectDefs, sizeof effectDefs);
    memcpy(keepTunes, tunes, sizeof tunes);

    ParseSounds(defaultSounds);         // everything standard...

    if (which < NUM_SOUNDS)             // ...then put back all the others
    {
        EffectDef standard = effectDefs[which];
        memcpy(effectDefs, keepFx, sizeof effectDefs);
        effectDefs[which] = standard;
        memcpy(tunes, keepTunes, sizeof tunes);
        RebuildEffect(which);
    }
    else
    {
        static Tune standard;
        standard = tunes[which - NUM_SOUNDS];
        memcpy(effectDefs, keepFx, sizeof effectDefs);
        memcpy(tunes, keepTunes, sizeof tunes);
        tunes[which - NUM_SOUNDS] = standard;
    }
}

void LoadSounds(void)
{
    char path[MAX_PATH];
    ParseSounds(defaultSounds);         // the standard sounds first...
    ExePath(SOUND_FILE, path);
    char *text = ReadTextFile(path);
    if (text == NULL)
        WriteTextFile(path, defaultSounds);
    else
    {
        ParseSounds(text);              // ...then whatever the file changes
        free(text);
    }
    for (int i = 0; i < NUM_SOUNDS; i++)
        RebuildEffect(i);
}

BOOL SaveSounds(void)
{
    char *buf = (char *)malloc(4000 + NUM_SOUNDS * (MAX_FX_NOTES * 24 + 40) + NUM_TUNES * (MAX_TUNE * 12 + 40));
    if (buf == NULL)
        return FALSE;

    // Keep the comment block from the standard file at the top
    const char *first = strstr(defaultSounds, "EFFECT");
    int n = first ? (int)(first - defaultSounds) : 0;
    memcpy(buf, defaultSounds, n);

    for (int i = 0; i < NUM_SOUNDS; i++)
    {
        n += wsprintf(buf + n, "EFFECT %s\n", soundIds[i]);
        for (int k = 0; k < effectDefs[i].count; k++)
            n += wsprintf(buf + n, "%d %d %d\n", effectDefs[i].notes[k].startHz,
                          effectDefs[i].notes[k].endHz, effectDefs[i].notes[k].ms);
        n += wsprintf(buf + n, "END\n\n");
    }
    for (int t = 0; t < NUM_TUNES; t++)
    {
        n += wsprintf(buf + n, "; %s\nTUNE %d\nTEMPO %d\n", tuneNames[t], t + 1, tunes[t].tempo);
        for (int i = 0; i < tunes[t].len; i++)
            n += wsprintf(buf + n, "%d %d\n", tunes[t].notes[i].note, tunes[t].notes[i].len);
        n += wsprintf(buf + n, t + 1 < NUM_TUNES ? "END\n\n" : "END\n");
    }
    buf[n] = 0;

    char path[MAX_PATH];
    ExePath(SOUND_FILE, path);
    BOOL ok = WriteTextFile(path, buf);
    free(buf);
    if (!ok)
        MessageBox(hwndMain, "Couldn't save " SOUND_FILE ".", "Cavern Runner", MB_ICONERROR);
    return ok;
}
