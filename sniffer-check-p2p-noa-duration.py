#!/usr/bin/python
#
# Sigma Control API DUT (sniffer - P2P NoA check)
# Copyright (c) 2014, Qualcomm Atheros, Inc.
# All Rights Reserved.
# Licensed under the Clear BSD license. See README for more details.

import sys
import subprocess

infile = sys.argv[1]
bssid = sys.argv[2]
srcmac = sys.argv[3]
destmac = sys.argv[4]

tshark = subprocess.Popen(['tshark', '-r', infile,
                           '-c', '1',
                           '-R', 'wlan.sa==' + bssid + " and wlan.fc.type_subtype==8",
                           '-Tfields',
                           '-e', 'radiotap.mactime',
                           '-e', 'wlan_mgt.fixed.timestamp',
                           '-e', 'wifi_p2p.noa.duration',
                           '-e', 'wifi_p2p.noa.interval',
                           '-e', 'wifi_p2p.noa.start_time',
                           '-e', 'frame.time_relative'],
                          stdout=subprocess.PIPE)
noa_data = tshark.stdout.read()
vals = noa_data.rstrip().split('\t')
mactime = int(vals[0])
timestamp = int(vals[1], 16)
noa_duration = int(vals[2])
noa_interval = int(vals[3])
noa_start = int(vals[4])
frame_time = float(vals[5])

if noa_start > timestamp:
    print "FilterStatus,FAIL,reasonCode,Unexpected NoA Start Time after Beacon timestamp"
    sys.exit()

noa_start_mactime = mactime - (timestamp - noa_start)
noa_start_frame_time = frame_time - (timestamp - noa_start) / 1000000.0

debug = open(infile + ".txt", "w")
debug.write("mactime={}\ntimestamp={}\nnoa_duration={}\nnoa_interval={}\nnoa_start={}\nnoa_start_mactime={}\nnoa_start_frame_time={}\n".format(mactime, timestamp, noa_duration, noa_interval, noa_start, noa_start_mactime, noa_start_frame_time))

tshark = subprocess.Popen(['tshark', '-r', infile,
                           '-R', 'wlan.da==' + destmac + " and wlan.sa==" + srcmac,
                           '-Tfields',
                           '-e', 'frame.number',
                           '-e', 'radiotap.mactime',
                           '-e', 'frame.time_relative'],
                          stdout=subprocess.PIPE)
frames = tshark.stdout.read()
error_frame = None
debug.write("\nframenum mactime offset(mactime) offset(frametime)\n")
for f in frames.splitlines():
    vals = f.rstrip().split('\t')
    framenum = int(vals[0])
    mactime = int(vals[1])
    frametime = float(vals[2])
    if mactime < noa_start_mactime:
        print "FilterStatus,FAIL,reasonCode,Unexpected mactime before NoA Start Time"
        sys.exit()
    offset = (mactime - noa_start_mactime) % noa_interval
    offset_t = ((frametime - noa_start_frame_time) * 1000000) % noa_interval
    debug.write("{} {} {} {}\n".format(framenum, mactime, offset, offset_t))
    # allow 200 us as extra buffer to compensate for sniffer inaccuracy
    allow_buffer = 200
    if offset > allow_buffer and offset + allow_buffer < noa_duration:
        debug.write("Frame {} during GO absence (mactime) (mactime={}, offset={} usec, NoA-duration={} usec)\n".format(framenum, mactime, offset, noa_duration))
        error_frame = framenum
    if offset_t > allow_buffer and offset_t + allow_buffer < noa_duration:
        debug.write("Frame {} during GO absence (frametime) (frametime={}, offset={} usec, NoA-duration={} usec)\n".format(framenum, frametime, offset_t, noa_duration))
        #error_frame = framenum

if error_frame:
    print "FilterStatus,FAILURE,frameNumber," + str(error_frame)
else:
    print "FilterStatus,SUCCESS"
