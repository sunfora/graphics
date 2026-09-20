#!/usr/bin/env bash

(
	. /home/ivan/bin/intel/oneapi/setvars.sh
	guix shell clang-toolchain pkg-config tcc make
	:
)
