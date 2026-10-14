/* dogfight.h - the dogfight: a mode of its own (enhanced; not in the
 * arcade machine), chosen as a fourth Death Star on the page where one is
 * chosen. The player flies freely in space, in a box he cannot leave (at
 * its walls the ship turns back by itself: "WHERE ARE YOU GOING"), the
 * Death Star far off; wave after wave of enemies comes, for as long as he
 * lives (docs/STATUS.md, "Dogfight").
 *
 * Controlled as Time Pilot 3D: the yoke moves the sight a little way out
 * of the middle at once, and turns and pitches the ship the more, the
 * farther out the sight is (gently near the middle, fully at its reach):
 * small aims need no turn of the whole ship; the thumb buttons
 * make it faster and slower, the throttle going back to the middle when
 * they are let go; both together and the yoke to a side roll it about its
 * own axis. The right trigger fires the lasers.
 *
 * The enemies, each brought down by so many laser hits: TIE fighters (1),
 * TIE interceptors (2; a model of its own, the ROM has none), Darth
 * Vader's TIE (3, every fifth wave). They attack and break away, and fire
 * fireballs, which can be shot down; each that hits takes a shield (6, as
 * in the arcade game). After each wave R2 mends one. Far off, now and then,
 * other X-wings are chased by TIE fighters (only to be seen).
 *
 * Its picture is made as the game makes its own: a display list for the
 * vector generator, calling the game's own routines in the vector ROM
 * (its lettering, the X-wing's nose and guns, the sight, the stars, the
 * fireballs, the Death Star), run by render/avg into lines; the ships are
 * the ROM's models (TIE fighter, Vader's TIE, the X-wing, the pieces a TIE
 * breaks into) drawn in true perspective, their lines marked as the game's
 * are, so that render/solid makes them solid and the views of
 * render/enhance.h (cockpit, outside) work on it. Its sounds are the
 * board's own, asked for as the game asks (sw_dogfight_sound).
 */
#ifndef SWE_DOGFIGHT_H
#define SWE_DOGFIGHT_H

#include "avg/avg.h"
#include "core/machine.h"
#include "render/solid.h"

/* What the player does, once a frame. */
typedef struct sw_dogfight_input {
    float yaw, pitch;           /* the yoke, -1..1: to the right, up (as the sight goes) */
    int   faster, slower;       /* the thumb buttons held */
    int   fire, torpedo;        /* the triggers held */
} sw_dogfight_input;

#define SW_DF_ENEMIES 12
#define SW_DF_SHOTS   32
#define SW_DF_PIECES  48
#define SW_DF_MODELS  6         /* TIE fighter, its three pieces, Vader's TIE, the X-wing */
#define SW_DF_FACES   96

/* A thing in space: where it is, and its axes ahead, to the right, up. */
typedef struct sw_df_body { double pos[3], f[3], r[3], u[3]; } sw_df_body;

typedef struct sw_df_enemy {
    int        kind;            /* 0 none, 1 TIE fighter, 2 interceptor, 3 Vader */
    int        hp, mode;        /* mode 0 attacking, 1 breaking away */
    sw_df_body b;
    double     speed, turn;     /* units a second, radians a second */
    double     mode_t, fire_t, hit_t;
    double     away[3];
    int        passed;          /* (its pass-by sound has been heard) */
} sw_df_enemy;

typedef struct sw_df_shot { int live; double pos[3], vel[3], t; } sw_df_shot;

typedef struct sw_df_piece {
    int        live, model;
    sw_df_body b;
    double     vel[3], spin[3], t;
} sw_df_piece;

typedef struct sw_dogfight {
    int    active;
    int    finished;            /* the game is over and shown: the program goes back to the attract mode */
    double t;                   /* seconds since it began */
    /* the ship: where it is, and its own axes ahead (f), to the right (r)
     * and up (u), in the box's coordinates */
    double pos[3], f[3], r[3], u[3];
    double speed;               /* units a second */
    float  throttle;            /* -1 (slowest) .. 1 (fastest); rests at 0 */
    float  yaw_rate, pitch_rate, roll_rate;     /* radians a second, as they are now (they ease) */
    float  sight_x, sight_y;    /* the sight, from the middle of the view (the lines' units): it goes
                                 * with the yoke, and the ship turns the more, the farther out it is */
    int    turning_back;        /* at a wall: the ship turns back by itself */
    double back_since;
    /* the game */
    int    over;                /* the shields are gone */
    double over_t;
    int    shields, wave;
    long   score;
    int    pending[4];          /* of each kind, still to come in this wave */
    double wave_t;              /* when the wave began (its name is shown a while) */
    double hit_t, mend_t;       /* when a shield was lost, mended */
    double laser_t, fire_next;  /* the last shot, when the trigger held fires again */
    int    laser_side;
    sw_df_enemy enemy[SW_DF_ENEMIES];
    sw_df_shot  shot[SW_DF_SHOTS];
    sw_df_piece piece[SW_DF_PIECES];
    double chase_t;             /* the chase far off began */
    uint32_t rng;
    uint8_t  sound[32];         /* requests to the sound board, waiting */
    int      sound_r, sound_w;
    /* the ROM's models */
    int16_t  mp[SW_DF_MODELS][64][3];
    uint8_t  me[SW_DF_MODELS][160][2];
    int      mnp[SW_DF_MODELS], mne[SW_DF_MODELS];
    /* the display list and its lines */
    uint8_t  space[SW_AVG_SPACE_SIZE];
    uint16_t tags[SW_AVG_SPACE_SIZE / 2];
    uint16_t at;                /* the next word of the list */
    uint16_t tag;
    sw_avg_list list;
    uint16_t inst;
    uint32_t line_id;
    sw_solid_extra faces[SW_DF_FACES];          /* of what has no faces in the ROM (the interceptor) */
    int      n_faces;
    float    dust[48][3];       /* specks near the ship, to see it moving */
} sw_dogfight;

/* Begins the dogfight (the models and the vector ROM are the machine's). */
void sw_dogfight_start(sw_dogfight *d, const sw_machine *m);

/* A step of dt seconds. */
void sw_dogfight_step(sw_dogfight *d, const sw_dogfight_input *in, double dt);

/* The next request for the sound board, or -1. */
int sw_dogfight_sound(sw_dogfight *d);

/* The picture as it is: lines as render/avg gives them (tagged, see
 * core/tags.h). Valid until the next call. The faces of what the ROM has
 * no faces for are in d->faces (for render/solid). */
int sw_dogfight_lines(sw_dogfight *d, const sw_machine *m, const sw_line **out);

/* The fourth choice on the page where a Death Star is chosen: its picture
 * (appended to `list`), and whether the sight is on it. */
void sw_dogfight_choice_lines(sw_dogfight *d, const sw_machine *m, sw_avg_list *list);
int  sw_dogfight_chosen(const sw_machine *m);

/* How the ship banks and pitches as the player sees it from outside
 * (-1..1, as the sight would be): for render/enhance.h. */
void sw_dogfight_attitude(const sw_dogfight *d, float *bank, float *pitch);

#endif
