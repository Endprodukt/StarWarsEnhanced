/* tags.h - what each word of a display list belongs to.
 *
 * The game writes a new display list every frame. To move the picture
 * between two frames (render/smooth.h) the renderer has to know which line
 * of one list is the same thing in the next: the routines that write a
 * list therefore note, next to every word, a tag: a section (what kind of
 * thing) and a key (which one). The tags are no part of the original's
 * memory and change nothing the game computes.
 */
#ifndef SWVP_TAGS_H
#define SWVP_TAGS_H

enum {
    SW_TAG_NONE = 0,
    SW_TAG_INIT,        /* the list's first words */
    SW_TAG_MESS,        /* a message; key: its number */
    SW_TAG_SPMESS,      /* a receding line of the title text; key: its number */
    SW_TAG_BANNER,      /* the STAR WARS logo */
    SW_TAG_SCORE,       /* the score; key: which number (score, last added, wave) */
    SW_TAG_COIN,        /* credits */
    SW_TAG_HISCORE,     /* a row of the high score table; key: row * 4 + part */
    SW_TAG_ALPHABET,    /* the letters for entering initials */
    SW_TAG_STAR,        /* a star; key: its number and how often it was replaced */
    SW_TAG_STARSEC,     /* the way to a sector of stars; key: the sector */
    SW_TAG_SITE,        /* the sight (moved by the interrupt, not by the frame) */
    SW_TAG_PLANE,       /* the X-wing's nose and guns */
    SW_TAG_DSTAR,       /* a Death Star to choose; key: which */
    SW_TAG_COUNT,       /* a countdown */
    SW_TAG_OBJECT,      /* a 3-D object; key: the slot of the thing drawn */
    SW_TAG_CODE,        /* written by the original program (core/emu.h); key: from where */
    SW_TAG_SECTIONS
};

#define SW_TAG(section, key) ((uint16_t)(((section) << 10) | ((key) & 0x3ff)))
#define SW_TAG_SECTION(tag)  ((tag) >> 10)

#endif
