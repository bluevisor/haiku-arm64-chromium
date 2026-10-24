#!/bin/sh
# venus-shot.sh <name> <url|-> <seconds> [typed-url]: screenshot a throwaway
# Chromium whose ANGLE runs on Venus (~/venus-lib) instead of SwiftShader.
name=$1; url=$2; wait=${3:-20}; typed=$4
cd /boot/home/HaikuChrome || exit 1
LIBRARY_PATH="/boot/home/venus-lib:%A/lib:$LIBRARY_PATH"; export LIBRARY_PATH
ulimit -n 8192
export VN_DEBUG=${VN_DEBUG:-} MESA_LOG_LEVEL=${MESA_LOG_LEVEL:-}
prof=/tmp/venus-$name; rm -rf $prof
[ "$url" = "-" ] && url=
./Chromium --ozone-platform=haiku --no-sandbox --single-process \
	--in-process-gpu --disable-gpu-compositing \
	--use-angle=vulkan --enable-logging=stderr $VENUS_EXTRA \
	--use-fake-device-for-media-stream --no-first-run --no-default-browser-check \
	--window-position=24,40 --window-size=1800,1090 \
	--user-data-dir=$prof $url > /tmp/venus-$name.log 2>&1 < /dev/null &
pid=$!
if [ -n "$typed" ]; then
	sleep 12
	/boot/home/bin/typepost $pid "New Tab" "$typed\n" > /dev/null 2>&1
fi
sleep $wait
/boot/home/bin/hshot /boot/home/shots/$name.ppm > /dev/null 2>&1
kill $pid; sleep 3; kill -9 $pid 2>/dev/null
rm -rf $prof
echo "$name done"
