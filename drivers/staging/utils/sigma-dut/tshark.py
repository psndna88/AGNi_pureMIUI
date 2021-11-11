#!/usr/bin/python
#
# Sigma Control API DUT (helpers for calling tshark)
# Copyright (c) 2014, Qualcomm Atheros, Inc.
# All Rights Reserved.
# Licensed under the Clear BSD license. See README for more details.

def tshark_fieldnames():
    fields = {}
    with open("sniffer-tshark-fields.txt", "r") as f:
        for l in f.read().splitlines():
            [sigma_name,tshark_name] = l.split('\t')
            fields[sigma_name.lower()] = tshark_name
    return fields

def tshark_framenames():
    frames = {}
    with open("sniffer-tshark-frames.txt", "r") as f:
        for l in f.read().splitlines():
            [sigma_name,tshark_name] = l.split('\t')
            frames[sigma_name.lower()] = tshark_name
    return frames

def tshark_hasfields():
    fields = {}
    with open("sniffer-tshark-hasfields.txt", "r") as f:
        for l in f.read().splitlines():
            [sigma_name,tshark_name] = l.split('\t')
            fields[sigma_name.lower()] = tshark_name
    return fields
