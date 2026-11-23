/* Publishes std_msgs/String, geometry_msgs/Twist or sensor_msgs/LaserScan at 10 Hz.
   usage: rosc_talker TOPIC string|twist|scan [COUNT (0: forever)] [latch] [remaps...] */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rosc/msgs.h"
#include "rosc/rosc.h"

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int sig) {
  (void)sig;
  g_stop = 1;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s TOPIC string|twist|scan [COUNT] [latch]\n", argv[0]);
    return 2;
  }
  const char* kind = argv[2];
  int count = argc > 3 ? atoi(argv[3]) : 0;
  bool latch = argc > 4 && strcmp(argv[4], "latch") == 0;
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  rosc_node_options_t opts = {0};
  opts.args = (const char* const*)argv;
  opts.num_args = argc;
  rosc_node_t* node;
  if (rosc_node_create("rosc_talker", &opts, &node) != ROSC_OK) {
    fprintf(stderr, "node: %s\n", rosc_last_error());
    return 1;
  }
  const rosc_msg_type_t* type = strcmp(kind, "twist") == 0  ? &rosc_geometry_msgs_Twist
                                : strcmp(kind, "scan") == 0 ? &rosc_sensor_msgs_LaserScan
                                                            : &rosc_std_msgs_String;
  rosc_pub_t* pub;
  if (rosc_advertise(node, argv[1], type, 10, latch, &pub) != ROSC_OK) {
    fprintf(stderr, "advertise: %s\n", rosc_last_error());
    rosc_node_destroy(node);
    return 1;
  }

  unsigned char buf[4096];
  for (int i = 0; (count <= 0 || i < count) && !g_stop && rosc_ok(node); i++) {
    size_t n;
    if (strcmp(kind, "twist") == 0) {
      rosc_geometry_msgs_Twist_t t = {{0.5, 0, 0}, {0, 0, 0.25 * i}};
      n = rosc_serialize_geometry_msgs_Twist(&t, buf, sizeof(buf));
    } else if (strcmp(kind, "scan") == 0) {
      float ranges[5] = {1.0f, 1.5f, 2.0f, 2.5f, (float)i};
      rosc_sensor_msgs_LaserScan_t s = {0};
      s.seq = (uint32_t)i;
      s.frame_id = "laser";
      s.frame_id_len = 5;
      s.angle_min = -1.0f, s.angle_max = 1.0f, s.angle_increment = 0.5f, s.range_min = 0.1f, s.range_max = 10.0f;
      s.ranges = ranges;
      s.ranges_len = 5;
      n = rosc_serialize_sensor_msgs_LaserScan(&s, buf, sizeof(buf));
    } else {
      char text[64];
      int len = snprintf(text, sizeof(text), "hello from rosc %d", i);
      n = rosc_serialize_std_msgs_String(text, (size_t)len, buf, sizeof(buf));
    }
    rosc_publish(pub, buf, n);
    rosc_spin_once(node, 100);
  }
  while (latch && !g_stop && rosc_ok(node)) rosc_spin_once(node, 100);  // keep serving the latched message
  rosc_node_destroy(node);
  return 0;
}
