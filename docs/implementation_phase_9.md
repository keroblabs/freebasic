# Phase 9 — Sound (SDL2 Audio): Detailed Implementation Guide

This document specifies every data structure, algorithm, file change, and API needed to build **SOUND, PLAY macro language, and PLAY(n) function** for the FreeBASIC interpreter. Phase 9 enables music programs and games with sound effects. All audio is generated via SDL2's audio subsystem.

---

## Project File Structure (Phase 9 additions)

```
fbasic/
├── Makefile                        [MOD] — link SDL2 audio
├── include/
│   ├── ast.h                      [MOD] — AST_SOUND, AST_PLAY
│   ├── parser.h                   [MOD]
│   ├── interpreter.h              [MOD]
│   ├── sound.h                    [NEW] — audio subsystem API
│   └── ...
├── src/
│   ├── parser.c                   [MOD] — parse SOUND, PLAY
│   ├── interpreter.c              [MOD] — dispatch sound statements
│   ├── sound.c                    [NEW] — SDL2 audio, tone generation, PLAY engine
│   ├── play_parser.c              [NEW] — PLAY macro language tokenizer/executor
│   └── ...
└── tests/
    └── verify/
        ├── phase9_sound.bas       [NEW]
        ├── phase9_play.bas        [NEW]
        ├── phase9_music.bas       [NEW]
        ├── phase9_milestone.bas   [NEW]
        └── phase9_expected/       [NEW]
            ├── sound.txt
            ├── play.txt
            ├── music.txt
            └── milestone.txt
```

---

## 1. Audio Subsystem Architecture

### 1.1 Data Structures

```c
#include <SDL.h>
#include <math.h>

#define AUDIO_SAMPLE_RATE   44100
#define AUDIO_BUFFER_SIZE   2048
#define AUDIO_CHANNELS      1
#define MAX_NOTE_QUEUE      256

// A single tone/note to play
typedef struct FBTone {
    double   frequency;     // Hz (0 = rest/silence)
    double   duration_ms;   // Milliseconds
} FBTone;

// Background note queue for PLAY MB (music background)
typedef struct NoteQueue {
    FBTone   notes[MAX_NOTE_QUEUE];
    int      head;
    int      tail;
    int      count;
    SDL_mutex* mutex;
} NoteQueue;

// Audio subsystem state
typedef struct FBSound {
    SDL_AudioDeviceID device;
    SDL_AudioSpec     spec;

    // Current tone being generated
    double   current_freq;
    double   phase;           // Phase accumulator (0.0 to 1.0)
    int64_t  samples_remaining; // Samples left for current tone
    int      playing;         // 1 if currently outputting sound

    // Background queue
    NoteQueue queue;

    // PLAY state
    int      play_mode;       // 0=MF (foreground), 1=MB (background), 2=MN (normal/legato)
    int      octave;          // Current octave (0-6, default 4)
    int      tempo;           // Quarter notes per minute (default 120)
    int      note_length;     // Default note length (1=whole, 4=quarter, etc., default 4)
    double   note_fraction;   // Fraction of note duration that sounds (legato=1.0, staccato=0.75, normal=0.875)

    int      initialized;
} FBSound;
```

### 1.2 SDL2 Audio Callback

```c
static FBSound g_sound = {0};

static void audio_callback(void* userdata, Uint8* stream, int len) {
    (void)userdata;
    int16_t* buffer = (int16_t*)stream;
    int samples = len / sizeof(int16_t);

    for (int i = 0; i < samples; i++) {
        if (g_sound.samples_remaining <= 0) {
            // Try to dequeue next note
            SDL_LockMutex(g_sound.queue.mutex);
            if (g_sound.queue.count > 0) {
                FBTone tone = g_sound.queue.notes[g_sound.queue.head];
                g_sound.queue.head = (g_sound.queue.head + 1) % MAX_NOTE_QUEUE;
                g_sound.queue.count--;
                SDL_UnlockMutex(g_sound.queue.mutex);

                g_sound.current_freq = tone.frequency;
                g_sound.samples_remaining =
                    (int64_t)(tone.duration_ms * AUDIO_SAMPLE_RATE / 1000.0);
                g_sound.phase = 0.0;
            } else {
                SDL_UnlockMutex(g_sound.queue.mutex);
                buffer[i] = 0;
                continue;
            }
        }

        if (g_sound.current_freq > 0) {
            // Generate square wave (FB-style sound)
            double sample = (g_sound.phase < 0.5) ? 0.3 : -0.3;
            buffer[i] = (int16_t)(sample * 32767);

            g_sound.phase += g_sound.current_freq / (double)AUDIO_SAMPLE_RATE;
            if (g_sound.phase >= 1.0) g_sound.phase -= 1.0;
        } else {
            buffer[i] = 0; // Rest
        }

        g_sound.samples_remaining--;
    }
}
```

### 1.3 Init / Shutdown

```c
int sound_init(void) {
    if (g_sound.initialized) return 0;

    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL audio init failed: %s\n", SDL_GetError());
        return -1;
    }

    SDL_AudioSpec want = {0};
    want.freq = AUDIO_SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = AUDIO_CHANNELS;
    want.samples = AUDIO_BUFFER_SIZE;
    want.callback = audio_callback;

    g_sound.device = SDL_OpenAudioDevice(NULL, 0, &want, &g_sound.spec, 0);
    if (g_sound.device == 0) {
        fprintf(stderr, "SDL OpenAudioDevice failed: %s\n", SDL_GetError());
        return -1;
    }

    g_sound.queue.mutex = SDL_CreateMutex();

    // Set defaults
    g_sound.octave = 4;
    g_sound.tempo = 120;
    g_sound.note_length = 4;
    g_sound.note_fraction = 7.0 / 8.0; // MN (normal)
    g_sound.play_mode = 0; // MF (foreground)

    SDL_PauseAudioDevice(g_sound.device, 0); // Start playback
    g_sound.initialized = 1;
    return 0;
}

void sound_shutdown(void) {
    if (!g_sound.initialized) return;
    SDL_CloseAudioDevice(g_sound.device);
    SDL_DestroyMutex(g_sound.queue.mutex);
    g_sound.initialized = 0;
}
```

---

## 2. SOUND Statement

### 2.1 Syntax

```basic
SOUND frequency, duration
' frequency: 37 to 32767 Hz (0 = silence)
' duration: in clock ticks (18.2 ticks per second)
```

### 2.2 Implementation

```c
static void exec_sound(Interpreter* interp, ASTNode* node) {
    FBValue freq_v = eval_expr(interp, node->data.sound_stmt.frequency);
    FBValue dur_v  = eval_expr(interp, node->data.sound_stmt.duration);

    double freq = fbval_to_double(&freq_v);
    double dur  = fbval_to_double(&dur_v);

    fbval_release(&freq_v);
    fbval_release(&dur_v);

    // Validate
    if (freq < 37 && freq != 0) {
        // FB allows frequency 0 for silence, 37-32767 for tones
        // Some implementations also allow low frequencies
    }
    if (dur < 0 || dur > 65535) {
        fb_runtime_error(interp, FB_ERR_ILLEGAL_FUNCTION_CALL,
                         node->line, "SOUND duration out of range");
        return;
    }

    if (!g_sound.initialized) {
        if (sound_init() < 0) return;
    }

    // Convert duration from clock ticks to milliseconds
    // FB clock ticks = 18.2 per second
    double ms = dur / 18.2 * 1000.0;

    FBTone tone = { freq, ms };

    // SOUND is always synchronous (blocks until tone finishes)
    enqueue_tone(&tone);
    sound_wait_for_completion();
}

static void enqueue_tone(FBTone* tone) {
    SDL_LockMutex(g_sound.queue.mutex);
    if (g_sound.queue.count < MAX_NOTE_QUEUE) {
        g_sound.queue.notes[g_sound.queue.tail] = *tone;
        g_sound.queue.tail = (g_sound.queue.tail + 1) % MAX_NOTE_QUEUE;
        g_sound.queue.count++;
    }
    SDL_UnlockMutex(g_sound.queue.mutex);
}

static void sound_wait_for_completion(void) {
    while (1) {
        SDL_LockMutex(g_sound.queue.mutex);
        int remaining = g_sound.queue.count;
        int playing = (g_sound.samples_remaining > 0);
        SDL_UnlockMutex(g_sound.queue.mutex);

        if (remaining == 0 && !playing) break;
        SDL_Delay(10);
    }
}
```

---

## 3. PLAY Macro Language

### 3.1 Command Reference

| Command | Description |
|---------|-------------|
| `A` - `G` | Play note A through G |
| `#` or `+` | Sharp (after note letter) |
| `-` | Flat (after note letter) |
| `N n` | Play note by number (0-84, 0=rest) |
| `O n` | Set octave (0-6) |
| `>` | Increase octave by 1 |
| `<` | Decrease octave by 1 |
| `L n` | Set default note length (1=whole, 2=half, 4=quarter, etc.) |
| `T n` | Set tempo in quarter notes per minute (32-255) |
| `P n` | Rest for duration n |
| `.` | Dotted note (1.5× duration, after note or length) |
| `MF` | Music foreground (block until done) |
| `MB` | Music background (play while program continues) |
| `MN` | Music normal (each note plays 7/8 of duration) |
| `ML` | Music legato (each note plays full duration) |
| `MS` | Music staccato (each note plays 3/4 of duration) |

### 3.2 Note Frequency Table

```c
// Frequencies for octave 0 (C0 to B0)
// Using equal temperament: f = 440 * 2^((n-49)/12) where n is piano key
static const double base_frequencies[12] = {
    // C      C#     D      D#     E      F      F#     G      G#     A      A#     B
    16.35, 17.32, 18.35, 19.45, 20.60, 21.83, 23.12, 24.50, 25.96, 27.50, 29.14, 30.87
};

static double note_frequency(int note, int octave) {
    // note: 0=C, 1=C#, 2=D, ..., 11=B
    // octave: 0-6
    if (note < 0 || note > 11 || octave < 0 || octave > 6) return 0;
    return base_frequencies[note] * pow(2.0, octave);
}
```

### 3.3 PLAY Parser / Executor

```c
// play_parser.c

typedef struct {
    const char* src;
    int         pos;
    int         len;
} PlayState;

void play_execute(Interpreter* interp, const char* cmd_string) {
    if (!g_sound.initialized) {
        if (sound_init() < 0) return;
    }

    PlayState ps = { cmd_string, 0, (int)strlen(cmd_string) };

    while (ps.pos < ps.len) {
        skip_spaces_play(&ps);
        if (ps.pos >= ps.len) break;

        char c = toupper(ps.src[ps.pos++]);

        switch (c) {
            case 'A': case 'B': case 'C': case 'D':
            case 'E': case 'F': case 'G':
                play_note_letter(&ps, c);
                break;

            case 'N': {
                int n = parse_play_int(&ps);
                if (n == 0) {
                    // Rest
                    play_rest(g_sound.note_length);
                } else {
                    // Note by number (1-84)
                    int octave = (n - 1) / 12;
                    int semitone = (n - 1) % 12;
                    double freq = note_frequency(semitone, octave);
                    double ms = note_duration_ms(g_sound.note_length, 0);
                    play_tone(freq, ms);
                }
                break;
            }

            case 'O':
                g_sound.octave = parse_play_int(&ps);
                if (g_sound.octave < 0) g_sound.octave = 0;
                if (g_sound.octave > 6) g_sound.octave = 6;
                break;

            case '>':
                if (g_sound.octave < 6) g_sound.octave++;
                break;

            case '<':
                if (g_sound.octave > 0) g_sound.octave--;
                break;

            case 'L': {
                int l = parse_play_int(&ps);
                if (l >= 1 && l <= 64) g_sound.note_length = l;
                break;
            }

            case 'T': {
                int t = parse_play_int(&ps);
                if (t >= 32 && t <= 255) g_sound.tempo = t;
                break;
            }

            case 'P': {
                int p = parse_play_int(&ps);
                int dots = count_dots(&ps);
                double ms = note_duration_ms(p, dots);
                FBTone rest = { 0.0, ms };
                enqueue_tone(&rest);
                break;
            }

            case 'M':
                if (ps.pos < ps.len) {
                    char m = toupper(ps.src[ps.pos++]);
                    switch (m) {
                        case 'F': g_sound.play_mode = 0; break; // Foreground
                        case 'B': g_sound.play_mode = 1; break; // Background
                        case 'N': g_sound.note_fraction = 7.0 / 8.0; break;
                        case 'L': g_sound.note_fraction = 1.0; break;
                        case 'S': g_sound.note_fraction = 3.0 / 4.0; break;
                    }
                }
                break;
        }
    }

    // If foreground mode, wait for all notes to finish
    if (g_sound.play_mode == 0) {
        sound_wait_for_completion();
    }
}

static void play_note_letter(PlayState* ps, char letter) {
    // Map letter to semitone: C=0, D=2, E=4, F=5, G=7, A=9, B=11
    static const int letter_to_semitone[] = {
        9, 11, 0, 2, 4, 5, 7  // A=9, B=11, C=0, D=2, E=4, F=5, G=7
    };
    int semitone = letter_to_semitone[letter - 'A'];

    // Check for sharp/flat
    if (ps->pos < ps->len) {
        char mod = ps->src[ps->pos];
        if (mod == '#' || mod == '+') { semitone++; ps->pos++; }
        else if (mod == '-')          { semitone--; ps->pos++; }
    }

    // Wrap semitone
    int oct = g_sound.octave;
    if (semitone > 11) { semitone -= 12; oct++; }
    if (semitone < 0)  { semitone += 12; oct--; }

    // Optional length suffix
    int length = g_sound.note_length;
    if (ps->pos < ps->len && isdigit(ps->src[ps->pos])) {
        length = parse_play_int(ps);
    }

    int dots = count_dots(ps);

    double freq = note_frequency(semitone, oct);
    double total_ms = note_duration_ms(length, dots);
    double sound_ms = total_ms * g_sound.note_fraction;
    double rest_ms  = total_ms - sound_ms;

    play_tone(freq, sound_ms);
    if (rest_ms > 0) {
        FBTone rest = { 0.0, rest_ms };
        enqueue_tone(&rest);
    }
}

static double note_duration_ms(int length, int dots) {
    // Duration of a whole note in ms at current tempo
    // tempo = quarter notes per minute
    // quarter note = 60000/tempo ms
    // whole note = 4 * quarter note
    double quarter_ms = 60000.0 / g_sound.tempo;
    double ms = (4.0 / length) * quarter_ms;

    // Dotted notes: each dot adds half of previous
    double add = ms / 2.0;
    for (int i = 0; i < dots; i++) {
        ms += add;
        add /= 2.0;
    }
    return ms;
}

static void play_tone(double freq, double duration_ms) {
    FBTone tone = { freq, duration_ms };
    enqueue_tone(&tone);
}

static int count_dots(PlayState* ps) {
    int dots = 0;
    while (ps->pos < ps->len && ps->src[ps->pos] == '.') {
        dots++;
        ps->pos++;
    }
    return dots;
}
```

---

## 4. PLAY(n) Function

### 4.1 Syntax

```basic
n% = PLAY(0)   ' Returns number of notes remaining in background buffer
```

### 4.2 Implementation

```c
static FBValue builtin_play_func(Interpreter* interp, FBValue* args, int nargs) {
    (void)interp;
    (void)args;
    (void)nargs;

    SDL_LockMutex(g_sound.queue.mutex);
    int count = g_sound.queue.count;
    if (g_sound.samples_remaining > 0) count++; // Currently playing note
    SDL_UnlockMutex(g_sound.queue.mutex);

    return fbval_int((int16_t)count);
}
```

---

## 5. PLAY Statement vs PLAY Function Disambiguation

The parser must distinguish between:
- `PLAY "string"` — the PLAY statement (macro language)
- `x = PLAY(n)` — the PLAY function (notes remaining)

```c
// In parse_statement:
if (token_is_keyword(tok, KW_PLAY)) {
    // Look ahead: if next token is '(' and this is in expression context → function
    // Otherwise → statement
    Token* next = peek_token(p);

    // PLAY as a statement takes a string expression:
    // PLAY "..." or PLAY variable$
    // PLAY(n) is the function form — handled in expression parser
    parse_play_statement(p);
}

// In expression parser, for function calls:
// If "PLAY" followed by "(", parse as PLAY(n) function
```

---

## 6. Waveform Options

FB's SOUND used the PC speaker, which produced a square wave. For better compatibility and nicer sound, support multiple waveforms:

```c
typedef enum {
    WAVE_SQUARE,     // Classic FB sound
    WAVE_SINE,       // Smoother
    WAVE_TRIANGLE,   // Mellow
    WAVE_SAWTOOTH    // Bright
} WaveformType;

// In audio_callback, replace the square wave line:
static double generate_sample(double phase, WaveformType wave) {
    switch (wave) {
        case WAVE_SQUARE:
            return (phase < 0.5) ? 0.3 : -0.3;
        case WAVE_SINE:
            return 0.3 * sin(2.0 * M_PI * phase);
        case WAVE_TRIANGLE:
            return 0.3 * (phase < 0.5
                          ? (4.0 * phase - 1.0)
                          : (3.0 - 4.0 * phase));
        case WAVE_SAWTOOTH:
            return 0.3 * (2.0 * phase - 1.0);
        default:
            return 0.0;
    }
}
```

---

## 7. Verification Test Files

### 7.1 `tests/verify/phase9_sound.bas`

```basic
REM Phase 9 Test: SOUND statement
PRINT "Playing ascending tones..."
FOR f% = 200 TO 800 STEP 100
    SOUND f%, 3    ' ~0.16 seconds each
NEXT f%
PRINT "Playing silence..."
SOUND 0, 18        ' ~1 second of silence
PRINT "One more tone"
SOUND 440, 18      ' A4 for ~1 second
PRINT "Done"
```

**Expected output (`tests/verify/phase9_expected/sound.txt`):**
```
Playing ascending tones...
Playing silence...
One more tone
Done
```

### 7.2 `tests/verify/phase9_play.bas`

```basic
REM Phase 9 Test: PLAY macro language
PRINT "Scale in C major..."
PLAY "O4 L4 C D E F G A B > C"

PRINT "Changing tempo..."
PLAY "T200 O3 L8 C E G > C"

PRINT "Dotted notes..."
PLAY "T120 O4 L4 C. D. E"

PRINT "Sharps and flats..."
PLAY "O4 L4 C C# D D# E"

PRINT "Rest..."
PLAY "O4 L4 C P4 E P4 G"

PRINT "PLAY function test"
PLAY "MB O4 L1 C E G"
PRINT "Notes remaining:"; PLAY(0)
' Wait for background music
DO WHILE PLAY(0) > 0
    ' busy wait
LOOP
PRINT "Done"
```

**Expected output (`tests/verify/phase9_expected/play.txt`):**
```
Scale in C major...
Changing tempo...
Dotted notes...
Sharps and flats...
Rest...
PLAY function test
Notes remaining: 3
Done
```

### 7.3 `tests/verify/phase9_music.bas`

```basic
REM Phase 9 Test: Simple melody (Twinkle Twinkle)
PRINT "Twinkle Twinkle Little Star"
PLAY "T120 O4 L4"
PLAY "C C G G A A G2"
PLAY "F F E E D D C2"
PLAY "G G F F E E D2"
PLAY "G G F F E E D2"
PLAY "C C G G A A G2"
PLAY "F F E E D D C2"
PRINT "Music finished!"
```

**Expected output (`tests/verify/phase9_expected/music.txt`):**
```
Twinkle Twinkle Little Star
Music finished!
```

### 7.4 `tests/verify/phase9_milestone.bas` — Milestone

```basic
REM Phase 9 Milestone: Music and sound effects
PRINT "=== Sound & Music Demo ==="

' Sound effect: laser
PRINT "Laser sound..."
FOR f% = 2000 TO 200 STEP -50
    SOUND f%, 1
NEXT f%

' Short melody
PRINT "Short melody..."
PLAY "MN T160 O4"
PLAY "L8 E E P8 E P8 C E P8 G P4 < G"

' Staccato vs legato
PRINT "Staccato:"
PLAY "MS T120 O4 L4 C D E F G"
PRINT "Legato:"
PLAY "ML T120 O4 L4 C D E F G"

' Background music
PRINT "Background music test..."
PLAY "MB T120 O4 L2 C E G > C"
x% = 0
DO WHILE PLAY(0) > 0
    x% = x% + 1
LOOP
PRINT "Loop iterations during music:"; x%
PRINT "=== Milestone passed ==="
```

**Expected output (`tests/verify/phase9_expected/milestone.txt`):**
```
=== Sound & Music Demo ===
Laser sound...
Short melody...
Staccato:
Legato:
Background music test...
Loop iterations during music: <N>
=== Milestone passed ===
```
*(Note: loop count varies by system speed)*

---

## 8. Makefile Updates

```makefile
# SDL2 audio is part of SDL2, no separate library needed
# Already linked via sdl2-config --libs from Phase 8

ifeq ($(USE_SDL2),1)
    SRC += src/sound.c src/play_parser.c
else
    SRC += src/sound_stub.c
endif
```

---

## 9. Phase 9 Completion Checklist

| # | Component | Acceptance Criteria |
|---|-----------|-------------------|
| 1 | **SDL2 audio init** | Audio device opens at 44100 Hz, 16-bit, mono. Callback generates samples. |
| 2 | **SOUND freq, dur** | Plays tone at specified frequency for duration in clock ticks (18.2/sec). Blocks until complete. freq=0 produces silence. |
| 3 | **PLAY string** | Parses and plays all PLAY macro commands. |
| 4 | **Notes A-G** | Correct frequencies for all 7 natural notes across octaves 0-6. |
| 5 | **Sharps/flats** | `#`/`+` raises semitone, `-` lowers semitone. |
| 6 | **Octave control** | `O n` sets octave. `>` increments, `<` decrements. Range 0-6. |
| 7 | **Note length** | `L n` sets default length. Per-note suffix overrides. Values 1-64. |
| 8 | **Tempo** | `T n` sets quarter notes per minute. Range 32-255. |
| 9 | **Dotted notes** | `.` extends note by 50%. Multiple dots compound. |
| 10 | **Rests** | `P n` produces silence for specified length. |
| 11 | **N n** | Play note by number (1-84). N 0 is rest. |
| 12 | **MF/MB** | MF: block until notes finish. MB: play in background while program continues. |
| 13 | **MN/ML/MS** | Normal (7/8), legato (full), staccato (3/4) note articulation. |
| 14 | **PLAY(n) function** | Returns count of notes remaining in background buffer. |
| 15 | **Note queue** | Background queue holds up to 256 notes. Thread-safe with mutex. |
| 16 | **No SDL2 fallback** | Without SDL2, SOUND/PLAY print warnings and calculate timing (delay without audio). |
| 17 | **Milestone** | Laser sound effect, melody, staccato/legato contrast, background music with foreground counting all work. |

---

## 10. Key Implementation Warnings

1. **Thread safety:** The audio callback runs on a separate SDL thread. All access to the note queue must be protected by the mutex. Never access `g_sound.queue` without locking.

2. **SOUND duration units:** FB's SOUND uses clock ticks (18.2 per second), NOT milliseconds. `SOUND 440, 18` plays for approximately 1 second. Convert: `ms = ticks / 18.2 * 1000`.

3. **PLAY state persistence:** PLAY state (octave, tempo, length, mode) persists across multiple PLAY statements. `PLAY "O4 T120"` followed by `PLAY "C D E"` uses octave 4 at tempo 120.

4. **MF blocking semantics:** In MF (foreground) mode, the PLAY statement blocks until ALL queued notes have finished playing. In MB (background) mode, it returns immediately. Programs often use `DO WHILE PLAY(0) > 0 : LOOP` to wait for background music.

5. **Frequency range:** SOUND accepts 37-32767 Hz. PLAY generates frequencies from C0 (16.35 Hz) to B6 (~3951 Hz). Frequencies below 37 Hz may not be audible but should still time correctly.

6. **Audio latency:** SDL2's audio buffer size affects latency. 2048 samples at 44100 Hz = ~46ms latency. Shorter buffers reduce latency but risk underruns. This is acceptable for FB music.

7. **No SDL2 stub:** When compiled without SDL2, provide `sound_stub.c` that prints a warning on first SOUND/PLAY call, then uses `SDL_Delay()` or `Sleep()`/`usleep()` to simulate timing without actual audio output.
