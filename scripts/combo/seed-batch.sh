#!/bin/bash
# scripts/combo/seed-batch.sh <build/combo dir> [seed-name] [count] — generate a batch with comborando (each seed is
# generated and its playthrough judged); exits non-zero on any unbeatable seed. Run after any logic change.
set -e
DIR=${1:?build combo dir}; NAME=${2:-batch}; COUNT=${3:-8}
cd "$DIR"
export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH
./comborando --seed "$NAME" --count "$COUNT" | tee /tmp/seed-batch.log
grep -q "RESULT: PASS" /tmp/seed-batch.log
