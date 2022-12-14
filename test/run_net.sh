#!/bin/bash
#Fail if any test fails
set -e
set -o pipefail

appdir=../app
if [ ! -f "${appdir}/pserver" ]; then
   appdir="../bin"
   if [ ! -f "${appdir}/pserver" ]; then
       echo "Could not find application directory"
       exit
   fi
fi

srcdir=@srcdir@
if [ ! -d "data" ]; then
  ln -s $srcdir/data data
fi

echo "run test ZMQNet"
echo "First run web-server for NetStatSenderTest"
python3 ${appdir}/ws_flask_stat.py &
sleep 1

mpirun --allow-run-as-root --oversubscribe -n 1 mainNet -n 1 &
test_pid=$!

sleep 1
echo "pclient 1 rank"
mpirun --allow-run-as-root --oversubscribe -n 1 ${appdir}/pclient "tcp://localhost:5559"

sleep 1
echo "pclient 10 rank"
mpirun --allow-run-as-root --oversubscribe -n 10 ${appdir}/pclient "tcp://localhost:5559"

sleep 1
echo "pclient 10 rank"
mpirun --allow-run-as-root --oversubscribe -n 10 ${appdir}/pclient "tcp://localhost:5559"

sleep 1
echo "pclient_stats 10 rank"
mpirun --allow-run-as-root --oversubscribe -n 10 ${appdir}/pclient_stats "tcp://localhost:5559"

sleep 1
echo "pclient_stats 10 rank"
mpirun --allow-run-as-root --oversubscribe -n 10 ${appdir}/pclient_stats "tcp://localhost:5559"

wait $test_pid
curl -X POST "http://0.0.0.0:5000/shutdown"
