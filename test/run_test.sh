#!/bin/bash

export PYTHONPATH=../lib/codar/chimbuko/perf_anom:$PATH:$PYTHONPATH

python3 parser_test.py
