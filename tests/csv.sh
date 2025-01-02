#! /bin/bash

set -e -x
./mysqlpcap -i tests/multi-con.pcap --ip 127.0.0.1 --port 3306 --record-for-replay tests/multi-con.mcap
./mysqlpcap -i tests/multi-con.mcap --query-pattern "s/.*1.*/select/" --replay-host 127.0.0.1 --replay-user=mysqlpcap --replay-pw=mysqlpcap --replay-port=3306 --run --assert-on-query-error --csv out.csv
