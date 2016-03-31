#!/usr/bin/python
#
# Sigma Control API DUT (sniffer_control_field_check)
# Copyright (c) 2014, Qualcomm Atheros, Inc.
# All Rights Reserved.
# Licensed under the Clear BSD license. See README for more details.

import sys
import subprocess
import tshark

framename = None
wsc_state = None
pvb_bit = None
moredata_bit = None
eosp_bit = None

for arg in sys.argv:
    if arg.startswith("FileName="):
        infile = arg.split("=", 1)[1]
    elif arg.startswith("SrcMac="):
        srcmac = arg.split("=", 1)[1]
    elif arg.startswith("FrameName="):
        framename = arg.split("=", 1)[1].lower()
    elif arg.startswith("WSC_State="):
        wsc_state = arg.split("=", 1)[1]
    elif arg.startswith("pvb_bit="):
        pvb_bit = arg.split("=", 1)[1]
    elif arg.startswith("MoreData_bit="):
        moredata_bit = arg.split("=", 1)[1]
    elif arg.startswith("EOSP_bit="):
        eosp_bit = arg.split("=", 1)[1]

filter = 'wlan.sa==' + srcmac

if framename:
    frame_filters = tshark.tshark_framenames()
    if framename not in frame_filters:
        print "errorCode,Unsupported FrameName"
        sys.exit()
    filter = filter + " and " + frame_filters[framename]

if wsc_state:
    filter = filter + " and wps.wifi_protected_setup_state == " + wsc_state

if pvb_bit:
    val = int(pvb_bit)
    if val == 1:
        filter = filter + " and wlan_mgt.tim.partial_virtual_bitmap != 0"
    elif val == 0:
        filter = filter + " and wlan_mgt.tim.partial_virtual_bitmap == 0"
    else:
        filter = filter + " and wlan_mgt.tim.partial_virtual_bitmap == " + pvb_bit

if moredata_bit:
    filter = filter + " and wlan.fc.moredata == " + moredata_bit

if eosp_bit:
    filter = filter + " and wlan.qos.eosp == " + eosp_bit

cmd = ['tshark', '-r', infile, '-c', '1', '-R', filter,
       '-Tfields', '-e', 'frame.number']
tshark = subprocess.Popen(cmd, stdout=subprocess.PIPE)
data = tshark.stdout.read()
frames = data.splitlines()
if len(frames) == 0:
    print "CheckResult,FAIL"
else:
    print "CheckResult,SUCCESS"
