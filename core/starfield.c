/* StarField core engine — see starfield.h. */
#include "starfield.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* JavaScript's Math.round for the non-negative values used here. */
static double js_round(double v) { return floor(v + 0.5); }

static double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static int is_finite(double v) { return isfinite(v); }

double sf_random(void *state) {
    uint64_t *s = (uint64_t *)state;
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z ^= z >> 31;
    return (double)(z >> 11) * (1.0 / 9007199254740992.0);
}

void sf_create_star(sf_star *star, int initial, sf_random_fn random, void *ctx) {
    double z = initial ? 0.08 + random(ctx) * 0.92 : 1.0;
    star->x = (random(ctx) - 0.5) * z;
    star->y = (random(ctx) - 0.5) * z;
    star->z = z;
    star->trail_age = 0;
    star->color_seed = random(ctx);
}

void sf_advance_star(sf_star *star, double seconds, sf_random_fn random,
                     void *ctx, double speed) {
    /* Clamp elapsed time so stalls never cause a jump. */
    double elapsed = is_finite(seconds) ? clampd(seconds, 0, 0.05) : 0;
    star->z -= elapsed * SF_SPEED * speed;
    star->trail_age = fmin(SF_TRAIL_SECONDS, star->trail_age + elapsed);
    if (star->z <= 0.035 || fabs(star->x) > 0.51 * star->z ||
        fabs(star->y) > 0.51 * star->z) {
        sf_create_star(star, 0, random, ctx);
    }
}

sf_point sf_project_star(const sf_star *star, double width, double height) {
    double proximity = 1 - star->z;
    sf_point p;
    p.x = width * (0.5 + star->x / star->z);
    p.y = height * (0.5 + star->y / star->z);
    p.radius = 0.35 + proximity * proximity * 1.65;
    p.opacity = 0.3 + proximity * 0.7;
    return p;
}

/*
 * Red at the far edge, white through most of the journey, blue close by.
 * Approximate visible stellar colours, not a population simulation.
 */
sf_color sf_star_color(double z, int phase_shift, double color_seed,
                       double variation) {
    static const double palette[5][3] = {
        {255, 165, 110}, {255, 211, 165}, {255, 241, 210},
        {255, 255, 255}, {195, 220, 255}};
    double amount = clampd(is_finite(variation) ? variation : 0, 0, 1);
    sf_color white = {255, 255, 255};
    if (!phase_shift && amount == 0) return white;

    double seed = is_finite(color_seed) ? color_seed : 0.5;
    double position = clampd(seed, 0, 1) * 4;
    int index = (int)floor(position);
    if (index > 3) index = 3;
    double fraction = position - index;
    double base[3];
    for (int c = 0; c < 3; c++) {
        double v = palette[index][c];
        base[c] = 255 + ((v + (palette[index + 1][c] - v) * fraction) - 255) * amount;
    }
    double depth = clampd(z, 0, 1);
    double far = phase_shift ? fmin(1, fmax(0, (depth - 0.65) / 0.15)) : 0;
    double near = phase_shift ? fmin(1, fmax(0, (0.25 - depth) / 0.25)) : 0;
    sf_color out;
    out.r = (int)js_round(base[0] * (255 - 110 * near) / 255);
    out.g = (int)js_round(base[1] * (255 - 116 * far - 50 * near) / 255);
    out.b = (int)js_round(base[2] * (255 - 121 * far) / 255);
    return out;
}

/* Analytic shutter exposure, not a history buffer: bounded in time and pixels. */
sf_trail sf_project_trail(const sf_star *star, double width, double height,
                          double speed, double tail_length) {
    double length = is_finite(tail_length) ? clampd(tail_length, 0, 3) : 1;
    sf_trail t;
    t.head = sf_project_star(star, width, height);
    double age = clampd(star->trail_age, 0, SF_TRAIL_SECONDS);
    double old_z = star->z + SF_SPEED * age * speed * length;
    double dx = t.head.x - width * (0.5 + star->x / old_z);
    double dy = t.head.y - height * (0.5 + star->y / old_z);
    double dist = hypot(dx, dy);
    double scale = fmin(1, (SF_MAX_TRAIL_LENGTH * length) / (dist != 0 ? dist : 1));
    t.x = t.head.x - dx * scale;
    t.y = t.head.y - dy * scale;
    t.opacity = t.head.opacity * 0.32;
    return t;
}

void sf_clock_reset(sf_clock *clock) { memset(clock, 0, sizeof *clock); }

/* Keep fractional phase; comparing rounded frame deltas can halve 60 Hz output. */
int sf_clock_tick(sf_clock *clock, double timestamp, double *seconds) {
    if (!clock->started) {
        clock->started = 1;
        clock->previous = clock->drawn = timestamp;
        clock->budget = 0;
        *seconds = 0;
        return 1;
    }
    clock->budget += fmax(0, timestamp - clock->previous);
    clock->previous = timestamp;
    const double interval = 1000.0 / 60.0;
    /* Sub-millisecond jitter must not skip every other refresh. */
    if (clock->budget + 1 < interval) return 0;
    clock->budget -= fmax(1, floor((clock->budget + 1) / interval)) * interval;
    *seconds = (timestamp - clock->drawn) / 1000.0;
    clock->drawn = timestamp;
    return 1;
}

sf_size sf_starfield_size(double width, double height, double dpr) {
    sf_size s;
    double d = (dpr > 0 && is_finite(dpr)) ? dpr : 1;
    s.scale = fmin(fmin(d, 2), sqrt(8000000.0 / fmax(1, width * height)));
    double count = js_round((width * height) / 4200);
    s.count = (int)fmax(100, fmin(640, count));
    return s;
}

void sf_settings_defaults(sf_settings *s) {
    s->speed = 1;
    s->density = 1;
    s->tail_length = 1;
    s->variation = 0;
    s->phase_shift = 0;
    s->motion = 1;
}

double sf_bounded_setting(double value, double min, double max) {
    return is_finite(value) ? clampd(value, min, max) : 1;
}

void sf_settings_sanitize(sf_settings *s) {
    s->speed = sf_bounded_setting(s->speed, SF_SPEED_MIN, SF_SPEED_MAX);
    s->density = sf_bounded_setting(s->density, SF_DENSITY_MIN, SF_DENSITY_MAX);
    s->tail_length = sf_bounded_setting(s->tail_length, SF_TAIL_MIN, SF_TAIL_MAX);
    s->variation = is_finite(s->variation)
                       ? clampd(s->variation, SF_VARIATION_MIN, SF_VARIATION_MAX)
                       : 0;
    s->phase_shift = s->phase_shift ? 1 : 0;
    s->motion = s->motion ? 1 : 0;
}

int sf_field_init(sf_field *field, uint64_t seed) {
    memset(field, 0, sizeof *field);
    field->capacity = SF_MAX_STARS;
    field->stars = (sf_star *)calloc(field->capacity, sizeof(sf_star));
    field->draw = (sf_draw_star *)calloc(field->capacity, sizeof(sf_draw_star));
    field->rng_state = seed;
    field->dpr = 1;
    if (!field->stars || !field->draw) {
        sf_field_free(field);
        return 0;
    }
    return 1;
}

void sf_field_free(sf_field *field) {
    free(field->stars);
    free(field->draw);
    field->stars = NULL;
    field->draw = NULL;
    field->count = field->capacity = 0;
}

void sf_field_resize(sf_field *field, double width, double height, double dpr) {
    field->width = fmax(1, width);
    field->height = fmax(1, height);
    field->dpr = dpr;
    sf_size size = sf_starfield_size(field->width, field->height, dpr);
    field->count = (size_t)size.count;
    for (size_t i = 0; i < field->count; i++)
        sf_create_star(&field->stars[i], 1, sf_random, &field->rng_state);
}

size_t sf_field_step(sf_field *field, const sf_settings *settings,
                     double seconds) {
    const double w = field->width, h = field->height;
    double want = js_round(
        sf_starfield_size(w, h, field->dpr).count * settings->density);
    size_t count = (size_t)fmax(1, fmin((double)field->capacity, want));
    while (field->count < count)
        sf_create_star(&field->stars[field->count++], 1, sf_random,
                       &field->rng_state);
    field->count = count;

    for (size_t i = 0; i < count; i++) {
        sf_star *star = &field->stars[i];
        if (seconds)
            sf_advance_star(star, seconds, sf_random, &field->rng_state,
                            settings->speed);
        sf_trail trail =
            sf_project_trail(star, w, h, settings->speed, settings->tail_length);
        sf_draw_star *d = &field->draw[i];
        d->head = trail.head;
        /* Keep distant stars their size, but brighten the tint enough to see. */
        if (settings->phase_shift && star->z > 0.65) {
            double far = fmin(1, (star->z - 0.65) / 0.15);
            d->head.opacity = fmax(d->head.opacity, 0.5 + far * 0.3);
        }
        d->color = sf_star_color(star->z, settings->phase_shift,
                                 star->color_seed, settings->variation);
        d->has_tail = settings->motion && seconds && star->trail_age > 0 &&
                      settings->tail_length > 0;
        d->tail_x = trail.x;
        d->tail_y = trail.y;
        d->tail_opacity = trail.opacity;
    }
    return count;
}

void sf_tail_segment(const sf_draw_star *d, int i, double *x0, double *y0,
                     double *x1, double *y1, double *line_width) {
    double start = (double)i / SF_TAIL_SEGMENTS;
    double end = (double)(i + 1) / SF_TAIL_SEGMENTS;
    double dx = d->head.x - d->tail_x, dy = d->head.y - d->tail_y;
    *x0 = d->tail_x + dx * start;
    *y0 = d->tail_y + dy * start;
    *x1 = d->tail_x + dx * end;
    *y1 = d->tail_y + dy * end;
    *line_width = d->head.radius * 2 * end;
}
