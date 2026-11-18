/*
 * Port of The Bridge's starfield.test.ts, starfield-trails.test.ts and
 * starfield-settings.test.ts against the C core.
 */
#include "../core/starfield.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0, checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
        }                                                                    \
    } while (0)
#define CLOSE(a, b, digits) CHECK(fabs((a) - (b)) < 0.5 * pow(10, -(digits)))

static double fixed_value;
static double fixed(void *ctx) { (void)ctx; return fixed_value; }
static double rnd(void *ctx) { return sf_random(ctx); }
static uint64_t seed = 42;

static int color_is(sf_color c, int r, int g, int b) {
    return c.r == r && c.g == g && c.b == b;
}

static sf_star star_at(double x, double y, double z, double age) {
    sf_star s = {x, y, z, age, 0};
    return s;
}

static int star_eq(const sf_star *a, const sf_star *b) {
    return a->x == b->x && a->y == b->y && a->z == b->z &&
           a->trail_age == b->trail_age && a->color_seed == b->color_seed;
}

static void test_colors(void) {
    CHECK(color_is(sf_star_color(1, 1, 0.5, 0), 255, 139, 134));
    CHECK(color_is(sf_star_color(0.8, 1, 0.5, 0), 255, 139, 134));
    CHECK(color_is(sf_star_color(0.65, 1, 0.5, 0), 255, 255, 255));
    CHECK(color_is(sf_star_color(0.5, 1, 0.5, 0), 255, 255, 255));
    CHECK(color_is(sf_star_color(0.25, 1, 0.5, 0), 255, 255, 255));
    CHECK(color_is(sf_star_color(0, 1, 0.5, 0), 145, 205, 255));
    CHECK(color_is(sf_star_color(1, 0, 0.5, 0), 255, 255, 255));
    /* Variation */
    CHECK(color_is(sf_star_color(0.5, 0, 0, 0), 255, 255, 255));
    CHECK(color_is(sf_star_color(0.5, 0, 0, 1), 255, 165, 110));
    CHECK(color_is(sf_star_color(0.5, 0, 1, 1), 195, 220, 255));
    CHECK(color_is(sf_star_color(0.5, 0, 0, 0.5), 255, 210, 183));
    sf_color a = sf_star_color(1, 1, 0, 1), b = sf_star_color(1, 0, 0, 1);
    CHECK(!(a.r == b.r && a.g == b.g && a.b == b.b));
}

static void test_projection(void) {
    sf_star star = star_at(0.1, -0.1, 0.8, 0);
    sf_point before = sf_project_star(&star, 1000, 800);
    sf_advance_star(&star, 1.0 / 30, rnd, &seed, 1);
    sf_point after = sf_project_star(&star, 1000, 800);
    CHECK(after.x > before.x);
    CHECK(after.y < before.y);
    CHECK(after.radius > before.radius);
    CHECK(after.opacity > before.opacity);

    /* Respawn when passing the viewer or leaving the viewport. */
    sf_star cases[2] = {star_at(0, 0, 0.03, 0), star_at(0.4, 0, 0.2, 0)};
    sf_star expect = {0, 0, 1, 0, 0.5};
    fixed_value = 0.5;
    for (int i = 0; i < 2; i++) {
        sf_advance_star(&cases[i], 1.0 / 30, fixed, NULL, 1);
        CHECK(star_eq(&cases[i], &expect));
    }

    /* Long gaps are clamped. */
    sf_star p = star_at(0.1, 0.1, 0.8, 0), q = p;
    sf_advance_star(&p, 60, rnd, &seed, 1);
    sf_advance_star(&q, 0.05, rnd, &seed, 1);
    CHECK(star_eq(&p, &q));
    sf_advance_star(&p, -1, rnd, &seed, 1);
    CHECK(star_eq(&p, &q));

    /* Seeded stars project inside the viewport. */
    double values[] = {0, 0.25, 0.5, 0.75, 0.999};
    for (int i = 0; i < 5; i++) {
        fixed_value = values[i];
        sf_star s;
        sf_create_star(&s, 1, fixed, NULL);
        sf_point pt = sf_project_star(&s, 1200, 900);
        CHECK(pt.x >= 0 && pt.x <= 1200);
        CHECK(pt.y >= 0 && pt.y <= 900);
        CHECK(isfinite(pt.radius));
    }
}

static double tail_distance(const sf_star *s, double length) {
    sf_trail t = sf_project_trail(s, 1200, 900, 1, length);
    return hypot(t.head.x - t.x, t.head.y - t.y);
}

static int trail_eq(sf_trail a, sf_trail b) {
    return a.x == b.x && a.y == b.y && a.opacity == b.opacity &&
           a.head.x == b.head.x && a.head.y == b.head.y &&
           a.head.radius == b.head.radius && a.head.opacity == b.head.opacity;
}

static void test_tail_length(void) {
    sf_star s = star_at(0.1, 0.1, 0.3, 0.045);
    CHECK(tail_distance(&s, 0) == 0);
    CHECK(tail_distance(&s, 3) > tail_distance(&s, 1));
    CHECK(tail_distance(&s, 3) <= SF_MAX_TRAIL_LENGTH * 3 + 1e-9);
    sf_trail t3 = sf_project_trail(&s, 1200, 900, 1, 3);
    sf_trail t1 = sf_project_trail(&s, 1200, 900, 1, 1);
    CHECK(t3.head.x == t1.head.x && t3.head.y == t1.head.y);
    CHECK(trail_eq(sf_project_trail(&s, 1200, 900, 1, -1),
                   sf_project_trail(&s, 1200, 900, 1, 0)));
    CHECK(trail_eq(sf_project_trail(&s, 1200, 900, 1, 99), t3));
    CHECK(trail_eq(sf_project_trail(&s, 1200, 900, 1, NAN), t1));
}

static void test_trails(void) {
    double signs[] = {-0.1, 0.1};
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            double x = signs[i], y = signs[j];
            sf_star s = star_at(x, y, 0.5, 0);
            sf_advance_star(&s, 0.05, rnd, &seed, 1);
            sf_trail t = sf_project_trail(&s, 1200, 800, 1, 1);
            double dx = t.head.x - t.x, dy = t.head.y - t.y;
            CHECK(dx * x > 0);
            CHECK(dy * y > 0);
            CLOSE(dx * (t.head.y - 400), dy * (t.head.x - 600), 2);
            CHECK(t.opacity < t.head.opacity);
        }

    /* Same exposure and travel regardless of frame interval. */
    int rates[] = {20, 30, 60, 120, 144};
    sf_trail first = {0};
    for (int r = 0; r < 5; r++) {
        sf_star s = star_at(0.08, -0.06, 0.8, 0);
        for (int i = 0; i < rates[r]; i++)
            sf_advance_star(&s, 1.0 / rates[r], rnd, &seed, 1);
        sf_trail t = sf_project_trail(&s, 1440, 900, 1, 1);
        if (r == 0) first = t;
        CLOSE(t.head.x, first.head.x, 8);
        CLOSE(t.x, first.x, 8);
        CLOSE(t.y, first.y, 8);
    }

    /* Length grows with proximity but is capped. */
    sf_star near = star_at(0.01, 0.01, 0.1, SF_TRAIL_SECONDS);
    sf_star far = star_at(0.01, 0.01, 0.8, SF_TRAIL_SECONDS);
    sf_star close = star_at(0.01, 0.01, 0.04, SF_TRAIL_SECONDS);
    sf_trail tn = sf_project_trail(&near, 1200, 800, 1, 1);
    sf_trail tf = sf_project_trail(&far, 1200, 800, 1, 1);
    sf_trail tc = sf_project_trail(&close, 100000, 800, 1, 1);
    CHECK(hypot(tn.head.x - tn.x, tn.head.y - tn.y) >
          hypot(tf.head.x - tf.x, tf.head.y - tf.y));
    CHECK(tn.opacity > tf.opacity);
    CLOSE(hypot(tc.head.x - tc.x, tc.head.y - tc.y), SF_MAX_TRAIL_LENGTH, 2);

    /* Exposure reset on respawn; stalls and invalid deltas clamp. */
    sf_star s = star_at(0.1, 0.1, 0.8, 0);
    sf_advance_star(&s, 60, rnd, &seed, 1);
    CLOSE(s.z, 0.78, 2);
    CHECK(s.trail_age == SF_TRAIL_SECONDS);
    double bad[] = {-1, NAN, INFINITY};
    for (int i = 0; i < 3; i++) {
        sf_star before = s;
        sf_advance_star(&s, bad[i], rnd, &seed, 1);
        CHECK(star_eq(&s, &before));
    }
    s.z = 0.03;
    fixed_value = 0.7;
    sf_advance_star(&s, 0.05, fixed, NULL, 1);
    sf_star fresh;
    sf_create_star(&fresh, 1, fixed, NULL);
    sf_star *pair[2] = {&s, &fresh};
    for (int i = 0; i < 2; i++) {
        sf_trail t = sf_project_trail(pair[i], 2000, 1200, 1, 1);
        CHECK(t.x == t.head.x && t.y == t.head.y);
        CHECK(pair[i]->trail_age == 0);
    }
}

static void test_clock(void) {
    double rates[] = {59.94, 60, 120, 144, 240};
    for (int r = 0; r < 5; r++) {
        double hz = rates[r];
        sf_clock clock;
        sf_clock_reset(&clock);
        sf_star s = star_at(0, 0, 0.9, 0);
        int draws = 0;
        for (int i = 0; i <= (int)floor(hz); i++) {
            double seconds;
            if (sf_clock_tick(&clock, i * 1000.0 / hz, &seconds)) {
                draws++;
                sf_advance_star(&s, seconds, rnd, &seed, 1);
            }
        }
        CHECK(draws >= 59 && draws <= 62);
        CLOSE(s.z, 0.5, 1);
    }

    sf_clock clock;
    sf_clock_reset(&clock);
    int draws = 0;
    double seconds;
    for (int i = 0; i <= 120; i++) {
        double ts = i * 1000.0 / 60 + (i % 2 ? -0.4 : 0.4);
        if (sf_clock_tick(&clock, ts, &seconds)) draws++;
    }
    CHECK(draws >= 119 && draws <= 121);

    sf_clock_reset(&clock);
    sf_clock_tick(&clock, 0, &seconds);
    CHECK(sf_clock_tick(&clock, 10000, &seconds) && seconds == 10);
    CHECK(!sf_clock_tick(&clock, 10001, &seconds));
    sf_clock_reset(&clock);
    CHECK(sf_clock_tick(&clock, 50000, &seconds) && seconds == 0);
}

static void test_size(void) {
    double cases[3][3] = {{390, 844, 3}, {1440, 900, 2}, {16000, 10000, 4}};
    for (int i = 0; i < 3; i++) {
        sf_size size = sf_starfield_size(cases[i][0], cases[i][1], cases[i][2]);
        CHECK(size.count >= 100 && size.count <= 640);
        CHECK(size.scale <= 2);
        CHECK(cases[i][0] * cases[i][1] * size.scale * size.scale <= 8000001);
    }
}

static void test_settings(void) {
    CHECK(sf_bounded_setting(NAN, 0.25, 2) == 1);
    CHECK(sf_bounded_setting(-1, 0.25, 2) == 0.25);
    CHECK(sf_bounded_setting(100, 0.25, 1) == 1);

    sf_star slow = star_at(0.01, 0.01, 0.8, 0), fast = slow;
    fixed_value = 0.5;
    sf_advance_star(&slow, 0.02, fixed, NULL, 0.5);
    sf_advance_star(&fast, 0.02, fixed, NULL, 2);
    CLOSE(0.8 - fast.z, 4 * (0.8 - slow.z), 2);
    sf_point before = sf_project_star(&fast, 100, 100);
    sf_advance_star(&fast, 0, fixed, NULL, 0.25);
    sf_point after = sf_project_star(&fast, 100, 100);
    CHECK(before.x == after.x && before.y == after.y &&
          before.radius == after.radius && before.opacity == after.opacity);

    sf_settings s = {NAN, 99, -5, NAN, 7, 3};
    sf_settings_sanitize(&s);
    CHECK(s.speed == 1 && s.density == 10 && s.tail_length == 0);
    CHECK(s.variation == 0 && s.phase_shift == 1 && s.motion == 1);
}

static void test_field(void) {
    sf_field field;
    CHECK(sf_field_init(&field, 7));
    sf_settings settings;
    sf_settings_defaults(&settings);
    sf_field_resize(&field, 1440, 900, 2);
    CHECK(field.count == 309); /* round(1440*900/4200) */
    size_t n = sf_field_step(&field, &settings, 0);
    CHECK(n == 309);
    for (size_t i = 0; i < n; i++) CHECK(!field.draw[i].has_tail);

    int tails = 0;
    for (int frame = 0; frame < 120; frame++)
        n = sf_field_step(&field, &settings, 1.0 / 60);
    for (size_t i = 0; i < n; i++) tails += field.draw[i].has_tail;
    CHECK(tails > 0);

    settings.density = 10;
    CHECK(sf_field_step(&field, &settings, 1.0 / 60) == 3090);
    settings.density = 0.25;
    CHECK(sf_field_step(&field, &settings, 1.0 / 60) == 77);

    settings.motion = 0;
    n = sf_field_step(&field, &settings, 1.0 / 60);
    for (size_t i = 0; i < n; i++) CHECK(!field.draw[i].has_tail);

    /* Tail triangle: tip at the tail end, base through the head centre,
       perpendicular to the motion and as wide as the head diameter. */
    sf_draw_star d = {{255, 255, 255}, {30, 40, 2, 1}, 1, 0, 0, 0.32};
    sf_tail_frame tf;
    CHECK(sf_tail_triangle(&d, &tf));
    CHECK(tf.tip_x == 0 && tf.tip_y == 0);
    CHECK(tf.tip_x + tf.along_x == 30 && tf.tip_y + tf.along_y == 40);
    CLOSE(hypot(tf.across_x, tf.across_y), 2, 9);                   /* half-width = radius */
    CLOSE(tf.across_x * tf.along_x + tf.across_y * tf.along_y, 0, 9); /* perpendicular */
    /* Longer tails only move the tip; the base stays the same. */
    sf_draw_star longer = d;
    longer.tail_x = -30;
    longer.tail_y = -40;
    sf_tail_frame lf;
    CHECK(sf_tail_triangle(&longer, &lf));
    CLOSE(lf.tip_x + lf.along_x, 30, 9);
    CLOSE(lf.across_x, tf.across_x, 9);
    CLOSE(lf.across_y, tf.across_y, 9);
    /* Zero-length tail draws nothing. */
    sf_draw_star none = d;
    none.tail_x = 30;
    none.tail_y = 40;
    CHECK(!sf_tail_triangle(&none, &tf));
    sf_field_free(&field);
}

int main(void) {
    test_colors();
    test_projection();
    test_tail_length();
    test_trails();
    test_clock();
    test_size();
    test_settings();
    test_field();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
