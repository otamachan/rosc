#include "rosc/msgs.h"

#include <string.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "rosc assumes a little-endian host"
#endif

const rosc_msg_type_t rosc_std_msgs_String = {
    "std_msgs/String",
    "992ce8a1687cec8c8bd883ec73ca41d1",
    "string data\n"
};

const rosc_msg_type_t rosc_geometry_msgs_Twist = {
    "geometry_msgs/Twist",
    "9f195f881246fdfa2798d1d3eebca84a",
    "# This expresses velocity in free space broken into its linear and angular parts.\n"
    "Vector3 linear\n"
    "Vector3 angular\n"
    "\n"
    "================================================================================\n"
    "MSG: geometry_msgs/Vector3\n"
    "# This represents a vector in free space. \n"
    "# It is only meant to represent a direction. Therefore, it does not\n"
    "# make sense to apply a translation to it (e.g., when applying a \n"
    "# generic rigid transformation to a Vector3, tf2 will only apply the\n"
    "# rotation). If you want your data to be translatable too, use the\n"
    "# geometry_msgs/Point message instead.\n"
    "\n"
    "float64 x\n"
    "float64 y\n"
    "float64 z"
};

const rosc_msg_type_t rosc_sensor_msgs_LaserScan = {
    "sensor_msgs/LaserScan",
    "90c7ef2dc6895d81024acba2ac42f369",
    "# Single scan from a planar laser range-finder\n"
    "#\n"
    "# If you have another ranging device with different behavior (e.g. a sonar\n"
    "# array), please find or create a different message, since applications\n"
    "# will make fairly laser-specific assumptions about this data\n"
    "\n"
    "Header header            # timestamp in the header is the acquisition time of \n"
    "                         # the first ray in the scan.\n"
    "                         #\n"
    "                         # in frame frame_id, angles are measured around \n"
    "                         # the positive Z axis (counterclockwise, if Z is up)\n"
    "                         # with zero angle being forward along the x axis\n"
    "                         \n"
    "float32 angle_min        # start angle of the scan [rad]\n"
    "float32 angle_max        # end angle of the scan [rad]\n"
    "float32 angle_increment  # angular distance between measurements [rad]\n"
    "\n"
    "float32 time_increment   # time between measurements [seconds] - if your scanner\n"
    "                         # is moving, this will be used in interpolating position\n"
    "                         # of 3d points\n"
    "float32 scan_time        # time between scans [seconds]\n"
    "\n"
    "float32 range_min        # minimum range value [m]\n"
    "float32 range_max        # maximum range value [m]\n"
    "\n"
    "float32[] ranges         # range data [m] (Note: values < range_min or > range_max should be discarded)\n"
    "float32[] intensities    # intensity data [device-specific units].  If your\n"
    "                         # device does not provide intensities, please leave\n"
    "                         # the array empty.\n"
    "\n"
    "================================================================================\n"
    "MSG: std_msgs/Header\n"
    "# Standard metadata for higher-level stamped data types.\n"
    "# This is generally used to communicate timestamped data \n"
    "# in a particular coordinate frame.\n"
    "# \n"
    "# sequence ID: consecutively increasing ID \n"
    "uint32 seq\n"
    "#Two-integer timestamp that is expressed as:\n"
    "# * stamp.sec: seconds (stamp_secs) since epoch (in Python the variable is called 'secs')\n"
    "# * stamp.nsec: nanoseconds since stamp_secs (in Python the variable is called 'nsecs')\n"
    "# time-handling sugar is provided by the client library\n"
    "time stamp\n"
    "#Frame this data is associated with\n"
    "string frame_id\n"
};

typedef struct {
  const uint8_t* p;
  const uint8_t* end;
  bool ok;
} reader;

static const uint8_t* take(reader* r, size_t n) {
  if (!r->ok || (size_t)(r->end - r->p) < n) {
    r->ok = false;
    return NULL;
  }
  const uint8_t* q = r->p;
  r->p += n;
  return q;
}

static void get(reader* r, void* v, size_t n) {
  const uint8_t* q = take(r, n);
  if (q) memcpy(v, q, n);
}

static uint32_t get_u32(reader* r) {
  uint32_t v = 0;
  get(r, &v, 4);
  return v;
}

static float get_f32(reader* r) {
  float v = 0;
  get(r, &v, 4);
  return v;
}

/* Length-prefixed array of elem-byte elements; returns a pointer into the buffer. */
static const uint8_t* get_array(reader* r, size_t elem, size_t* n) {
  *n = get_u32(r);
  if (r->ok && *n > (size_t)(r->end - r->p) / elem) r->ok = false;
  return take(r, *n * elem);
}

static uint8_t* put(uint8_t* p, const void* v, size_t n) {
  if (n) memcpy(p, v, n);
  return p + n;
}

static uint8_t* put_u32(uint8_t* p, uint32_t v) { return put(p, &v, 4); }

float rosc_float_at(const void* array, size_t i) {
  float v;
  memcpy(&v, (const uint8_t*)array + i * 4, 4);
  return v;
}

size_t rosc_serialize_std_msgs_String(const char* s, size_t len, void* buf, size_t cap) {
  size_t need = 4 + len;
  if (need <= cap) put(put_u32(buf, (uint32_t)len), s, len);
  return need;
}

bool rosc_deserialize_std_msgs_String(const void* data, size_t size, const char** s, size_t* len) {
  reader r = {data, (const uint8_t*)data + size, true};
  *s = (const char*)get_array(&r, 1, len);
  return r.ok && r.p == r.end;
}

size_t rosc_serialize_geometry_msgs_Twist(const rosc_geometry_msgs_Twist_t* t, void* buf, size_t cap) {
  if (cap >= 48) put(put(buf, t->linear, 24), t->angular, 24);
  return 48;
}

bool rosc_deserialize_geometry_msgs_Twist(const void* data, size_t size, rosc_geometry_msgs_Twist_t* t) {
  reader r = {data, (const uint8_t*)data + size, true};
  get(&r, t->linear, 24);
  get(&r, t->angular, 24);
  return r.ok && r.p == r.end;
}

size_t rosc_serialize_sensor_msgs_LaserScan(const rosc_sensor_msgs_LaserScan_t* m, void* buf, size_t cap) {
  size_t need = 4 * 3 + 4 + m->frame_id_len + 4 * 7 + 4 + 4 * m->ranges_len + 4 + 4 * m->intensities_len;
  if (need > cap) return need;
  const float f[7] = {m->angle_min, m->angle_max, m->angle_increment, m->time_increment,
                      m->scan_time, m->range_min, m->range_max};
  uint8_t* p = buf;
  p = put_u32(p, m->seq);
  p = put_u32(p, m->stamp_sec);
  p = put_u32(p, m->stamp_nsec);
  p = put(put_u32(p, (uint32_t)m->frame_id_len), m->frame_id, m->frame_id_len);
  p = put(p, f, sizeof(f));
  p = put(put_u32(p, (uint32_t)m->ranges_len), m->ranges, 4 * m->ranges_len);
  put(put_u32(p, (uint32_t)m->intensities_len), m->intensities, 4 * m->intensities_len);
  return need;
}

bool rosc_deserialize_sensor_msgs_LaserScan(const void* data, size_t size, rosc_sensor_msgs_LaserScan_t* m) {
  reader r = {data, (const uint8_t*)data + size, true};
  m->seq = get_u32(&r);
  m->stamp_sec = get_u32(&r);
  m->stamp_nsec = get_u32(&r);
  m->frame_id = (const char*)get_array(&r, 1, &m->frame_id_len);
  m->angle_min = get_f32(&r);
  m->angle_max = get_f32(&r);
  m->angle_increment = get_f32(&r);
  m->time_increment = get_f32(&r);
  m->scan_time = get_f32(&r);
  m->range_min = get_f32(&r);
  m->range_max = get_f32(&r);
  m->ranges = get_array(&r, 4, &m->ranges_len);
  m->intensities = get_array(&r, 4, &m->intensities_len);
  return r.ok && r.p == r.end;
}
