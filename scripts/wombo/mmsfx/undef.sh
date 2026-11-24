#!/bin/bash
# undef.sh <build dir>: list libsoh/lib2ship dynamic undefined symbols not defined by any built .so (glibc etc. excluded by @version)
B=$1
OUT=/tmp/scratch/mmsfx
find "$B" -name "*.so" -not -path "*/_deps/*" > $OUT/libs.txt
cat $OUT/libs.txt
: > $OUT/defs.txt
while read -r l; do nm -D --defined-only "$l" | awk '{print $3}' >> $OUT/defs.txt; done < $OUT/libs.txt
sort -u -o $OUT/defs.txt $OUT/defs.txt
for lib in soh/libsoh.so mm/lib2ship.so; do
  echo "== $lib"
  nm -D --undefined-only "$B/$lib" | awk '{print $2}' | grep -v "@" | sort -u | comm -23 - $OUT/defs.txt
done
