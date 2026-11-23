#!/usr/bin/env bash
# Interop test against ROS Noetic (roscore, rostopic) in docker.
# usage: tests/interop.sh BUILD_DIR   (BUILD_DIR holds rosc_talker / rosc_listener built for ubuntu:24.04)
set -euo pipefail

BUILD=$(cd "$1" && pwd)
ROS_IMAGE=${ROS_IMAGE:-ros:noetic}
RUN_IMAGE=${RUN_IMAGE:-ubuntu:24.04}
ID=$$
NET=rosc-interop-$ID
CONTAINERS=()
FAILED=0

cleanup() {
  [ ${#CONTAINERS[@]} -gt 0 ] && docker rm -f "${CONTAINERS[@]}" >/dev/null 2>&1 || true
  docker network rm "$NET" >/dev/null 2>&1 || true
}
trap cleanup EXIT

# Each container's ROS_HOSTNAME is its own name, resolvable on the docker network.
run() {  # run NAME IMAGE DETACH CMD...
  local name=$1-$ID image=$2 detach=$3
  shift 3
  CONTAINERS+=("$name")
  docker run --rm $detach --name "$name" --hostname "$name" --network "$NET" \
    -e ROS_MASTER_URI=http://master-$ID:11311 -e ROS_HOSTNAME="$name" -v "$BUILD":/build:ro "$image" "$@"
}
ros() { local n=$1 d=$2; shift 2; run "$n" "$ROS_IMAGE" "$d" bash -c "source /opt/ros/noetic/setup.bash && $*"; }
rosc() { local n=$1 d=$2; shift 2; run "$n" "$RUN_IMAGE" "$d" timeout 60 "$@"; }
stop() { docker rm -f "$1-$ID" >/dev/null 2>&1 || true; }

check() {  # check NAME EXPECTED OUTPUT
  if grep -qF -- "$2" <<<"$3"; then
    echo "PASS $1"
  else
    echo "FAIL $1: expected '$2' in:"
    echo "$3" | sed 's/^/    /'
    FAILED=1
  fi
}

docker network create "$NET" >/dev/null
ros master -d roscore >/dev/null
for _ in $(seq 60); do
  docker exec master-$ID bash -c 'source /opt/ros/noetic/setup.bash && rostopic list' >/dev/null 2>&1 && break
  sleep 1
done

# ROS -> rosc
ros pub1 -d "rostopic pub -r 10 /chatter std_msgs/String 'data: hello'" >/dev/null
check "rostopic pub -> rosc (String)" "string: hello" "$(rosc l1 '' /build/rosc_listener /chatter string 3 2>&1 || true)"
check "rostopic pub -> rosc (*)" "any: type=std_msgs/String md5=992ce8a1687cec8c8bd883ec73ca41d1" \
  "$(rosc l2 '' /build/rosc_listener /chatter any 1 2>&1 || true)"
check "remap" "string: hello" "$(rosc l3 '' /build/rosc_listener other string 1 other:=/chatter 2>&1 || true)"
stop pub1

ros pub2 -d "rostopic pub -r 10 /cmd_vel geometry_msgs/Twist '{linear: {x: 0.5}, angular: {z: 1.0}}'" >/dev/null
check "rostopic pub -> rosc (Twist)" "twist: 0.5 0 0 0 0 1" "$(rosc l4 '' /build/rosc_listener /cmd_vel twist 1 2>&1 || true)"
stop pub2

ros pub3 -d "rostopic pub -r 10 /scan sensor_msgs/LaserScan '{header: {frame_id: base}, angle_min: -1.0, ranges: [3.0, 4.0]}'" >/dev/null
check "rostopic pub -> rosc (LaserScan)" "scan: frame=base n=2 r0=3 angle_min=-1" \
  "$(rosc l5 '' /build/rosc_listener /scan scan 1 2>&1 || true)"
stop pub3

# rosc -> ROS
rosc t1 -d /build/rosc_talker /rosc_chatter string >/dev/null
check "rosc -> rostopic echo (String)" "data: \"hello from rosc" "$(ros e1 '' timeout 30 rostopic echo -n 1 /rosc_chatter 2>&1 || true)"
stop t1

rosc t2 -d /build/rosc_talker /rosc_cmd_vel twist >/dev/null
check "rosc -> rostopic echo (Twist)" "x: 0.5" "$(ros e2 '' timeout 30 rostopic echo -n 1 /rosc_cmd_vel 2>&1 || true)"
stop t2

rosc t3 -d /build/rosc_talker /rosc_scan scan >/dev/null
check "rosc -> rostopic echo (LaserScan)" "ranges: [1.0, 1.5, 2.0, 2.5" "$(ros e3 '' timeout 30 rostopic echo -n 1 /rosc_scan 2>&1 || true)"
stop t3

# Latched: published once before the subscriber exists.
rosc t4 -d /build/rosc_talker /rosc_latched string 1 latch >/dev/null
sleep 3
check "latch" "data: \"hello from rosc 0\"" "$(ros e4 '' timeout 30 rostopic echo -n 1 /rosc_latched 2>&1 || true)"
stop t4

# rosc -> rosc through the ROS master
rosc t5 -d /build/rosc_talker /rosc_loop string >/dev/null
check "rosc -> rosc" "string: hello from rosc" "$(rosc l6 '' /build/rosc_listener /rosc_loop string 2 2>&1 || true)"
stop t5

exit $FAILED
