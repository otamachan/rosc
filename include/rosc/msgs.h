/* Serialization helpers and type info for a few common ROS1 message types. */
#ifndef ROSC_MSGS_H
#define ROSC_MSGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rosc/rosc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Type name, md5sum and full definition, as passed to rosc_advertise / rosc_subscribe. */
typedef struct {
  const char* type;
  const char* md5sum;
  const char* definition;
} rosc_msg_type_t;

ROSC_API extern const rosc_msg_type_t rosc_std_msgs_String;
ROSC_API extern const rosc_msg_type_t rosc_geometry_msgs_Twist;
ROSC_API extern const rosc_msg_type_t rosc_sensor_msgs_LaserScan;

typedef struct {
  double linear[3];
  double angular[3];
} rosc_geometry_msgs_Twist_t;

typedef struct {
  uint32_t seq;
  uint32_t stamp_sec;
  uint32_t stamp_nsec;
  const char* frame_id; /* not NUL-terminated */
  size_t frame_id_len;
  float angle_min, angle_max, angle_increment, time_increment, scan_time, range_min, range_max;
  const void* ranges; /* float32 x ranges_len, may be unaligned: read with rosc_float_at */
  size_t ranges_len;
  const void* intensities;
  size_t intensities_len;
} rosc_sensor_msgs_LaserScan_t;

/* rosc_serialize_* return the serialized size and write only when it fits in cap.
   rosc_deserialize_* return false on malformed input; pointers they set point into data. */
ROSC_API size_t rosc_serialize_std_msgs_String(const char* s, size_t len, void* buf, size_t cap);
ROSC_API bool rosc_deserialize_std_msgs_String(const void* data, size_t size, const char** s, size_t* len);
ROSC_API size_t rosc_serialize_geometry_msgs_Twist(const rosc_geometry_msgs_Twist_t* t, void* buf, size_t cap);
ROSC_API bool rosc_deserialize_geometry_msgs_Twist(const void* data, size_t size, rosc_geometry_msgs_Twist_t* t);
ROSC_API size_t rosc_serialize_sensor_msgs_LaserScan(const rosc_sensor_msgs_LaserScan_t* m, void* buf, size_t cap);
ROSC_API bool rosc_deserialize_sensor_msgs_LaserScan(const void* data, size_t size, rosc_sensor_msgs_LaserScan_t* m);

/* Element i of a float32 array that may be unaligned. */
ROSC_API float rosc_float_at(const void* array, size_t i);

#ifdef __cplusplus
}
#endif

#endif
