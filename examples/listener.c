/* Prints messages and exits after COUNT of them (exit 1 after 30 s without enough).
   usage: rosc_listener TOPIC any|string|twist|scan [COUNT] [remaps...] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rosc/msgs.h"
#include "rosc/rosc.h"

static int g_received = 0;

static void on_msg(const void* data, size_t size, const rosc_msg_info_t* info, void* user) {
  const char* kind = (const char*)user;
  g_received++;
  if (strcmp(kind, "twist") == 0) {
    rosc_twist_t t;
    if (!rosc_twist_parse(data, size, &t)) return (void)printf("bad twist\n");
    printf("twist: %g %g %g %g %g %g\n", t.linear[0], t.linear[1], t.linear[2], t.angular[0], t.angular[1],
           t.angular[2]);
  } else if (strcmp(kind, "scan") == 0) {
    rosc_laser_scan_t s;
    if (!rosc_laser_scan_parse(data, size, &s)) return (void)printf("bad scan\n");
    printf("scan: frame=%.*s n=%zu r0=%g angle_min=%g\n", (int)s.frame_id_len, s.frame_id, s.ranges_len,
           s.ranges_len ? rosc_float_at(s.ranges, 0) : 0.0f, s.angle_min);
  } else if (strcmp(kind, "string") == 0) {
    const char* str;
    size_t len;
    if (!rosc_string_parse(data, size, &str, &len)) return (void)printf("bad string\n");
    printf("string: %.*s\n", (int)len, str);
  } else {
    printf("any: type=%s md5=%s size=%zu latching=%d from=%s\n", info->type, info->md5sum, size, info->latching,
           info->callerid);
  }
  fflush(stdout);
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s TOPIC any|string|twist|scan [COUNT]\n", argv[0]);
    return 2;
  }
  const char* kind = argv[2];
  int count = argc > 3 ? atoi(argv[3]) : 1;
  const char *type = "*", *md5 = "*";
  if (strcmp(kind, "string") == 0) type = ROSC_STD_MSGS_STRING_TYPE, md5 = ROSC_STD_MSGS_STRING_MD5;
  if (strcmp(kind, "twist") == 0) type = ROSC_GEOMETRY_MSGS_TWIST_TYPE, md5 = ROSC_GEOMETRY_MSGS_TWIST_MD5;
  if (strcmp(kind, "scan") == 0) type = ROSC_SENSOR_MSGS_LASER_SCAN_TYPE, md5 = ROSC_SENSOR_MSGS_LASER_SCAN_MD5;

  rosc_node_options_t opts = {0};
  opts.args = (const char* const*)argv;
  opts.num_args = argc;
  rosc_node_t* node;
  rosc_sub_t* sub;
  if (rosc_node_create("rosc_listener", &opts, &node) != ROSC_OK) {
    fprintf(stderr, "node: %s\n", rosc_last_error());
    return 1;
  }
  if (rosc_subscribe(node, argv[1], type, md5, 10, on_msg, (void*)kind, &sub) != ROSC_OK) {
    fprintf(stderr, "subscribe: %s\n", rosc_last_error());
    rosc_node_destroy(node);
    return 1;
  }
  time_t deadline = time(NULL) + 30;
  while (g_received < count && time(NULL) < deadline && rosc_spin_once(node, 100) == ROSC_OK) {
  }
  rosc_node_destroy(node);
  return g_received >= count ? 0 : 1;
}
