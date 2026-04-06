# Phase 10 — Event Trapping: Detailed Implementation Guide

This document specifies every data structure, algorithm, file change, and API needed to build **ON KEY/TIMER GOSUB, KEY/TIMER ON/OFF/STOP, cooperative polling, and other event trap sources** for the FreeBASIC interpreter. Phase 10 enables real-time interactive programs and timed events.

---

## Project File Structure (Phase 10 additions)

```
fbasic/
├── Makefile                        [MOD]
├── include/
│   ├── ast.h                      [MOD] — trap statement AST nodes
│   ├── parser.h                   [MOD]
│   ├── interpreter.h              [MOD]
│   ├── events.h                   [NEW] — event system API
│   └── ...
├── src/
│   ├── parser.c                   [MOD] — parse ON KEY/TIMER, KEY/TIMER ON/OFF/STOP
│   ├── interpreter.c              [MOD] — cooperative event poll in main loop
│   ├── events.c                   [NEW] — event management, trap dispatch
│   └── ...
└── tests/
    └── verify/
        ├── phase10_on_key.bas     [NEW]
        ├── phase10_on_timer.bas   [NEW]
        ├── phase10_trapping.bas   [NEW]
        ├── phase10_milestone.bas  [NEW]
        └── phase10_expected/      [NEW]
            ├── on_key.txt
            ├── on_timer.txt
            ├── trapping.txt
            └── milestone.txt
```

---

## 1. Event System Architecture

### 1.1 Core Concepts

FB event trapping is **cooperative**: the interpreter checks for pending events **between each statement**. Events are not interrupts — they are polled. This means:

- **ON**: Event checking is active; when event fires, GOSUB to handler
- **OFF**: Event checking is disabled; events are lost
- **STOP**: Event checking is disabled but events are **queued**; when turned ON again, queued event fires immediately

### 1.2 Event Source Types

```c
typedef enum {
    EVT_KEY_1  = 0,    // KEY(1) through KEY(20)
    EVT_KEY_2  = 1,
    EVT_KEY_3  = 2,
    EVT_KEY_4  = 3,
    EVT_KEY_5  = 4,
    EVT_KEY_6  = 5,
    EVT_KEY_7  = 6,
    EVT_KEY_8  = 7,
    EVT_KEY_9  = 8,
    EVT_KEY_10 = 9,
    EVT_KEY_11 = 10,   // User-defined keys (KEY(11)-KEY(20))
    EVT_KEY_12 = 11,
    EVT_KEY_13 = 12,
    EVT_KEY_14 = 13,
    EVT_KEY_15 = 14,
    EVT_KEY_16 = 15,
    EVT_KEY_17 = 16,
    EVT_KEY_18 = 17,
    EVT_KEY_19 = 18,
    EVT_KEY_20 = 19,
    EVT_TIMER  = 20,   // TIMER event
    EVT_COM1   = 21,   // COM(1) event (stub)
    EVT_COM2   = 22,   // COM(2) event (stub)
    EVT_PEN    = 23,   // PEN event (stub)
    EVT_PLAY   = 24,   // PLAY event
    EVT_STRIG0 = 25,   // STRIG(0) event (stub)
    EVT_STRIG2 = 26,   // STRIG(2) event (stub)
    EVT_STRIG4 = 27,   // STRIG(4) event (stub)
    EVT_STRIG6 = 28,   // STRIG(6) event (stub)
    EVT_COUNT  = 29
} EventSource;
```

### 1.3 Event Trap State

```c
typedef enum {
    TRAP_OFF,       // Trapping disabled, events lost
    TRAP_ON,        // Trapping active, dispatch on fire
    TRAP_STOP       // Trapping suspended, events queued
} TrapState;

typedef struct EventTrap {
    TrapState   state;
    int         handler_target;  // Statement index for GOSUB target (-1 = no handler)
    char        handler_label[42];
    int         pending;         // 1 if event fired while STOP'd
    int         in_handler;      // 1 if currently executing handler (prevent re-entry)
} EventTrap;

// Timer-specific state
typedef struct TimerTrap {
    EventTrap  base;
    double     interval_sec;    // Interval in seconds
    double     last_fire_time;  // TIMER value when last fired
} TimerTrap;

// Play-specific state
typedef struct PlayTrap {
    EventTrap  base;
    int        threshold;       // Fire when note count drops below this
} PlayTrap;
```

### 1.4 Event Manager

```c
typedef struct EventManager {
    EventTrap   key_traps[20];   // KEY(1) through KEY(20)
    TimerTrap   timer_trap;
    EventTrap   com_traps[2];    // COM(1), COM(2) — stubs
    EventTrap   pen_trap;        // PEN — stub
    PlayTrap    play_trap;
    EventTrap   strig_traps[4];  // STRIG — stubs

    // Key definitions: KEY(11) through KEY(20) are user-defined
    // Each defined as a scancode + shift-state mask
    struct {
        int         defined;
        uint8_t     shift_mask;  // Shift/Ctrl/Alt state
        uint8_t     scancode;
    } key_defs[10];  // For KEY(11)-KEY(20)

    // Keyboard state buffer for key checking
    int         key_pressed[20]; // 1 if trap key currently pressed
} EventManager;

// Add to Interpreter struct:
EventManager events;
```

---

## 2. Cooperative Polling

### 2.1 Main Loop Integration

The event check runs **between every statement** in the main interpreter loop:

```c
void interp_run(Interpreter* interp) {
    // ... setjmp setup from Phase 6 ...

    while (interp->running && interp->pc < interp->prog->stmt_count) {
        ASTNode* stmt = interp->prog->statements[interp->pc];

        // TRON tracing (Phase 6)
        if (interp->tron_active) printf("[%d]", stmt->line);

        // === EVENT POLLING ===
        events_poll(interp);

        int old_pc = interp->pc;
        exec_statement(interp, stmt);
        if (interp->pc == old_pc) interp->pc++;
    }
}
```

### 2.2 Event Poll Implementation

```c
void events_poll(Interpreter* interp) {
    EventManager* em = &interp->events;

    // --- Check KEY traps ---
    for (int i = 0; i < 20; i++) {
        EventTrap* trap = &em->key_traps[i];
        if (trap->state == TRAP_OFF || trap->in_handler) continue;
        if (trap->handler_target < 0) continue;

        if (em->key_pressed[i]) {
            em->key_pressed[i] = 0; // Consume

            if (trap->state == TRAP_ON) {
                event_dispatch(interp, trap);
            } else if (trap->state == TRAP_STOP) {
                trap->pending = 1;
            }
        }
    }

    // --- Check TIMER trap ---
    {
        EventTrap* trap = &em->timer_trap.base;
        if (trap->state != TRAP_OFF && !trap->in_handler
            && trap->handler_target >= 0) {
            double now = get_timer_seconds();
            if (now - em->timer_trap.last_fire_time >=
                em->timer_trap.interval_sec) {
                em->timer_trap.last_fire_time = now;
                if (trap->state == TRAP_ON) {
                    event_dispatch(interp, trap);
                } else if (trap->state == TRAP_STOP) {
                    trap->pending = 1;
                }
            }
        }
    }

    // --- Check PLAY trap ---
    {
        EventTrap* trap = &em->play_trap.base;
        if (trap->state != TRAP_OFF && !trap->in_handler
            && trap->handler_target >= 0) {
            int notes = sound_notes_remaining(); // From Phase 9
            if (notes < em->play_trap.threshold) {
                if (trap->state == TRAP_ON) {
                    event_dispatch(interp, trap);
                } else if (trap->state == TRAP_STOP) {
                    trap->pending = 1;
                }
            }
        }
    }
}
```

### 2.3 Event Dispatch (GOSUB to Handler)

```c
static void event_dispatch(Interpreter* interp, EventTrap* trap) {
    trap->in_handler = 1;

    // Save return address (like GOSUB)
    gosub_push(interp, interp->pc);

    // Jump to handler
    if (trap->handler_target >= 0) {
        interp->pc = trap->handler_target;
    } else {
        int target = program_find_label(interp->prog, trap->handler_label);
        if (target >= 0) {
            trap->handler_target = target; // Cache for next time
            interp->pc = target;
        }
    }
}
```

### 2.4 RETURN from Event Handler

When RETURN executes inside an event handler, also clear the `in_handler` flag:

```c
static void exec_return(Interpreter* interp, ASTNode* node) {
    // ... existing RETURN logic (pop GOSUB stack, restore pc) ...

    // Check if returning from an event handler
    events_handler_return(interp);
}

void events_handler_return(Interpreter* interp) {
    EventManager* em = &interp->events;

    // Check all traps — clear in_handler for whichever was active
    for (int i = 0; i < 20; i++) {
        if (em->key_traps[i].in_handler) {
            em->key_traps[i].in_handler = 0;
            return;
        }
    }
    if (em->timer_trap.base.in_handler) {
        em->timer_trap.base.in_handler = 0;
        return;
    }
    if (em->play_trap.base.in_handler) {
        em->play_trap.base.in_handler = 0;
        return;
    }
    // ... COM, PEN, STRIG ...
}
```

---

## 3. ON KEY(n) GOSUB

### 3.1 Syntax

```basic
ON KEY(n) GOSUB label       ' Register key trap handler
KEY(n) ON                   ' Enable trapping
KEY(n) OFF                  ' Disable trapping (events lost)
KEY(n) STOP                 ' Suspend trapping (events queued)
```

### 3.2 Default Key Assignments (KEY 1-10)

| KEY(n) | Default Key |
|--------|------------|
| 1 | F1 |
| 2 | F2 |
| 3 | F3 |
| 4 | F4 |
| 5 | F5 |
| 6 | F6 |
| 7 | F7 |
| 8 | F8 |
| 9 | F9 |
| 10 | F10 |
| 11-20 | User-defined |

### 3.3 Parse ON KEY(n) GOSUB

```c
static void parse_on_key_gosub(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // consume ON
    expect(p, TOK_KW_KEY);
    expect(p, TOK_LPAREN);
    int key_num = parse_int_literal(p);
    expect(p, TOK_RPAREN);
    expect(p, TOK_KW_GOSUB);

    char label[42];
    parse_label_or_lineno(p, label);

    ASTNode* node = ast_on_key_gosub(line, key_num, label);
    program_add_stmt(p->prog, node);
}
```

### 3.4 Execute ON KEY(n) GOSUB

```c
static void exec_on_key_gosub(Interpreter* interp, ASTNode* node) {
    int n = node->data.on_key.key_num;
    if (n < 1 || n > 20) {
        fb_runtime_error(interp, FB_ERR_ILLEGAL_FUNCTION_CALL,
                         node->line, "KEY number 1-20 required");
        return;
    }

    EventTrap* trap = &interp->events.key_traps[n - 1];
    strncpy(trap->handler_label, node->data.on_key.label, 41);

    // Resolve label to statement index
    int target = program_find_label(interp->prog, trap->handler_label);
    trap->handler_target = target; // -1 if not found yet
}
```

### 3.5 KEY(n) ON / OFF / STOP

```c
static void exec_key_control(Interpreter* interp, ASTNode* node) {
    int n = node->data.key_control.key_num;
    if (n < 1 || n > 20) {
        fb_runtime_error(interp, FB_ERR_ILLEGAL_FUNCTION_CALL,
                         node->line, "KEY number 1-20 required");
        return;
    }

    EventTrap* trap = &interp->events.key_traps[n - 1];

    switch (node->data.key_control.action) {
        case TRAP_ACTION_ON:
            trap->state = TRAP_ON;
            // If pending event, fire immediately
            if (trap->pending) {
                trap->pending = 0;
                event_dispatch(interp, trap);
            }
            break;
        case TRAP_ACTION_OFF:
            trap->state = TRAP_OFF;
            trap->pending = 0;
            break;
        case TRAP_ACTION_STOP:
            trap->state = TRAP_STOP;
            break;
    }
}
```

---

## 4. Key Detection

### 4.1 Keyboard Input for KEY Trapping

Key traps need to detect specific keys being pressed. Integrate with the INKEY$ input system (Phase 2) and SDL events (Phase 8):

```c
// Called from the input system or SDL event loop
void events_key_press(EventManager* em, int scancode, int shift_state) {
    // Check keys 1-10 (function keys)
    for (int i = 0; i < 10; i++) {
        if (scancode == default_key_scancodes[i]) {
            em->key_pressed[i] = 1;
            return;
        }
    }

    // Check user-defined keys 11-20
    for (int i = 0; i < 10; i++) {
        if (em->key_defs[i].defined &&
            em->key_defs[i].scancode == scancode &&
            (em->key_defs[i].shift_mask == 0 ||
             (em->key_defs[i].shift_mask & shift_state))) {
            em->key_pressed[10 + i] = 1;
            return;
        }
    }
}
```

### 4.2 KEY Statement (Define/Display)

```basic
KEY n, CHR$(shift) + CHR$(scan)   ' Define KEY(n) for n=11-20
KEY LIST                           ' Display all key definitions
KEY ON                             ' Show function key labels on screen
KEY OFF                            ' Hide function key labels
```

```c
static void exec_key_define(Interpreter* interp, ASTNode* node) {
    int n = (int)eval_to_long(interp, node->data.key_def.key_num);
    if (n < 11 || n > 20) {
        // KEY 1-10 with string arg redefines soft key label (display only)
        // KEY 11-20 defines trap keys
        if (n >= 1 && n <= 10) {
            // Store label string (for KEY ON display)
            return;
        }
        fb_runtime_error(interp, FB_ERR_ILLEGAL_FUNCTION_CALL,
                         node->line, "KEY 11-20 for user-defined traps");
        return;
    }

    FBValue def = eval_expr(interp, node->data.key_def.definition);
    const char* str = fbval_to_cstr(&def);
    if (fbstr_len(def.data.str) < 2) {
        fb_runtime_error(interp, FB_ERR_ILLEGAL_FUNCTION_CALL,
                         node->line, "KEY definition needs 2 bytes");
        fbval_release(&def);
        return;
    }

    int idx = n - 11;
    interp->events.key_defs[idx].shift_mask = (uint8_t)str[0];
    interp->events.key_defs[idx].scancode   = (uint8_t)str[1];
    interp->events.key_defs[idx].defined    = 1;
    fbval_release(&def);
}
```

---

## 5. ON TIMER(n) GOSUB

### 5.1 Syntax

```basic
ON TIMER(seconds) GOSUB label   ' Register timer handler
TIMER ON                        ' Enable timer trapping
TIMER OFF                       ' Disable timer trapping
TIMER STOP                      ' Suspend timer trapping
```

### 5.2 Parse & Execute

```c
static void parse_on_timer_gosub(Parser* p) {
    int line = current_token(p)->line;
    advance(p); // ON
    expect(p, TOK_KW_TIMER);
    expect(p, TOK_LPAREN);
    ASTNode* interval = parse_expr(p);
    expect(p, TOK_RPAREN);
    expect(p, TOK_KW_GOSUB);

    char label[42];
    parse_label_or_lineno(p, label);

    ASTNode* node = ast_on_timer_gosub(line, interval, label);
    program_add_stmt(p->prog, node);
}

static void exec_on_timer_gosub(Interpreter* interp, ASTNode* node) {
    FBValue val = eval_expr(interp, node->data.on_timer.interval);
    double interval = fbval_to_double(&val);
    fbval_release(&val);

    if (interval <= 0) {
        fb_runtime_error(interp, FB_ERR_ILLEGAL_FUNCTION_CALL,
                         node->line, "TIMER interval must be > 0");
        return;
    }

    TimerTrap* tt = &interp->events.timer_trap;
    tt->interval_sec = interval;
    tt->last_fire_time = get_timer_seconds();

    strncpy(tt->base.handler_label, node->data.on_timer.label, 41);
    tt->base.handler_target = program_find_label(interp->prog,
                                                  tt->base.handler_label);
}

static void exec_timer_control(Interpreter* interp, ASTNode* node) {
    EventTrap* trap = &interp->events.timer_trap.base;

    switch (node->data.timer_control.action) {
        case TRAP_ACTION_ON:
            trap->state = TRAP_ON;
            interp->events.timer_trap.last_fire_time = get_timer_seconds();
            if (trap->pending) {
                trap->pending = 0;
                event_dispatch(interp, trap);
            }
            break;
        case TRAP_ACTION_OFF:
            trap->state = TRAP_OFF;
            trap->pending = 0;
            break;
        case TRAP_ACTION_STOP:
            trap->state = TRAP_STOP;
            break;
    }
}
```

---

## 6. ON PLAY(n) GOSUB

### 6.1 Syntax

```basic
ON PLAY(threshold) GOSUB label  ' Fire when note count < threshold
PLAY ON
PLAY OFF
PLAY STOP
```

### 6.2 Implementation

```c
static void exec_on_play_gosub(Interpreter* interp, ASTNode* node) {
    FBValue val = eval_expr(interp, node->data.on_play.threshold);
    int threshold = (int)fbval_to_long(&val);
    fbval_release(&val);

    PlayTrap* pt = &interp->events.play_trap;
    pt->threshold = threshold;
    strncpy(pt->base.handler_label, node->data.on_play.label, 41);
    pt->base.handler_target = program_find_label(interp->prog,
                                                  pt->base.handler_label);
}
```

---

## 7. Stub Event Sources

### 7.1 COM, PEN, STRIG

These event sources are hardware-specific and unlikely to be used. Implement as stubs:

```c
// ON COM(n) GOSUB — serial port event
// ON PEN GOSUB — light pen event
// ON STRIG(n) GOSUB — joystick button event

// Parse normally, store handler, but:
// - COM traps never fire (no serial port emulation)
// - PEN traps never fire (no light pen)
// - STRIG traps could fire if joystick support is added via SDL2

static void exec_on_com_gosub(Interpreter* interp, ASTNode* node) {
    fprintf(stderr, "Warning: ON COM GOSUB registered but COM events not supported\n");
    // Store handler anyway
}

static void exec_on_pen_gosub(Interpreter* interp, ASTNode* node) {
    fprintf(stderr, "Warning: ON PEN GOSUB registered but PEN events not supported\n");
}

static void exec_on_strig_gosub(Interpreter* interp, ASTNode* node) {
    // Could be implemented with SDL2 joystick
    fprintf(stderr, "Warning: ON STRIG GOSUB registered but STRIG events not supported\n");
}
```

---

## 8. AST Node Types

```c
// New AST nodes for Phase 10:
AST_ON_KEY_GOSUB,       // ON KEY(n) GOSUB label
AST_KEY_CONTROL,        // KEY(n) ON/OFF/STOP
AST_KEY_DEFINE,         // KEY n, string$
AST_KEY_DISPLAY,        // KEY ON / KEY OFF / KEY LIST
AST_ON_TIMER_GOSUB,     // ON TIMER(n) GOSUB label
AST_TIMER_CONTROL,      // TIMER ON/OFF/STOP
AST_ON_PLAY_GOSUB,      // ON PLAY(n) GOSUB label
AST_PLAY_CONTROL,       // PLAY ON/OFF/STOP
AST_ON_COM_GOSUB,       // ON COM(n) GOSUB label (stub)
AST_COM_CONTROL,        // COM(n) ON/OFF/STOP (stub)
AST_ON_PEN_GOSUB,       // ON PEN GOSUB label (stub)
AST_PEN_CONTROL,        // PEN ON/OFF/STOP (stub)
AST_ON_STRIG_GOSUB,     // ON STRIG(n) GOSUB label (stub)
AST_STRIG_CONTROL,      // STRIG(n) ON/OFF/STOP (stub)
```

---

## 9. Parser Disambiguation

The `ON` keyword starts many different statements. Disambiguate by looking ahead:

```c
static void parse_on_statement(Parser* p) {
    advance(p); // consume ON
    Token* next = current_token(p);

    if (token_is_keyword(next, KW_KEY)) {
        parse_on_key_gosub(p);
    } else if (token_is_keyword(next, KW_TIMER)) {
        parse_on_timer_gosub(p);
    } else if (token_is_keyword(next, KW_PLAY)) {
        parse_on_play_gosub(p);
    } else if (token_is_keyword(next, KW_COM)) {
        parse_on_com_gosub(p);
    } else if (token_is_keyword(next, KW_PEN)) {
        parse_on_pen_gosub(p);
    } else if (token_is_keyword(next, KW_STRIG)) {
        parse_on_strig_gosub(p);
    } else if (token_is_keyword(next, KW_ERROR)) {
        parse_on_error(p);  // Phase 6
    } else {
        // ON expr GOTO/GOSUB — Phase 4 computed branch
        parse_on_goto_gosub(p);
    }
}
```

---

## 10. Verification Test Files

### 10.1 `tests/verify/phase10_on_key.bas`

```basic
REM Phase 10 Test: ON KEY GOSUB
DIM count%
count% = 0

ON KEY(1) GOSUB F1Handler
KEY(1) ON

PRINT "Press F1 (simulation)..."
' In a real test, we'd need to simulate keypress
' For automated testing, directly trigger the key
PRINT "Trapping enabled"
KEY(1) STOP
PRINT "Trapping stopped"
KEY(1) ON
PRINT "Trapping resumed"
KEY(1) OFF
PRINT "Trapping disabled"
GOTO Done

F1Handler:
count% = count% + 1
PRINT "F1 pressed! Count:"; count%
RETURN

Done:
PRINT "Done"
END
```

**Expected output (`tests/verify/phase10_expected/on_key.txt`):**
```
Press F1 (simulation)...
Trapping enabled
Trapping stopped
Trapping resumed
Trapping disabled
Done
```

### 10.2 `tests/verify/phase10_on_timer.bas`

```basic
REM Phase 10 Test: ON TIMER GOSUB
DIM ticks%
ticks% = 0

ON TIMER(1) GOSUB TimerHandler
TIMER ON

PRINT "Waiting for timer events..."
' Run a tight loop for a few seconds
DIM start!
start! = TIMER
DO WHILE TIMER - start! < 3.5
    ' Busy loop — timer fires every 1 second
LOOP

TIMER OFF
PRINT "Timer ticks:"; ticks%
PRINT "Expected: 3 ticks in 3.5 seconds"
GOTO Done

TimerHandler:
ticks% = ticks% + 1
PRINT "Tick:"; ticks%
RETURN

Done:
PRINT "Done"
END
```

**Expected output (`tests/verify/phase10_expected/on_timer.txt`):**
```
Waiting for timer events...
Tick: 1
Tick: 2
Tick: 3
Timer ticks: 3
Expected: 3 ticks in 3.5 seconds
Done
```

### 10.3 `tests/verify/phase10_trapping.bas`

```basic
REM Phase 10 Test: STOP/ON behavior
DIM count%
count% = 0

ON TIMER(.5) GOSUB Tick
TIMER STOP

' Wait 2 seconds with trapping stopped
DIM start!
start! = TIMER
DO WHILE TIMER - start! < 2
LOOP

PRINT "Events pending (should queue 1)"
TIMER ON
' The pending event fires immediately on ON
' Then wait for 1 more
start! = TIMER
DO WHILE TIMER - start! < 1
LOOP

TIMER OFF
PRINT "Final count:"; count%
GOTO Done

Tick:
count% = count% + 1
PRINT "Tick:"; count%
RETURN

Done:
PRINT "Done"
```

**Expected output (`tests/verify/phase10_expected/trapping.txt`):**
```
Events pending (should queue 1)
Tick: 1
Tick: 2
Tick: 3
Final count: 3
Done
```

### 10.4 `tests/verify/phase10_milestone.bas` — Milestone

```basic
REM Phase 10 Milestone: Real-time interactive program
DIM seconds%
seconds% = 0

ON TIMER(1) GOSUB Clock
TIMER ON

PRINT "=== Real-time Clock ==="
PRINT "(Running for 5 seconds)"

DIM start!
start! = TIMER
DO WHILE TIMER - start! < 5.5
    ' Main program loop doing other work
    FOR i% = 1 TO 1000: NEXT i%
LOOP

TIMER OFF
PRINT
PRINT "Clock updated"; seconds%; "times"
IF seconds% >= 4 AND seconds% <= 6 THEN
    PRINT "=== Milestone passed ==="
ELSE
    PRINT "=== Milestone FAILED ==="
END IF
END

Clock:
seconds% = seconds% + 1
PRINT "Time:"; TIME$; "  ";
RETURN
```

**Expected output (`tests/verify/phase10_expected/milestone.txt`):**
```
=== Real-time Clock ===
(Running for 5 seconds)
Time: <HH:MM:SS>  Time: <HH:MM:SS>  Time: <HH:MM:SS>  Time: <HH:MM:SS>  Time: <HH:MM:SS>
Clock updated 5 times
=== Milestone passed ===
```

---

## 11. Makefile Updates

```makefile
SRC += src/events.c
```

---

## 12. Phase 10 Completion Checklist

| # | Component | Acceptance Criteria |
|---|-----------|-------------------|
| 1 | **EventManager** | All event trap sources initialized. State tracked per source (OFF/ON/STOP). |
| 2 | **Cooperative polling** | `events_poll()` called between every statement in interpreter main loop. |
| 3 | **ON KEY(n) GOSUB** | Registers handler for KEY(1)-KEY(20). |
| 4 | **KEY(n) ON/OFF/STOP** | ON enables trapping; OFF disables and discards; STOP queues. |
| 5 | **Default key assignments** | KEY(1)-KEY(10) map to F1-F10 by default. |
| 6 | **User-defined keys** | KEY n, CHR$(shift) + CHR$(scan) defines KEY(11)-KEY(20). |
| 7 | **ON TIMER(n) GOSUB** | Registers timer handler with interval in seconds. |
| 8 | **TIMER ON/OFF/STOP** | Controls timer trapping. Fires periodically based on wall-clock time. |
| 9 | **ON PLAY(n) GOSUB** | Registers handler when background note count drops below threshold. |
| 10 | **PLAY ON/OFF/STOP** | Controls play event trapping. |
| 11 | **Event dispatch** | GOSUB to handler on event fire. RETURN clears in_handler flag. No re-entrant dispatch. |
| 12 | **STOP → ON resumes** | Pending event from STOP state fires immediately when switched to ON. Only one event queued per source. |
| 13 | **ON/KEY/TIMER stubs** | COM, PEN, STRIG parse and store but never fire (warning on registration). |
| 14 | **KEY ON/OFF/LIST** | Display function key labels (bottom screen row). KEY LIST shows definitions. |
| 15 | **Parser disambiguation** | `ON` keyword correctly routes to KEY/TIMER/PLAY/COM/PEN/STRIG/ERROR/GOTO/GOSUB based on next token. |
| 16 | **Milestone** | Real-time clock demo fires timer ~5 times in 5 seconds with consistent behavior. |

---

## 13. Key Implementation Warnings

1. **Performance impact:** Calling `events_poll()` between every statement adds overhead. Keep the poll function fast — quick checks of boolean flags and a single `get_timer_seconds()` call. Avoid system calls in the poll path if possible (read cached time).

2. **One pending event per source:** When STOP is active, only ONE event is queued per source (not a backlog). If the timer fires 5 times during STOP, only one pending event exists.

3. **No re-entrant handlers:** If a timer handler is executing and the timer fires again, the new event is ignored (not queued). The `in_handler` flag prevents this.

4. **RETURN is special in handlers:** RETURN from an event handler must clear the correct `in_handler` flag. Track which handler is currently executing. If RETURN is called in a regular GOSUB (not event handler), it should work normally.

5. **Key detection platform differences:** On Windows console mode, function keys come through `ReadConsoleInput`. With SDL2, they come as `SDL_KEYDOWN` events. The console path (Phase 2 INKEY$) and the SDL path (Phase 8) must both feed into `events_key_press()`.

6. **TIMER resolution:** FB's cooperative polling means timer events can't fire faster than the statement execution rate. If statements take 10ms each, a 1ms timer interval effectively fires every ~10ms. Document this behavior.

7. **GET\_TIMER\_SECONDS caching:** To avoid calling clock functions every statement, cache the timer value and refresh it only every N statements or when a TIME/TIMER-sensitive operation occurs.
