#!/bin/bash
gcc main.c -o main
strace -o main-trace.log ./main 192.168.107.1 33445 192.168.107.6 33445