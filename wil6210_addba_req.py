#!/usr/bin/python
#
# Sigma Control API DUT (wil6210_addba_req)
# Copyright (c) 2015, Qualcomm Atheros, Inc.
# All Rights Reserved.
# Licensed under the Clear BSD license. See README for more details.

import subprocess,sys
import re

dest_mac = sys.argv[1]
agg_size = sys.argv[2]
mac = re.escape(dest_mac)

debugfs_path = subprocess.check_output(['sudo','find','/sys/kernel/debug/ieee80211','-name','wil6210'])
debugfs_path = debugfs_path.rstrip()
vrings_file = debugfs_path + '/vrings'
print vrings_file
vrings = open(vrings_file,'r')
#vrings = open('/home/wigig/work/vrings_example.txt','r')

#print vrings.name
line1 = vrings.readline()
while line1:
  match = re.match(mac,line1)
  if (match is not None):
#    print "I found the requested MAC\n"
    break
  line1 = vrings.readline()

if line1:
  vring_line = vrings.readline()
#  print "the next vring_line is",vring_line
  match = re.match(r'VRING tx_ (\d+)',vring_line)
  if match is not None:
     vring_id = match.group(1)
     back_file = debugfs_path+"/back"
     addba_cmd = "sudo echo \"add {} {}\" > {}".format(vring_id,agg_size,back_file)
     print "addba command is:", addba_cmd
#echo "add 0 11" > /sys/kernel/debug/ieee80211/phy26/wil6210/back
     ret = subprocess.call(addba_cmd, shell=True)
     sys.exit(ret)

sys.exit(1)
