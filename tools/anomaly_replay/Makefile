# tools/anomaly_replay — host replay harness (arch §10.2/§10.5)
#   make replay   — full injection suite + metrics to out/
#   make sweep    — K/H sensitivity sweep (§10.3)
#   make parity   — Python mirror vs compiled anomaly_core.h
#   make coretest — native g++ unit tests for the core header

CORE_DIR = ../../firmware/common

replay:
	python3 replay.py --inject all --report out/

sweep:
	python3 replay.py --sweep --inject stress,external_event --report out/

parity:
	python3 test_parity.py

coretest:
	g++ -std=c++17 -Wall -Wextra -I$(CORE_DIR) ../../firmware/test/test_core.cpp -o /tmp/opencode/test_core && /tmp/opencode/test_core

cpp:
	g++ -std=c++17 -Wall -O2 -I$(CORE_DIR) cpp_replay.cpp -o cpp_replay

all: coretest parity replay
