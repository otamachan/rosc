# rosc

Publish and subscribe ROS1 topics from C (or C++) without roscpp.

rosc speaks the ROS1 protocols itself — the master/slave XML-RPC APIs and TCPROS — so it has no
dependencies beyond the C++17 standard library and POSIX sockets. Messages are passed as serialized
bytes; `rosc/msgs.h` has header-only helpers for `std_msgs/String`, `geometry_msgs/Twist` and
`sensor_msgs/LaserScan`.

Supported: topics (publish, subscribe, `"*"` type, latching), `ROS_MASTER_URI` / `ROS_IP` /
`ROS_HOSTNAME` / `ROS_NAMESPACE`, `from:=to` remapping. Not supported: services, the parameter
server, UDPROS, IPv6. Linux and macOS.

## Example

```c
#include <stdio.h>

#include <rosc/msgs.h>
#include <rosc/rosc.h>

static void on_scan(const void* data, size_t size, const rosc_msg_info_t* info, void* user) {
  rosc_sensor_msgs_LaserScan_t scan;
  if (rosc_deserialize_sensor_msgs_LaserScan(data, size, &scan) && scan.ranges_len > 0)
    printf("first range: %f\n", rosc_float_at(scan.ranges, 0));
}

int main(int argc, char** argv) {
  rosc_node_options_t opts = {0};
  opts.args = (const char* const*)argv;  /* picks up remaps like scan:=/front/scan */
  opts.num_args = argc;
  rosc_node_t* node;
  rosc_pub_t* pub;
  rosc_sub_t* sub;
  if (rosc_node_create("teleop", &opts, &node) != ROSC_OK ||
      rosc_advertise(node, "cmd_vel", rosc_geometry_msgs_Twist.type, rosc_geometry_msgs_Twist.md5sum,
                     rosc_geometry_msgs_Twist.definition, 1, false, &pub) != ROSC_OK ||
      rosc_subscribe(node, "scan", rosc_sensor_msgs_LaserScan.type, rosc_sensor_msgs_LaserScan.md5sum,
                     1, on_scan, NULL, &sub) != ROSC_OK) {
    fprintf(stderr, "rosc: %s\n", rosc_last_error());
    return 1;
  }
  while (rosc_spin_once(node, 100) == ROSC_OK) {
    rosc_geometry_msgs_Twist_t twist = {{0.1, 0, 0}, {0, 0, 0}};
    uint8_t buf[48];
    rosc_publish(pub, buf, rosc_serialize_geometry_msgs_Twist(&twist, buf, sizeof(buf)));
  }
  rosc_node_destroy(node);
}
```

Callbacks run on the thread that calls `rosc_spin_once`. `rosc_publish` may be called from any
thread. All sockets are served by one internal I/O thread; registering with the master blocks the
calling thread.

## Build

CMake 3.20+ and a C++17 compiler. Builds both `librosc.so` (`rosc::rosc`) and `librosc.a`
(`rosc::rosc_static`); after `cmake --install`, use `find_package(rosc)`.

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

Or in docker:

```sh
docker build -t rosc-build .
docker run --rm -u $(id -u):$(id -g) -v $PWD:/src -w /src rosc-build \
  sh -c 'cmake -S . -B build && cmake --build build && ctest --test-dir build'
```

`tests/interop.sh build` checks both directions against `roscore` / `rostopic` from the
`ros:noetic` docker image (needs docker; binaries must run on `ubuntu:24.04`).

## License

Apache-2.0
