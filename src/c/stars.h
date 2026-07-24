#pragma once
#include <pebble.h>
#include "catalog_gen.h"

// The Local Bubble: 2,235 real systems within 100 ly, packed by
// tools/make_catalog.py. The numeric table lives in heap (~27 KB); names
// stream from the resource on demand — only a few are ever on screen.
// Star index -1 is Sol, handled specially (origin, G2, home).

#define STAR_SOL (-1)
#define STAR_NAME_MAX 24

bool stars_load(void);
const StarRec *star_rec(int i);            // i >= 0
void star_name(int i, char *buf, size_t cap);
void star_pos01(int i, int *x, int *y, int *z);   // 0.01 ly units
char star_class(int i);                    // 'O','B','A','F','G','K','M','D','?'
int star_subclass(int i);                  // 0..9
const char *star_class_desc(int i);        // "red dwarf", "yellow dwarf", ...
const char *star_con3(int i);              // "Cen", NULL if none
double star_absmag(int i);
