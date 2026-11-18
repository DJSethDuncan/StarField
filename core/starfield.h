/*
 * StarField core engine.
 *
 * A platform-neutral C99 port of The Bridge's starfield-engine.ts. It owns the
 * simulation (perspective stars flying toward the viewer), the analytic motion
 * trails, star colouring, the 60 Hz frame clock and the size/density bounds.
 * Platform front ends (macOS ScreenSaverView, Windows .scr) only translate the
 * draw list this produces into native drawing calls.
 */
#ifndef STARFIELD_H
#define STARFIELD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exposure window of each star's tail, in seconds. */
#define SF_TRAIL_SECONDS 0.045
/* Maximum tail length in logical (CSS-equivalent) pixels at 1x tail length. */
#define SF_MAX_TRAIL_LENGTH 18.0
/* Base depth travelled per second. */
#define SF_SPEED 0.4
/* Opaque background colour, #020405. */
#define SF_BACKGROUND_R 2
#define SF_BACKGROUND_G 4
#define SF_BACKGROUND_B 5

typedef double (*sf_random_fn)(void *ctx);

/* Coordinates are normalised to the viewport; z decreases as stars approach. */
typedef struct {
    double x, y, z;
    double trail_age;
    double color_seed;
} sf_star;

typedef struct {
    double x, y, radius, opacity;
} sf_point;

typedef struct {
    sf_point head;
    double x, y;     /* tail end */
    double opacity;  /* tail opacity */
} sf_trail;

typedef struct {
    int r, g, b; /* 0..255 */
} sf_color;

/* --- Engine primitives (1:1 with starfield-engine.ts) --------------------- */

void sf_create_star(sf_star *star, int initial, sf_random_fn random, void *ctx);
void sf_advance_star(sf_star *star, double seconds, sf_random_fn random,
                     void *ctx, double speed);
sf_point sf_project_star(const sf_star *star, double width, double height);
sf_trail sf_project_trail(const sf_star *star, double width, double height,
                          double speed, double tail_length);
sf_color sf_star_color(double z, int phase_shift, double color_seed,
                       double variation);

/* Logical pixel budget: backing scale (DPR <= 2, <= 8M px) and star count. */
typedef struct {
    double scale;
    int count;
} sf_size;
sf_size sf_starfield_size(double width, double height, double dpr);

/* 60 Hz cadence with fractional phase budget and 1 ms jitter tolerance. */
typedef struct {
    int started;
    double previous, drawn, budget;
} sf_clock;
void sf_clock_reset(sf_clock *clock);
/* Returns 1 and sets *seconds when a frame should be drawn, else 0. */
int sf_clock_tick(sf_clock *clock, double timestamp_ms, double *seconds);

/* --- Settings ------------------------------------------------------------- */

typedef struct {
    double speed;       /* 0.025 .. 2     (default 1) */
    double density;     /* 0.25  .. 10    (default 1) */
    double tail_length; /* 0     .. 3     (default 1) */
    double variation;   /* 0     .. 1     (default 0) */
    int phase_shift;    /* bool           (default 0) */
    int motion;         /* bool           (default 1) — off draws static dots */
} sf_settings;

#define SF_SPEED_MIN 0.025
#define SF_SPEED_MAX 2.0
#define SF_DENSITY_MIN 0.25
#define SF_DENSITY_MAX 10.0
#define SF_TAIL_MIN 0.0
#define SF_TAIL_MAX 3.0
#define SF_VARIATION_MIN 0.0
#define SF_VARIATION_MAX 1.0

void sf_settings_defaults(sf_settings *settings);
/* Clamp to range; non-finite values fall back to 1 (as boundedSetting does). */
double sf_bounded_setting(double value, double min, double max);
/* Clamp every field in place (variation falls back to 0 when invalid). */
void sf_settings_sanitize(sf_settings *settings);

/* --- Field: a whole screen's worth of stars -------------------------------- */

typedef struct {
    sf_color color;
    sf_point head;
    int has_tail;
    double tail_x, tail_y, tail_opacity;
} sf_draw_star;

typedef struct {
    sf_star *stars;
    sf_draw_star *draw;
    size_t count, capacity;
    double width, height; /* logical pixels */
    double dpr;
    uint64_t rng_state;
} sf_field;

/* Max stars: 640 * max density. */
#define SF_MAX_STARS 6400

int sf_field_init(sf_field *field, uint64_t seed);
void sf_field_free(sf_field *field);
/* Resize reseeds every star (as the web version does). */
void sf_field_resize(sf_field *field, double width, double height, double dpr);
/*
 * Advance by `seconds` (0 for a static redraw) and fill field->draw.
 * Returns the number of entries in field->draw to render, back to front in
 * array order. Renderers clear to the background colour, then for each entry
 * fill the tail triangle (if has_tail, see sf_tail_triangle) at tail_opacity,
 * then fill the head circle.
 */
size_t sf_field_step(sf_field *field, const sf_settings *settings,
                     double seconds);

/*
 * Tail as a single triangle: a sharp tip at the tail end and a base through
 * the head centre, perpendicular to the motion, as wide as the head diameter
 * (the head circle covers the base edge). Tail length only moves the tip.
 *
 * Expressed as an affine frame so renderers can reuse one fixed unit triangle
 * with vertices (0,0), (1,-1), (1,1):  P = tip + u * along + v * across.
 * Returns 0 (draw nothing) for a degenerate, zero-length tail.
 */
typedef struct {
    double tip_x, tip_y;       /* unit (0,0): tail end */
    double along_x, along_y;   /* tip -> head centre */
    double across_x, across_y; /* head centre -> one base corner (length = radius) */
} sf_tail_frame;
int sf_tail_triangle(const sf_draw_star *d, sf_tail_frame *frame);

/* splitmix64-based [0,1) generator usable as sf_random_fn with a uint64_t*. */
double sf_random(void *state);

#ifdef __cplusplus
}
#endif

#endif /* STARFIELD_H */
