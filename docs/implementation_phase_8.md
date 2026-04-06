# Phase 8 — Graphics (SDL2): Detailed Implementation Guide

This document specifies every data structure, algorithm, file change, and API needed to build **SCREEN modes, pixel operations, LINE, CIRCLE, PAINT, DRAW, and GET/PUT graphics** for the FreeBASIC interpreter. Phase 8 enables drawing programs, retro games, and data visualization. All graphics are rendered via SDL2.

---

## Project File Structure (Phase 8 additions)

```
fbasic/
├── Makefile                        [MOD] — link SDL2
├── include/
│   ├── ast.h                      [MOD] — graphics AST nodes
│   ├── parser.h                   [MOD]
│   ├── interpreter.h              [MOD]
│   ├── graphics.h                 [NEW] — FBScreen, framebuffer, palette, drawing API
│   └── ...
├── src/
│   ├── parser.c                   [MOD] — parse SCREEN, PSET, LINE, CIRCLE, etc.
│   ├── interpreter.c              [MOD] — dispatch graphics statements
│   ├── graphics.c                 [NEW] — SDL2 window, framebuffer, drawing primitives
│   ├── draw_parser.c              [NEW] — DRAW macro language tokenizer/executor
│   └── ...
└── tests/
    └── verify/
        ├── phase8_screen.bas      [NEW]
        ├── phase8_pset.bas        [NEW]
        ├── phase8_line.bas        [NEW]
        ├── phase8_circle.bas      [NEW]
        ├── phase8_paint.bas       [NEW]
        ├── phase8_draw.bas        [NEW]
        ├── phase8_getput.bas      [NEW]
        ├── phase8_milestone.bas   [NEW]
        └── phase8_expected/       [NEW]
            └── (screenshots or text descriptions)
```

---

## 1. Graphics Subsystem Architecture

### 1.1 FBScreen Data Structure

```c
typedef struct FBPalette {
    uint8_t r, g, b;
} FBPalette;

typedef struct FBScreen {
    // SDL resources
    SDL_Window*   window;
    SDL_Renderer* renderer;
    SDL_Texture*  texture;

    // Framebuffer (indexed color)
    uint8_t*      framebuffer;      // width * height bytes (palette index)
    int           width;
    int           height;
    int           mode;             // SCREEN mode number
    int           num_colors;       // Number of palette entries
    int           pages;            // Number of video pages
    int           active_page;      // Page being drawn to
    int           visual_page;      // Page being displayed

    // Text overlay (for PRINT in graphics mode)
    int           text_cols;        // Characters per row
    int           text_rows;        // Character rows
    int           cursor_x;         // Text cursor column (0-based)
    int           cursor_y;         // Text cursor row (0-based)
    int           font_w;           // Font character width in pixels
    int           font_h;           // Font character height in pixels

    // Drawing state
    int           fg_color;         // Current foreground color
    int           bg_color;         // Current background color
    int           last_x;           // Last referenced X coordinate
    int           last_y;           // Last referenced Y coordinate

    // WINDOW coordinate mapping
    int           window_active;    // 1 if WINDOW is set
    double        wx1, wy1;        // World coordinate bounds (lower-left)
    double        wx2, wy2;        // World coordinate bounds (upper-right)
    int           window_screen;    // 1 if WINDOW SCREEN (Y increases downward)

    // VIEW clipping
    int           view_active;
    int           vx1, vy1;        // Viewport bounds in pixels
    int           vx2, vy2;

    // Palette
    FBPalette*    palette;          // Array of num_colors entries
    FBPalette*    default_palette;  // Mode's default palette

    // Multiple pages
    uint8_t**     page_buffers;     // Array of framebuffers (one per page)

    // Initialized?
    int           initialized;
} FBScreen;
```

### 1.2 Initialization / Shutdown

```c
static FBScreen g_screen = {0};

int graphics_init(int mode) {
    if (!g_screen.initialized) {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return -1;
        }
    }

    // Clean up previous mode
    if (g_screen.texture) SDL_DestroyTexture(g_screen.texture);
    if (g_screen.framebuffer) free(g_screen.framebuffer);
    // etc.

    // Set mode parameters
    set_mode_params(&g_screen, mode);

    // Create/resize window
    if (!g_screen.window) {
        g_screen.window = SDL_CreateWindow("FreeBASIC",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            g_screen.width * 2, g_screen.height * 2,  // 2x scale
            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        g_screen.renderer = SDL_CreateRenderer(g_screen.window, -1,
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    }

    // Create texture for streaming
    g_screen.texture = SDL_CreateTexture(g_screen.renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        g_screen.width, g_screen.height);

    // Allocate framebuffer
    g_screen.framebuffer = calloc(g_screen.width * g_screen.height, 1);

    // Allocate page buffers
    g_screen.page_buffers = calloc(g_screen.pages, sizeof(uint8_t*));
    for (int i = 0; i < g_screen.pages; i++) {
        g_screen.page_buffers[i] = calloc(g_screen.width * g_screen.height, 1);
    }
    g_screen.active_page = 0;
    g_screen.visual_page = 0;
    g_screen.framebuffer = g_screen.page_buffers[0];

    // Load default palette
    load_default_palette(&g_screen, mode);

    g_screen.initialized = 1;
    return 0;
}

void graphics_shutdown(void) {
    if (g_screen.texture)  SDL_DestroyTexture(g_screen.texture);
    if (g_screen.renderer) SDL_DestroyRenderer(g_screen.renderer);
    if (g_screen.window)   SDL_DestroyWindow(g_screen.window);
    SDL_Quit();
    // Free page buffers, palette, etc.
}
```

---

## 2. SCREEN Modes

### 2.1 Mode Table

```c
typedef struct {
    int mode;
    int width;
    int height;
    int colors;
    int pages;
    int font_w;
    int font_h;
    int text_cols;
    int text_rows;
} ScreenModeInfo;

static const ScreenModeInfo mode_table[] = {
    // mode  w    h    colors  pages  fw  fh  cols rows
    { 0,    720, 400,  16,    1,     9,  16, 80,  25 },  // Text mode
    { 1,    320, 200,  4,     1,     8,   8, 40,  25 },  // CGA 4-color
    { 2,    640, 200,  2,     1,     8,   8, 80,  25 },  // CGA 2-color
    { 7,    320, 200,  16,    8,     8,   8, 40,  25 },  // EGA lo-res
    { 8,    640, 200,  16,    4,     8,   8, 80,  25 },  // EGA hi-res
    { 9,    640, 350,  16,    2,     8,  14, 80,  25 },  // EGA color
    { 10,   640, 350,  4,     2,     8,  14, 80,  25 },  // EGA mono
    { 11,   640, 480,  2,     1,     8,  16, 80,  30 },  // VGA 2-color
    { 12,   640, 480,  16,    1,     8,  16, 80,  30 },  // VGA 16-color
    { 13,   320, 200,  256,   1,     8,   8, 40,  25 },  // MCGA 256-color
    { -1,   0,   0,    0,     0,     0,   0,  0,   0 },  // Sentinel
};
```

### 2.2 SCREEN Statement

```basic
SCREEN mode [, [colorswitch] [, [activepage] [, visualpage]]]
```

```c
static void exec_screen(Interpreter* interp, ASTNode* node) {
    int mode = (int)eval_to_long(interp, node->data.screen_stmt.mode);
    int active = -1, visual = -1;

    if (node->data.screen_stmt.active_page)
        active = (int)eval_to_long(interp, node->data.screen_stmt.active_page);
    if (node->data.screen_stmt.visual_page)
        visual = (int)eval_to_long(interp, node->data.screen_stmt.visual_page);

    if (mode == 0) {
        // Text mode — close graphics window if open
        if (g_screen.initialized) {
            graphics_shutdown();
            g_screen.initialized = 0;
        }
        return;
    }

    if (graphics_init(mode) < 0) {
        fb_runtime_error(interp, FB_ERR_ILLEGAL_FUNCTION_CALL,
                         node->line, "Cannot set SCREEN mode");
        return;
    }

    if (active >= 0) g_screen.active_page = active;
    if (visual >= 0) g_screen.visual_page = visual;
    g_screen.framebuffer = g_screen.page_buffers[g_screen.active_page];

    graphics_present();
}
```

### 2.3 Framebuffer → SDL Presentation

```c
void graphics_present(void) {
    if (!g_screen.initialized) return;

    // Convert indexed framebuffer to ARGB8888
    uint32_t* pixels;
    int pitch;
    SDL_LockTexture(g_screen.texture, NULL, (void**)&pixels, &pitch);

    uint8_t* fb = g_screen.page_buffers[g_screen.visual_page];
    for (int y = 0; y < g_screen.height; y++) {
        for (int x = 0; x < g_screen.width; x++) {
            uint8_t idx = fb[y * g_screen.width + x];
            FBPalette* c = &g_screen.palette[idx % g_screen.num_colors];
            pixels[y * (pitch / 4) + x] =
                0xFF000000u | (c->r << 16) | (c->g << 8) | c->b;
        }
    }

    SDL_UnlockTexture(g_screen.texture);
    SDL_RenderClear(g_screen.renderer);
    SDL_RenderCopy(g_screen.renderer, g_screen.texture, NULL, NULL);
    SDL_RenderPresent(g_screen.renderer);

    // Process SDL events (keep window responsive)
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            // Could set interp->running = 0
        }
    }
}
```

---

## 3. Coordinate Systems

### 3.1 WINDOW Statement

Maps world coordinates to physical pixels.

```basic
WINDOW [[SCREEN] (x1, y1)-(x2, y2)]
```

```c
// Convert world → physical coordinates
static int world_to_phys_x(double wx) {
    if (!g_screen.window_active) return (int)wx;
    int vx1 = g_screen.view_active ? g_screen.vx1 : 0;
    int vx2 = g_screen.view_active ? g_screen.vx2 : g_screen.width - 1;
    return vx1 + (int)((wx - g_screen.wx1) / (g_screen.wx2 - g_screen.wx1)
                        * (vx2 - vx1));
}

static int world_to_phys_y(double wy) {
    if (!g_screen.window_active) return (int)wy;
    int vy1 = g_screen.view_active ? g_screen.vy1 : 0;
    int vy2 = g_screen.view_active ? g_screen.vy2 : g_screen.height - 1;
    if (g_screen.window_screen) {
        // WINDOW SCREEN: Y increases downward (same as physical)
        return vy1 + (int)((wy - g_screen.wy1) / (g_screen.wy2 - g_screen.wy1)
                            * (vy2 - vy1));
    } else {
        // Standard WINDOW: Y increases upward (Cartesian)
        return vy2 - (int)((wy - g_screen.wy1) / (g_screen.wy2 - g_screen.wy1)
                            * (vy2 - vy1));
    }
}
```

### 3.2 STEP (Relative Coordinates)

Most graphics commands support `STEP(dx, dy)` for relative coordinates from `last_x, last_y`:

```c
static void resolve_coords(Interpreter* interp, ASTNode* coord,
                           int* px, int* py) {
    double x = eval_to_double(interp, coord->data.coord.x);
    double y = eval_to_double(interp, coord->data.coord.y);

    if (coord->data.coord.is_step) {
        x += g_screen.last_x;
        y += g_screen.last_y;
    }

    *px = world_to_phys_x(x);
    *py = world_to_phys_y(y);
}
```

### 3.3 VIEW Statement

```basic
VIEW [[SCREEN] (x1, y1)-(x2, y2) [, [fillcolor] [, bordercolor]]]
```

Sets the graphics viewport clipping rectangle.

```c
static void exec_view(Interpreter* interp, ASTNode* node) {
    if (!node->data.view.has_coords) {
        g_screen.view_active = 0;
        return;
    }
    g_screen.vx1 = (int)eval_to_long(interp, node->data.view.x1);
    g_screen.vy1 = (int)eval_to_long(interp, node->data.view.y1);
    g_screen.vx2 = (int)eval_to_long(interp, node->data.view.x2);
    g_screen.vy2 = (int)eval_to_long(interp, node->data.view.y2);
    g_screen.view_active = 1;

    if (node->data.view.has_fill) {
        int fill = (int)eval_to_long(interp, node->data.view.fill_color);
        for (int y = g_screen.vy1; y <= g_screen.vy2; y++)
            for (int x = g_screen.vx1; x <= g_screen.vx2; x++)
                plot_pixel(x, y, fill);
    }
}
```

### 3.4 PMAP Function

```basic
PMAP(expression, function)
' function: 0=world X→phys X, 1=world Y→phys Y
'           2=phys X→world X, 3=phys Y→world Y
```

---

## 4. Pixel Operations

### 4.1 Core Pixel Access

```c
static inline void plot_pixel(int x, int y, int color) {
    // Clip to viewport
    int cx1 = g_screen.view_active ? g_screen.vx1 : 0;
    int cy1 = g_screen.view_active ? g_screen.vy1 : 0;
    int cx2 = g_screen.view_active ? g_screen.vx2 : g_screen.width - 1;
    int cy2 = g_screen.view_active ? g_screen.vy2 : g_screen.height - 1;

    if (x < cx1 || x > cx2 || y < cy1 || y > cy2) return;
    g_screen.framebuffer[y * g_screen.width + x] = (uint8_t)color;
}

static inline int read_pixel(int x, int y) {
    if (x < 0 || x >= g_screen.width || y < 0 || y >= g_screen.height)
        return 0;
    return g_screen.framebuffer[y * g_screen.width + x];
}
```

### 4.2 PSET / PRESET

```basic
PSET [STEP] (x, y) [, color]   ' Set pixel to foreground or specified color
PRESET [STEP] (x, y) [, color] ' Set pixel to background or specified color
```

```c
static void exec_pset(Interpreter* interp, ASTNode* node) {
    int px, py;
    resolve_coords(interp, node->data.pset.coord, &px, &py);

    int color;
    if (node->data.pset.has_color) {
        color = (int)eval_to_long(interp, node->data.pset.color);
    } else {
        color = node->data.pset.is_preset ? g_screen.bg_color : g_screen.fg_color;
    }

    plot_pixel(px, py, color);
    g_screen.last_x = px;
    g_screen.last_y = py;
    graphics_present();
}
```

### 4.3 POINT Function

```basic
POINT(x, y)   ' Return color at pixel (x,y)
POINT(n)       ' n=0: last X, n=1: last Y, n=2: foreground, n=3: background
```

```c
static FBValue builtin_point(Interpreter* interp, FBValue* args, int nargs) {
    if (nargs == 2) {
        int x = (int)fbval_to_long(&args[0]);
        int y = (int)fbval_to_long(&args[1]);
        return fbval_int((int16_t)read_pixel(x, y));
    } else {
        int n = (int)fbval_to_long(&args[0]);
        switch (n) {
            case 0: return fbval_single((float)g_screen.last_x);
            case 1: return fbval_single((float)g_screen.last_y);
            case 2: return fbval_int((int16_t)g_screen.fg_color);
            case 3: return fbval_int((int16_t)g_screen.bg_color);
            default: return fbval_int(-1);
        }
    }
}
```

---

## 5. LINE Statement

### 5.1 Syntax

```basic
LINE [[STEP](x1,y1)] - [STEP](x2,y2) [, [color] [, [B|BF] [, style]]]
```

### 5.2 Bresenham's Line Algorithm

```c
static void draw_line_raw(int x0, int y0, int x1, int y1,
                           int color, uint16_t style) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int bit = 0;

    while (1) {
        if (style == 0xFFFF || (style >> (15 - (bit & 15))) & 1) {
            plot_pixel(x0, y0, color);
        }
        bit++;

        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
```

### 5.3 Box and Filled Box

```c
static void draw_box(int x1, int y1, int x2, int y2,
                     int color, uint16_t style) {
    draw_line_raw(x1, y1, x2, y1, color, style); // Top
    draw_line_raw(x2, y1, x2, y2, color, style); // Right
    draw_line_raw(x2, y2, x1, y2, color, style); // Bottom
    draw_line_raw(x1, y2, x1, y1, color, style); // Left
}

static void draw_filled_box(int x1, int y1, int x2, int y2, int color) {
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            plot_pixel(x, y, color);
        }
    }
}
```

### 5.4 LINE Execute

```c
static void exec_line(Interpreter* interp, ASTNode* node) {
    int x1, y1, x2, y2;

    if (node->data.line_stmt.has_start) {
        resolve_coords(interp, node->data.line_stmt.start, &x1, &y1);
    } else {
        x1 = g_screen.last_x;
        y1 = g_screen.last_y;
    }
    resolve_coords(interp, node->data.line_stmt.end, &x2, &y2);

    int color = g_screen.fg_color;
    if (node->data.line_stmt.has_color)
        color = (int)eval_to_long(interp, node->data.line_stmt.color);

    uint16_t style = 0xFFFF;
    if (node->data.line_stmt.has_style)
        style = (uint16_t)eval_to_long(interp, node->data.line_stmt.style);

    switch (node->data.line_stmt.box_mode) {
        case LINE_NORMAL:
            draw_line_raw(x1, y1, x2, y2, color, style);
            break;
        case LINE_BOX:
            draw_box(x1, y1, x2, y2, color, style);
            break;
        case LINE_BOX_FILLED:
            draw_filled_box(x1, y1, x2, y2, color);
            break;
    }

    g_screen.last_x = x2;
    g_screen.last_y = y2;
    graphics_present();
}
```

---

## 6. CIRCLE Statement

### 6.1 Syntax

```basic
CIRCLE [STEP](x, y), radius [, [color] [, [start] [, [end] [, aspect]]]]
```

### 6.2 Midpoint Circle Algorithm (with aspect ratio)

```c
static void draw_circle(int cx, int cy, int r, int color,
                        double start_angle, double end_angle,
                        double aspect) {
    if (aspect == 0) aspect = 1.0;

    // Full circle if start==end==0
    int full_circle = (start_angle == 0.0 && end_angle == 0.0);
    if (full_circle) {
        start_angle = 0.0;
        end_angle = 2.0 * M_PI;
    }

    // Negative angles → draw radius lines to center
    int draw_start_radius = (start_angle < 0);
    int draw_end_radius = (end_angle < 0);
    if (start_angle < 0) start_angle = -start_angle;
    if (end_angle < 0) end_angle = -end_angle;

    // Step through angles
    double step = 1.0 / (double)r;  // ~1 pixel per step
    if (step > 0.01) step = 0.01;

    double rx = (aspect >= 1.0) ? r : r * aspect;
    double ry = (aspect >= 1.0) ? r / aspect : r;

    double a = start_angle;
    while (a <= end_angle) {
        int px = cx + (int)(rx * cos(a));
        int py = cy - (int)(ry * sin(a));
        plot_pixel(px, py, color);
        a += step;
    }

    // Draw radius lines to center if negative angles
    if (draw_start_radius) {
        int sx = cx + (int)(rx * cos(start_angle));
        int sy = cy - (int)(ry * sin(start_angle));
        draw_line_raw(cx, cy, sx, sy, color, 0xFFFF);
    }
    if (draw_end_radius) {
        int ex = cx + (int)(rx * cos(end_angle));
        int ey = cy - (int)(ry * sin(end_angle));
        draw_line_raw(cx, cy, ex, ey, color, 0xFFFF);
    }

    g_screen.last_x = cx;
    g_screen.last_y = cy;
}
```

---

## 7. PAINT (Flood Fill)

### 7.1 Syntax

```basic
PAINT [STEP](x, y) [, [paintcolor] [, [bordercolor] [, background$]]]
```

### 7.2 Scanline Flood Fill Algorithm

```c
typedef struct { int x, y; } FillPoint;

static void paint_fill(int sx, int sy, int paint_color, int border_color) {
    // Stack-based scanline fill
    FillPoint* stack = malloc(sizeof(FillPoint) * g_screen.width * g_screen.height);
    int sp = 0;

    int start_color = read_pixel(sx, sy);
    if (start_color == paint_color || start_color == border_color) {
        free(stack);
        return;
    }

    stack[sp++] = (FillPoint){sx, sy};

    while (sp > 0) {
        FillPoint p = stack[--sp];
        int x = p.x, y = p.y;

        if (x < 0 || x >= g_screen.width || y < 0 || y >= g_screen.height)
            continue;

        int c = read_pixel(x, y);
        if (c == paint_color || c == border_color) continue;

        // Scan left
        int lx = x;
        while (lx >= 0 && read_pixel(lx, y) != border_color
                       && read_pixel(lx, y) != paint_color) lx--;
        lx++;

        // Scan right
        int rx = x;
        while (rx < g_screen.width && read_pixel(rx, y) != border_color
                                   && read_pixel(rx, y) != paint_color) rx++;
        rx--;

        // Fill the scanline
        for (int i = lx; i <= rx; i++) {
            plot_pixel(i, y, paint_color);
        }

        // Push scanlines above and below
        for (int i = lx; i <= rx; i++) {
            if (y > 0) {
                int above = read_pixel(i, y - 1);
                if (above != border_color && above != paint_color)
                    stack[sp++] = (FillPoint){i, y - 1};
            }
            if (y < g_screen.height - 1) {
                int below = read_pixel(i, y + 1);
                if (below != border_color && below != paint_color)
                    stack[sp++] = (FillPoint){i, y + 1};
            }
        }
    }

    free(stack);
}
```

**Note:** Production code should use an optimized scanline fill that avoids pushing duplicate spans. The above is a simplified version.

### 7.3 Pattern Fill

When `background$` is specified, PAINT uses a tile pattern:

```c
// background$ is a string where each byte is a bitmask for 8 pixels
// The pattern repeats every LEN(background$) scanlines
static void paint_fill_pattern(int sx, int sy, int paint_color,
                               int border_color,
                               const char* pattern, int pattern_len) {
    // Same scanline fill but use pattern to determine pixel color:
    // bit = (x % 8), row = (y % pattern_len)
    // if pattern[row] bit is set → paint_color, else → leave as-is
}
```

---

## 8. DRAW Macro Language

### 8.1 Command Reference

| Command | Action |
|---------|--------|
| `U n` | Move up n pixels |
| `D n` | Move down n pixels |
| `L n` | Move left n pixels |
| `R n` | Move right n pixels |
| `E n` | Move diagonal up-right |
| `F n` | Move diagonal down-right |
| `G n` | Move diagonal down-left |
| `H n` | Move diagonal up-left |
| `M x,y` | Move to (x,y). If prefixed with `+`/`-`, relative. |
| `B` | Prefix: move without drawing |
| `N` | Prefix: return to start after drawing |
| `C n` | Set color to n |
| `S n` | Set scale factor (default 4, units = n/4 pixels) |
| `A n` | Set angle (0=right, 1=up, 2=left, 3=down — 90° rotations) |
| `TA n` | Set angle in degrees (-360 to 360) |
| `P paint,border` | Paint enclosed area |
| `X varptr$(...)` | Execute substring |

### 8.2 DRAW Parser

```c
// draw_parser.c

typedef struct {
    const char* src;
    int         pos;
    int         len;
    int         cur_x, cur_y;
    int         color;
    int         scale;      // Default 4 (n/4 pixels per unit)
    int         angle;      // Degrees (0, 90, 180, 270 for A; arbitrary for TA)
    int         no_draw;    // B prefix
    int         no_move;    // N prefix
} DrawState;

void draw_execute(const char* cmd_string, int start_x, int start_y) {
    DrawState ds = {0};
    ds.src = cmd_string;
    ds.len = (int)strlen(cmd_string);
    ds.cur_x = start_x;
    ds.cur_y = start_y;
    ds.color = g_screen.fg_color;
    ds.scale = 4;

    while (ds.pos < ds.len) {
        skip_spaces(&ds);
        if (ds.pos >= ds.len) break;

        char c = toupper(ds.src[ds.pos++]);

        switch (c) {
            case 'B': ds.no_draw = 1; continue; // Prefix
            case 'N': ds.no_move = 1; continue; // Prefix

            case 'U': draw_direction(&ds,  0, -1); break;
            case 'D': draw_direction(&ds,  0,  1); break;
            case 'L': draw_direction(&ds, -1,  0); break;
            case 'R': draw_direction(&ds,  1,  0); break;
            case 'E': draw_direction(&ds,  1, -1); break;
            case 'F': draw_direction(&ds,  1,  1); break;
            case 'G': draw_direction(&ds, -1,  1); break;
            case 'H': draw_direction(&ds, -1, -1); break;

            case 'M': draw_move_absolute(&ds); break;
            case 'C': ds.color = parse_int(&ds); break;
            case 'S': ds.scale = parse_int(&ds); break;
            case 'A': ds.angle = parse_int(&ds) * 90; break;
            case 'T':
                if (ds.pos < ds.len && toupper(ds.src[ds.pos]) == 'A') {
                    ds.pos++;
                    ds.angle = parse_int(&ds);
                }
                break;
            case 'P': {
                int paint = parse_int(&ds);
                skip_comma(&ds);
                int border = parse_int(&ds);
                paint_fill(ds.cur_x, ds.cur_y, paint, border);
                break;
            }
        }

        ds.no_draw = 0;
        ds.no_move = 0;
    }

    g_screen.last_x = ds.cur_x;
    g_screen.last_y = ds.cur_y;
}

static void draw_direction(DrawState* ds, int dx, int dy) {
    int n = parse_optional_int(ds, 1); // Default 1 unit
    int pixels = n * ds->scale / 4;

    // Apply rotation
    double rad = ds->angle * M_PI / 180.0;
    int rdx = (int)(dx * cos(rad) - dy * sin(rad));
    int rdy = (int)(dx * sin(rad) + dy * cos(rad));

    int x0 = ds->cur_x, y0 = ds->cur_y;
    int x1 = x0 + rdx * pixels;
    int y1 = y0 + rdy * pixels;

    if (!ds->no_draw) {
        draw_line_raw(x0, y0, x1, y1, ds->color, 0xFFFF);
    }

    if (!ds->no_move) {
        ds->cur_x = x1;
        ds->cur_y = y1;
    }
}
```

---

## 9. GET / PUT (Graphics)

### 9.1 Syntax

```basic
GET [STEP](x1,y1)-[STEP](x2,y2), arrayname
PUT [STEP](x,y), arrayname [, actionverb]
' actionverb: PSET, PRESET, AND, OR, XOR (default XOR)
```

### 9.2 Image Buffer Format

FB stores screen images in integer arrays. First 2 elements store width/height in bits, then pixel data packed by plane.

```c
// GET: capture rectangle into array
static void exec_get_graphics(Interpreter* interp, ASTNode* node) {
    int x1, y1, x2, y2;
    resolve_coords(interp, node->data.get_gfx.start, &x1, &y1);
    resolve_coords(interp, node->data.get_gfx.end, &x2, &y2);

    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }

    int w = x2 - x1 + 1;
    int h = y2 - y1 + 1;

    // Calculate bytes needed
    int bits_per_pixel = bits_per_pixel_for_mode(g_screen.mode);
    int row_bits = w * bits_per_pixel;
    int row_bytes = (row_bits + 7) / 8;
    int total_bytes = 4 + row_bytes * h;  // 4 bytes header
    int ints_needed = (total_bytes + 1) / 2;

    // Resolve target array
    FBArray* arr = resolve_array(interp, node->data.get_gfx.array_name);
    if (!arr || arr->total_elements < ints_needed) {
        fb_runtime_error(interp, FB_ERR_ILLEGAL_FUNCTION_CALL,
                         node->line, "Array too small for GET");
        return;
    }

    // Header: width in bits (16-bit), height in pixels (16-bit)
    int16_t* data = (int16_t*)arr->data;
    data[0] = (int16_t)(w * bits_per_pixel);
    data[1] = (int16_t)h;

    // Copy pixel data (simplified — direct byte copy for 8bpp modes)
    uint8_t* dst = (uint8_t*)&data[2];
    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            *dst++ = (uint8_t)read_pixel(x, y);
        }
    }
}

// PUT: paste image from array with action verb
typedef enum { PUT_PSET, PUT_PRESET, PUT_AND, PUT_OR, PUT_XOR } PutAction;

static void exec_put_graphics(Interpreter* interp, ASTNode* node) {
    int px, py;
    resolve_coords(interp, node->data.put_gfx.coord, &px, &py);

    FBArray* arr = resolve_array(interp, node->data.put_gfx.array_name);
    if (!arr) return;

    int16_t* data = (int16_t*)arr->data;
    int bits_per_pixel = bits_per_pixel_for_mode(g_screen.mode);
    int w = data[0] / bits_per_pixel;
    int h = data[1];

    PutAction action = node->data.put_gfx.action;
    uint8_t* src = (uint8_t*)&data[2];

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t src_color = *src++;
            int dx = px + x;
            int dy = py + y;

            switch (action) {
                case PUT_PSET:
                    plot_pixel(dx, dy, src_color);
                    break;
                case PUT_PRESET:
                    plot_pixel(dx, dy, ~src_color & (g_screen.num_colors - 1));
                    break;
                case PUT_XOR: {
                    int existing = read_pixel(dx, dy);
                    plot_pixel(dx, dy, existing ^ src_color);
                    break;
                }
                case PUT_AND: {
                    int existing = read_pixel(dx, dy);
                    plot_pixel(dx, dy, existing & src_color);
                    break;
                }
                case PUT_OR: {
                    int existing = read_pixel(dx, dy);
                    plot_pixel(dx, dy, existing | src_color);
                    break;
                }
            }
        }
    }

    graphics_present();
}
```

---

## 10. PALETTE Statement

### 10.1 Syntax

```basic
PALETTE [attribute, color]       ' Set one palette entry
PALETTE USING array%             ' Set all entries from array
```

```c
static void exec_palette(Interpreter* interp, ASTNode* node) {
    if (!node->data.palette.has_args) {
        // Reset to default palette
        memcpy(g_screen.palette, g_screen.default_palette,
               g_screen.num_colors * sizeof(FBPalette));
        graphics_present();
        return;
    }

    int attr  = (int)eval_to_long(interp, node->data.palette.attr);
    long color = eval_to_long(interp, node->data.palette.color);

    if (attr < 0 || attr >= g_screen.num_colors) {
        fb_runtime_error(interp, FB_ERR_ILLEGAL_FUNCTION_CALL,
                         node->line, "PALETTE attribute out of range");
        return;
    }

    // EGA/VGA color: 6-bit RGB (each component 0-63)
    g_screen.palette[attr].r = (uint8_t)(((color >> 0) & 0x3F) * 255 / 63);
    g_screen.palette[attr].g = (uint8_t)(((color >> 8) & 0x3F) * 255 / 63);
    g_screen.palette[attr].b = (uint8_t)(((color >> 16) & 0x3F) * 255 / 63);

    graphics_present();
}
```

---

## 11. PCOPY Statement

```basic
PCOPY sourcepage, destpage
```

Copies one video page buffer to another:

```c
static void exec_pcopy(Interpreter* interp, ASTNode* node) {
    int src = (int)eval_to_long(interp, node->data.pcopy.src_page);
    int dst = (int)eval_to_long(interp, node->data.pcopy.dst_page);

    if (src < 0 || src >= g_screen.pages || dst < 0 || dst >= g_screen.pages) {
        fb_runtime_error(interp, FB_ERR_ILLEGAL_FUNCTION_CALL,
                         node->line, "PCOPY: invalid page number");
        return;
    }

    memcpy(g_screen.page_buffers[dst], g_screen.page_buffers[src],
           g_screen.width * g_screen.height);

    if (dst == g_screen.visual_page) graphics_present();
}
```

---

## 12. Text in Graphics Mode

PRINT in graphics modes renders text using a built-in 8×8 or 8×16 bitmap font.

```c
// Built-in 8x8 CP437 font (256 characters × 8 bytes each)
extern const uint8_t cp437_font_8x8[256][8];

void graphics_print_char(int ch, int col, int row, int fg, int bg) {
    int px = col * g_screen.font_w;
    int py = row * g_screen.font_h;

    const uint8_t* glyph = cp437_font_8x8[ch & 0xFF];
    for (int y = 0; y < g_screen.font_h && y < 8; y++) {
        uint8_t bits = glyph[y];
        for (int x = 0; x < g_screen.font_w && x < 8; x++) {
            int color = (bits & (0x80 >> x)) ? fg : bg;
            plot_pixel(px + x, py + y, color);
        }
    }
}
```

---

## 13. SDL2 Event Loop Integration

The SDL2 event loop must be polled periodically to keep the window responsive. Integrate polling into the main interpreter loop:

```c
// Call every N statements or on graphical operations
void graphics_pump_events(void) {
    if (!g_screen.initialized) return;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                // Signal interpreter to stop
                break;
            case SDL_KEYDOWN:
                // Feed to INKEY$ buffer (Phase 2)
                break;
        }
    }
}
```

---

## 14. Verification Test Files

### 14.1 `tests/verify/phase8_screen.bas`

```basic
REM Phase 8 Test: SCREEN modes
SCREEN 13
PRINT "Screen 13 (320x200x256)"
FOR i% = 0 TO 255
    PSET (i% + 32, 100), i%
NEXT i%
SLEEP 1
SCREEN 12
PRINT "Screen 12 (640x480x16)"
FOR i% = 0 TO 15
    LINE (i% * 40, 50)-STEP(35, 20), i%, BF
NEXT i%
SLEEP 1
SCREEN 0
PRINT "Back to text mode"
```

### 14.2 `tests/verify/phase8_line.bas`

```basic
REM Phase 8 Test: LINE statement
SCREEN 12
' Diagonal line
LINE (0, 0)-(639, 479), 15
' Box
LINE (100, 100)-(200, 200), 14, B
' Filled box
LINE (300, 100)-(400, 200), 12, BF
' Styled line (dashed)
LINE (0, 240)-(639, 240), 10, , &HF0F0
SLEEP 2
```

### 14.3 `tests/verify/phase8_circle.bas`

```basic
REM Phase 8 Test: CIRCLE statement
SCREEN 12
' Full circle
CIRCLE (320, 240), 100, 15
' Semi-circle (top half)
CIRCLE (320, 240), 80, 14, 0, 3.14159
' Ellipse (aspect ratio)
CIRCLE (320, 240), 60, 12, , , .5
' Pie slice (negative angles draw radii)
CIRCLE (150, 350), 50, 10, -.1, -3.0
SLEEP 2
```

### 14.4 `tests/verify/phase8_milestone.bas` — Milestone

```basic
REM Phase 8 Milestone: Simple Drawing Program
SCREEN 12
' Draw a house
LINE (200, 350)-(440, 200), 15, B   ' Walls
LINE (200, 200)-(320, 100), 15      ' Roof left
LINE (320, 100)-(440, 200), 15      ' Roof right
LINE (280, 350)-(360, 260), 14, BF  ' Door
CIRCLE (345, 310), 4, 11             ' Doorknob
LINE (220, 230)-(270, 280), 9, B     ' Left window
LINE (370, 230)-(420, 280), 9, B     ' Right window
' Sun
CIRCLE (550, 80), 40, 14
PAINT (550, 80), 14, 14
' Ground
LINE (0, 350)-(639, 479), 2, BF
' Text label
LOCATE 28, 30
PRINT "My House";
' Sky
PAINT (1, 1), 1, 15
SLEEP 3
SCREEN 0
PRINT "Milestone complete!"
```

---

## 15. Makefile Updates

```makefile
# Conditional SDL2 support
USE_SDL2 ?= 1

ifeq ($(USE_SDL2),1)
    SDL2_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
    SDL2_LDFLAGS := $(shell sdl2-config --libs 2>/dev/null)
    CFLAGS += $(SDL2_CFLAGS) -DUSE_SDL2
    LDFLAGS += $(SDL2_LDFLAGS)
    SRC += src/graphics.c src/draw_parser.c
else
    # Stub out graphics commands
    SRC += src/graphics_stub.c
endif
```

---

## 16. Phase 8 Completion Checklist

| # | Component | Acceptance Criteria |
|---|-----------|-------------------|
| 1 | **SCREEN modes** | Modes 0, 1, 2, 7, 8, 9, 10, 11, 12, 13 initialize with correct resolution, colors, pages. |
| 2 | **SDL2 window** | Window opens at 2× scale. Framebuffer rendered as texture. Events pumped. |
| 3 | **PSET / PRESET** | Plot/clear single pixels. STEP relative coords work. |
| 4 | **POINT** | Read pixel color. Query last position, fg/bg colors. |
| 5 | **LINE** | Lines, boxes (B), filled boxes (BF), line style bitmask all work. |
| 6 | **CIRCLE** | Full circles, arcs, ellipses (aspect), pie slices (negative angles). |
| 7 | **PAINT** | Scanline flood fill with solid color and border detection. Pattern fill supported. |
| 8 | **DRAW** | All direction commands, M, B/N prefixes, C color, S scale, A/TA angle, P paint. |
| 9 | **GET/PUT** | Capture rectangle to array. Paste with PSET/PRESET/AND/OR/XOR. |
| 10 | **WINDOW** | World coordinate mapping (standard and SCREEN variants). PMAP conversion. |
| 11 | **VIEW** | Viewport clipping. Fill and border colors. |
| 12 | **PALETTE** | Modify individual entries. Reset to defaults. PALETTE USING from array. |
| 13 | **PCOPY** | Copy page buffers. Multiple active/visual pages. |
| 14 | **Text rendering** | PRINT in graphics mode uses bitmap font. LOCATE/COLOR work. |
| 15 | **COLOR (graphics)** | Sets foreground/background for graphics modes (different from text COLOR). |
| 16 | **Event loop** | SDL events pumped regularly. Window close handled gracefully. |
| 17 | **Milestone** | House drawing program renders correctly with lines, circles, paint, text. |

---

## 17. Key Implementation Warnings

1. **SDL2 dependency:** Graphics is optional. The build must work without SDL2 by providing `graphics_stub.c` that reports "Feature unavailable" for all graphics commands. Use `#ifdef USE_SDL2` guards.

2. **Framebuffer sync:** Call `graphics_present()` after every drawing operation for immediate visual feedback (matching FB's behavior). For performance, consider dirty-rectangle tracking or presenting only at `SLEEP`/`INKEY$`/end-of-loop.

3. **Palette encoding varies by mode:** Mode 13 uses 256-color VGA (6-bit DAC). Mode 12 uses 16-color EGA. The PALETTE statement interprets the color long differently per mode. CGA modes (1, 2) use fixed palettes with limited PALETTE support.

4. **Coordinate system differences:** In standard WINDOW, Y increases upward (Cartesian). Without WINDOW, Y increases downward (screen space). All coordinate transforms must go through `world_to_phys_*()`.

5. **GET/PUT array format:** FB uses a specific bit-packed format that varies by screen mode. In mode 13 (8bpp), one byte per pixel. In mode 12 (4bpp/planar), the format is plane-interleaved. For simplicity, use 8bpp internally and convert for modes with fewer bits.

6. **Thread safety:** SDL2 must be called from the main thread. Don't create the window from a background thread.

7. **Font data:** The 8×8 CP437 font is 2048 bytes (256 × 8). Embed it as a C array. For 8×16 modes, use a 4096-byte font. Public-domain CP437 font data is widely available.
