#!/usr/bin/python
#
# Sigma Control API DUT (sniffer_get_field_value)
# Copyright (c) 2014, Qualcomm Atheros, Inc.
# All Rights Reserved.
# Licensed under the Clear BSD license. See README for more details.

import sys
import subprocess
import tshark

for arg in sys.argv:
    if arg.startswith("FileName="):
        filename = arg.split("=", 1)[1]
    elif arg.startswith("SrcMac="):
        srcmac = arg.split("=", 1)[1]
    elif arg.startswith("FrameName="):
        framename = arg.split("=", 1)[1].lower()
    elif arg.startswith("FieldName="):
        fieldname = arg.split("=", 1)[1].lower()

frame_filters = tshark.tshark_framenames()
if framename not in frame_filters:
    print "errorCode,Unsupported FrameName"
    sys.exit()

fields = tshark.tshark_fieldnames()
if fieldname not in fields:
    print "errorCode,Unsupported FieldName"
    sys.exit()

cmd = ['tshark', '-r', filename,
       '-c', '1',
       '-R', 'wlan.sa==' + srcmac + " and " + frame_filters[framename],
       '-Tfields',
       '-e', fields[fieldname]]
tshark = subprocess.Popen(cmd, stdout=subprocess.PIPE)
data = tshark.stdout.read().rstrip()
result = "SUCCESS" if len(data) > 0 else "FAIL"
print "CheckResult,%s,ReturnValue,%s" % (result, data)
