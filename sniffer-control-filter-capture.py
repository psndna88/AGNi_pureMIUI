#!/usr/bin/python
#
# Sigma Control API DUT (sniffer_control_filter_capture)
# Copyright (c) 2014, Qualcomm Atheros, Inc.
# All Rights Reserved.
# Licensed under the Clear BSD license. See README for more details.

import sys
import subprocess
import tshark

framename = None
hasfield = None
datalen = None

for arg in sys.argv:
    if arg.startswith("InFile="):
        infile = arg.split("=", 1)[1]
    elif arg.startswith("OutFile="):
        outfile = arg.split("=", 1)[1]
    elif arg.startswith("SrcMac="):
        srcmac = arg.split("=", 1)[1]
    elif arg.startswith("FrameName="):
        framename = arg.split("=", 1)[1].lower()
    elif arg.startswith("HasField="):
        hasfield = arg.split("=", 1)[1].lower()
    elif arg.startswith("Nframes="):
        nframes = arg.split("=", 1)[1]
    elif arg.startswith("Datalen="):
        datalen = arg.split("=", 1)[1]

filter = 'wlan.sa==' + srcmac

if framename:
    frame_filters = tshark.tshark_framenames()
    if framename not in frame_filters:
        print "errorCode,Unsupported FrameName"
        sys.exit()

    filter = filter + " and " + frame_filters[framename]

if hasfield:
    fields = tshark.tshark_hasfields()
    if hasfield not in fields:
        print "errorCode,Unsupported HasField"
        sys.exit()
    filter = filter + " and " + fields[hasfield]

if datalen:
    filter = filter + " and wlan.fc.type == 2 and data.len == " + datalen

if nframes == "last":
    cmd = ['tshark', '-r', infile, '-R', filter,
           '-Tfields', '-e', 'frame.number']
    tshark = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    data = tshark.stdout.read()
    frames = data.splitlines()
    if len(frames) == 0:
        print "CheckResult,NoPacketsFound"
        sys.exit()
    filter = "frame.number == " + frames[-1]
    nframes = "1"
elif nframes == "all":
    nframes = "9999999"

cmd = ['tshark', '-r', infile, '-w', outfile,
       '-c', nframes,
       '-R', filter]
tshark = subprocess.Popen(cmd, stdout=subprocess.PIPE)
data = tshark.stdout.read()

cmd = ['tshark', '-r', outfile, '-c', '1', '-Tfields', '-e', 'frame.number']
tshark = subprocess.Popen(cmd, stdout=subprocess.PIPE)
data = tshark.stdout.read().rstrip()

result = "SUCCESS" if len(data) > 0 else "NoPacketsFound"
print "CheckResult,%s" % (result)
