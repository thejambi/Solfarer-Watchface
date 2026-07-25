#include "stars.h"

static StarRec *s_tab;
static ResHandle s_res;
static size_t s_res_size;

static const char *CLASS_DESC[] = {
  "blue giant", "blue-white star", "white star", "yellow-white star",
  "yellow dwarf", "orange dwarf", "red dwarf", "white dwarf", "star",
};

bool stars_load(void) {
  s_res = resource_get_handle(RESOURCE_ID_STARS);
  s_res_size = resource_size(s_res);
  uint16_t n = 0;
  resource_load_byte_range(s_res, 0, (uint8_t *)&n, 2);
  if (n != CAT_COUNT) return false;        // resource/header mismatch
  s_tab = malloc(CAT_COUNT * sizeof(StarRec));
  if (!s_tab) return false;
  resource_load_byte_range(s_res, 2, (uint8_t *)s_tab,
                           CAT_COUNT * sizeof(StarRec));
  return true;
}

const StarRec *star_rec(int i) { return &s_tab[i]; }

void star_name(int i, char *buf, size_t cap) {
  if (i < 0) {
    strncpy(buf, "Sol", cap);
    buf[cap - 1] = 0;
    return;
  }
  // clamp the read to the resource end: an over-long request near the last
  // names in the blob fails outright rather than reading short
  uint32_t off = CAT_NAMES_OFF + s_tab[i].name_off;
  size_t want = cap - 1;
  if (off + want > s_res_size) want = off < s_res_size ? s_res_size - off : 0;
  size_t got = want ? resource_load_byte_range(s_res, off, (uint8_t *)buf, want) : 0;
  buf[got] = 0;                            // blob is NUL-terminated within
}

void star_pos01(int i, int *x, int *y, int *z) {
  if (i < 0) { *x = *y = *z = 0; return; }
  *x = s_tab[i].x;
  *y = s_tab[i].y;
  *z = s_tab[i].z;
}

char star_class(int i) {
  if (i < 0) return 'G';
  return CAT_CLASSES[s_tab[i].spect >> 4];
}

int star_subclass(int i) {
  if (i < 0) return 2;                     // Sol, G2
  return s_tab[i].spect & 0x0F;
}

const char *star_class_desc(int i) {
  int c = i < 0 ? 4 : (s_tab[i].spect >> 4);
  return CLASS_DESC[c];
}

const char *star_con3(int i) {
  if (i < 0 || s_tab[i].con >= CAT_N_CONS) return NULL;
  return CAT_CONS[s_tab[i].con];
}

const char *star_con_full(int i) {
  if (i < 0 || s_tab[i].con >= CAT_N_CONS) return NULL;
  return CAT_CONS_FULL[s_tab[i].con];
}

double star_absmag(int i) {
  if (i < 0) return 4.83;
  return s_tab[i].absmag4 / 4.0;
}
