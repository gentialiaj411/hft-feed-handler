#!/usr/bin/env python3
import zipfile
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/mdp3_incr.zip"
with zipfile.ZipFile(path) as z:
    names = z.namelist()
    print("files", len(names))
    for n in names[:30]:
        print(n)
