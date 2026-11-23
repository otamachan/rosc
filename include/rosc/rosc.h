/* rosc: ROS1 topic publish/subscribe from C, without roscpp. */
#ifndef ROSC_ROSC_H
#define ROSC_ROSC_H

#include <stdbool.h>
#include <stddef.h>

#if defined(__GNUC__)
#define ROSC_API __attribute__((visibility("default")))
#else
#define ROSC_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ROSC_OK = 0,
  ROSC_ERR_INVALID_ARG,
  ROSC_ERR_MASTER,   /* master unreachable or rejected the request */
  ROSC_ERR_NETWORK,
  ROSC_ERR_SHUTDOWN,
  ROSC_ERR_TIMEOUT,
  ROSC_ERR_NOMEM,
  ROSC_ERR_INTERNAL
} rosc_status;

typedef struct rosc_node rosc_node_t;
typedef struct rosc_pub rosc_pub_t;
typedef struct rosc_sub rosc_sub_t;

/* Type name, md5sum and full definition of a message type. */
typedef struct {
  const char* type;
  const char* md5sum;
  const char* definition;  /* may be NULL */
} rosc_msg_type_t;

typedef struct {
  const char* master_uri;  /* NULL: ROS_MASTER_URI */
  const char* host;        /* NULL: ROS_HOSTNAME, ROS_IP, then gethostname() */
  const char* ns;          /* NULL: ROS_NAMESPACE */
  /* "from:=to" remaps and "__name:=", "__ns:=", "__master:=", "__ip:=", "__hostname:=".
     argv can be passed as is; elements without ":=" are ignored. */
  const char* const* args;
  int num_args;
  int master_timeout_ms;   /* 0: 5000 */
} rosc_node_options_t;

/* The publisher's side of a received message (useful when subscribed with type NULL). */
typedef struct {
  const char* topic;
  const char* type;
  const char* md5sum;
  const char* msg_def;
  const char* callerid;
  bool latching;
} rosc_msg_info_t;

typedef void (*rosc_sub_cb)(const void* data, size_t size, const rosc_msg_info_t* info, void* user);

/* opts may be NULL. Does not contact the master. */
ROSC_API rosc_status rosc_node_create(const char* name, const rosc_node_options_t* opts, rosc_node_t** out);
/* Unregisters everything from the master and frees the node and all its pubs/subs. */
ROSC_API void rosc_node_destroy(rosc_node_t* node);
/* Thread safe (not async-signal-safe). Wakes rosc_spin_once and makes rosc_ok false. */
ROSC_API void rosc_node_shutdown(rosc_node_t* node);
ROSC_API bool rosc_ok(const rosc_node_t* node);
/* Runs pending callbacks on the calling thread. timeout_ms < 0 waits forever.
   Returns ROSC_OK on timeout, ROSC_ERR_SHUTDOWN after shutdown. */
ROSC_API rosc_status rosc_spin_once(rosc_node_t* node, int timeout_ms);
ROSC_API const char* rosc_node_name(const rosc_node_t* node);

/* queue_size <= 0 means unbounded. Blocks while registering. */
ROSC_API rosc_status rosc_advertise(rosc_node_t* node, const char* topic, const rosc_msg_type_t* type,
                                    int queue_size, bool latch, rosc_pub_t** out);
/* Thread safe. data is serialized message bytes; copied before returning. */
ROSC_API rosc_status rosc_publish(rosc_pub_t* pub, const void* data, size_t size);
ROSC_API int rosc_pub_num_subscribers(const rosc_pub_t* pub);
ROSC_API void rosc_unadvertise(rosc_pub_t* pub);

/* type NULL accepts any type. queue_size <= 0 means unbounded. Blocks while registering. */
ROSC_API rosc_status rosc_subscribe(rosc_node_t* node, const char* topic, const rosc_msg_type_t* type,
                                    int queue_size, rosc_sub_cb cb, void* user, rosc_sub_t** out);
/* Waits for a running callback of this subscription to finish (may be called from inside it). */
ROSC_API void rosc_unsubscribe(rosc_sub_t* sub);

/* Description of the last failure on this thread. */
ROSC_API const char* rosc_last_error(void);
ROSC_API const char* rosc_status_str(rosc_status status);

#ifdef __cplusplus
}
#endif

#endif
