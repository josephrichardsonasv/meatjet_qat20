#!/usr/bin/env python2

from os.path import join
import os
import hashlib
import sys


def check_folder(dir):

    with open(join(dir, "source.bin")) as sf:
        source = sf.read()

    with open(join(dir, "compare.bin")) as cf:
        compare = cf.read()

    source_hash = hashlib.md5(source).hexdigest()
    comp_hash = hashlib.md5(compare).hexdigest()

    if source_hash == comp_hash:
        return []

    source = source.encode('hex')
    compare = compare.encode('hex')

    misses = []

    for i in xrange(0, len(source), 2):
        sb = source[i:i+2]
        cb = compare[i:i+2]

        if sb != cb:
            misses.append((i/2, sb, cb))

    return misses


if __name__ == "__main__":
    dirs = [join(sys.argv[1], d)
            for d in os.listdir(sys.argv[1])
            if os.path.isdir(join(sys.argv[1], d))
            and "errlog" in d]

    for d in dirs:
        print "Checking folder {}".format(d)
        results = check_folder(d)

        for error in results:
            print "{:010x}: {}, {}".format(*error)
