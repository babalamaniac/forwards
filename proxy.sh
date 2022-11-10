#!/bin/bash
gcc proxy.c -o proxy
strace -o proxy-trace.log ./proxy 192.168.107.1 33445