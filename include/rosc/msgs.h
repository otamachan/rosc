/* Header-only serialization helpers for a few common ROS1 message types. */
#ifndef ROSC_MSGS_H
#define ROSC_MSGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "rosc/msgs.h assumes a little-endian host"
#endif

#define ROSC_STD_MSGS_STRING_TYPE "std_msgs/String"
#define ROSC_STD_MSGS_STRING_MD5 "992ce8a1687cec8c8bd883ec73ca41d1"
#define ROSC_GEOMETRY_MSGS_TWIST_TYPE "geometry_msgs/Twist"
#define ROSC_GEOMETRY_MSGS_TWIST_MD5 "9f195f881246fdfa2798d1d3eebca84a"
#define ROSC_SENSOR_MSGS_LASER_SCAN_TYPE "sensor_msgs/LaserScan"
#define ROSC_SENSOR_MSGS_LASER_SCAN_MD5 "90c7ef2dc6895d81024acba2ac42f369"

#define ROSC_STD_MSGS_STRING_DEF \
  "string data\n"

#define ROSC_GEOMETRY_MSGS_TWIST_DEF \
  "# This expresses velocity in free space broken into its linear and angular parts.\n" \
  "Vector3 linear\n" \
  "Vector3 angular\n" \
  "\n" \
  "================================================================================\n" \
  "MSG: geometry_msgs/Vector3\n" \
  "# This represents a vector in free space. \n" \
  "# It is only meant to represent a direction. Therefore, it does not\n" \
  "# make sense to apply a translation to it (e.g., when applying a \n" \
  "# generic rigid transformation to a Vector3, tf2 will only apply the\n" \
  "# rotation). If you want your data to be translatable too, use the\n" \
  "# geometry_msgs/Point message instead.\n" \
  "\n" \
  "float64 x\n" \
  "float64 y\n" \
  "float64 z"

#define ROSC_SENSOR_MSGS_LASER_SCAN_DEF \
  "# Single scan from a planar laser range-finder\n" \
  "#\n" \
  "# If you have another ranging device with different behavior (e.g. a sonar\n" \
  "# array), please find or create a different message, since applications\n" \
  "# will make fairly laser-specific assumptions about this data\n" \
  "\n" \
  "Header header            # timestamp in the header is the acquisition time of \n" \
  "                         # the first ray in the scan.\n" \
  "                         #\n" \
  "                         # in frame frame_id, angles are measured around \n" \
  "                         # the positive Z axis (counterclockwise, if Z is up)\n" \
  "                         # with zero angle being forward along the x axis\n" \
  "                         \n" \
  "float32 angle_min        # start angle of the scan [rad]\n" \
  "float32 angle_max        # end angle of the scan [rad]\n" \
  "float32 angle_increment  # angular distance between measurements [rad]\n" \
  "\n" \
  "float32 time_increment   # time between measurements [seconds] - if your scanner\n" \
  "                         # is moving, this will be used in interpolating position\n" \
  "                         # of 3d points\n" \
  "float32 scan_time        # time between scans [seconds]\n" \
  "\n" \
  "float32 range_min        # minimum range value [m]\n" \
  "float32 range_max        # maximum range value [m]\n" \
  "\n" \
  "float32[] ranges         # range data [m] (Note: values < range_min or > range_max should be discarded)\n" \
  "float32[] intensities    # intensity data [device-specific units].  If your\n" \
  "                         # device does not provide intensities, please leave\n" \
  "                         # the array empty.\n" \
  "\n" \
  "================================================================================\n" \
  "MSG: std_msgs/Header\n" \
  "# Standard metadata for higher-level stamped data types.\n" \
  "# This is generally used to communicate timestamped data \n" \
  "# in a particular coordinate frame.\n" \
  "# \n" \
  "# sequence ID: consecutively increasing ID \n" \
  "uint32 seq\n" \
  "#Two-integer timestamp that is expressed as:\n" \
  "# * stamp.sec: seconds (stamp_secs) since epoch (in Python the variable is called 'secs')\n" \
  "# * stamp.nsec: nanoseconds since stamp_secs (in Python the variable is called 'nsecs')\n" \
  "# time-handling sugar is provided by the client library\n" \
  "time stamp\n" \
  "#Frame this data is associated with\n" \
  "string frame_id\n"

/* --- internal cursor helpers --- */
typedef struct {
  const uint8_t* p;
  const uint8_t* end;
  bool ok;
} rosc_reader_;

static inline const uint8_t* rosc_take_(rosc_reader_* r, size_t n) {
  if (!r->ok || (size_t)(r->end - r->p) < n) {
    r->ok = false;
    return NULL;
  }
  const uint8_t* q = r->p;
  r->p += n;
  return q;
}
static inline uint32_t rosc_u32_(rosc_reader_* r) {
  uint32_t v = 0;
  const uint8_t* q = rosc_take_(r, 4);
  if (q) memcpy(&v, q, 4);
  return v;
}
static inline float rosc_f32_(rosc_reader_* r) {
  float v = 0;
  const uint8_t* q = rosc_take_(r, 4);
  if (q) memcpy(&v, q, 4);
  return v;
}
static inline double rosc_f64_(rosc_reader_* r) {
  double v = 0;
  const uint8_t* q = rosc_take_(r, 8);
  if (q) memcpy(&v, q, 8);
  return v;
}
/* Length-prefixed block of n elements of elem bytes; returns a pointer into the buffer. */
static inline const uint8_t* rosc_array_(rosc_reader_* r, size_t elem, size_t* n) {
  *n = rosc_u32_(r);
  if (r->ok && *n > (size_t)(r->end - r->p) / elem) r->ok = false;
  return rosc_take_(r, *n * elem);
}
static inline uint8_t* rosc_put_(uint8_t* p, const void* v, size_t n) {
  if (n) memcpy(p, v, n);
  return p + n;
}
static inline uint8_t* rosc_put_u32_(uint8_t* p, uint32_t v) { return rosc_put_(p, &v, 4); }
static inline uint8_t* rosc_put_f32_(uint8_t* p, float v) { return rosc_put_(p, &v, 4); }

/* Reads element i of a float32 array that may be unaligned. */
static inline float rosc_float_at(const void* array, size_t i) {
  float v;
  memcpy(&v, (const uint8_t*)array + i * 4, 4);
  return v;
}

/* All *_serialize functions return the serialized size and write only when it fits in cap.
   All *_parse functions return false on malformed input; pointers they set point into data. */

/* --- std_msgs/String --- */
static inline size_t rosc_string_serialize(const char* s, size_t len, void* buf, size_t cap) {
  size_t need = 4 + len;
  if (need <= cap) rosc_put_(rosc_put_u32_((uint8_t*)buf, (uint32_t)len), s, len);
  return need;
}
/* *s is not NUL-terminated. */
static inline bool rosc_string_parse(const void* data, size_t size, const char** s, size_t* len) {
  rosc_reader_ r = {(const uint8_t*)data, (const uint8_t*)data + size, true};
  *s = (const char*)rosc_array_(&r, 1, len);
  return r.ok && r.p == r.end;
}

/* --- geometry_msgs/Twist --- */
typedef struct {
  double linear[3];
  double angular[3];
} rosc_twist_t;

static inline size_t rosc_twist_serialize(const rosc_twist_t* t, void* buf, size_t cap) {
  if (cap >= 48) rosc_put_(rosc_put_((uint8_t*)buf, t->linear, 24), t->angular, 24);
  return 48;
}
static inline bool rosc_twist_parse(const void* data, size_t size, rosc_twist_t* t) {
  rosc_reader_ r = {(const uint8_t*)data, (const uint8_t*)data + size, true};
  for (int i = 0; i < 3; i++) t->linear[i] = rosc_f64_(&r);
  for (int i = 0; i < 3; i++) t->angular[i] = rosc_f64_(&r);
  return r.ok && r.p == r.end;
}

/* --- sensor_msgs/LaserScan --- */
typedef struct {
  uint32_t seq;
  uint32_t stamp_sec;
  uint32_t stamp_nsec;
  const char* frame_id; /* not NUL-terminated */
  size_t frame_id_len;
  float angle_min, angle_max, angle_increment, time_increment, scan_time, range_min, range_max;
  const void* ranges; /* float32 x ranges_len, read with rosc_float_at */
  size_t ranges_len;
  const void* intensities;
  size_t intensities_len;
} rosc_laser_scan_t;

static inline size_t rosc_laser_scan_serialize(const rosc_laser_scan_t* m, void* buf, size_t cap) {
  size_t need = 4 * 3 + 4 + m->frame_id_len + 4 * 7 + 4 + 4 * m->ranges_len + 4 + 4 * m->intensities_len;
  if (need > cap) return need;
  uint8_t* p = (uint8_t*)buf;
  p = rosc_put_u32_(p, m->seq);
  p = rosc_put_u32_(p, m->stamp_sec);
  p = rosc_put_u32_(p, m->stamp_nsec);
  p = rosc_put_(rosc_put_u32_(p, (uint32_t)m->frame_id_len), m->frame_id, m->frame_id_len);
  const float f[7] = {m->angle_min, m->angle_max, m->angle_increment, m->time_increment,
                      m->scan_time, m->range_min, m->range_max};
  for (int i = 0; i < 7; i++) p = rosc_put_f32_(p, f[i]);
  p = rosc_put_(rosc_put_u32_(p, (uint32_t)m->ranges_len), m->ranges, 4 * m->ranges_len);
  rosc_put_(rosc_put_u32_(p, (uint32_t)m->intensities_len), m->intensities, 4 * m->intensities_len);
  return need;
}
static inline bool rosc_laser_scan_parse(const void* data, size_t size, rosc_laser_scan_t* m) {
  rosc_reader_ r = {(const uint8_t*)data, (const uint8_t*)data + size, true};
  m->seq = rosc_u32_(&r);
  m->stamp_sec = rosc_u32_(&r);
  m->stamp_nsec = rosc_u32_(&r);
  m->frame_id = (const char*)rosc_array_(&r, 1, &m->frame_id_len);
  m->angle_min = rosc_f32_(&r);
  m->angle_max = rosc_f32_(&r);
  m->angle_increment = rosc_f32_(&r);
  m->time_increment = rosc_f32_(&r);
  m->scan_time = rosc_f32_(&r);
  m->range_min = rosc_f32_(&r);
  m->range_max = rosc_f32_(&r);
  m->ranges = rosc_array_(&r, 4, &m->ranges_len);
  m->intensities = rosc_array_(&r, 4, &m->intensities_len);
  return r.ok && r.p == r.end;
}

#endif
