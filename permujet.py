#!/usr/bin/env python2

import argparse
import distutils.spawn as spawn
import os
import itertools
import tempfile
import subprocess
from collections import OrderedDict

parser = argparse.ArgumentParser()
parser.add_argument("input_files", metavar="FILE", type=str, nargs="+")
parser.add_argument("-s", "--start", metavar="N", type=int)
parser.add_argument("-n", "--num-perms", metavar="N", type=int)
parser.add_argument("-m", "--meat", metavar="MEAT_ELF", type=str)
parser.add_argument("-c", "--meat-config", metavar="CONFIG", type=str)
parser.add_argument("-r", "--runs", metavar="N", type=int)

args = parser.parse_args()

input_files = []
for file in args.input_files:
    if os.stat(file).st_size > 0:
        input_files.append(file)

input_files.sort()
print input_files

start = min(len(input_files), args.start)
end = min(len(input_files), start + args.num_perms)

print start
print end

meat_place = None
if args.meat is None:
    meat_place = os.getenv("MEAT_EXE")
if meat_place is None:
    meat_place = spawn.find_executable("meatjet")

if meat_place is None:
    raise EnvironmentError("Could not find meatjet!")

# Load the files

file_data = OrderedDict()

for filename in input_files:
    with open(filename, "rb") as f:
        file_data[filename] = f.read()

for r in xrange(start, end + 1):
    for c in itertools.combinations(file_data.keys(), r):
        combi_data = "".join(file_data[f] for f in c)

        print "Trying combination {}".format("+".join(c))

        with tempfile.NamedTemporaryFile() as tmp:
            tmp.write(combi_data)
            tmp.flush()

            for i in xrange(0, args.runs):
                print "\tRun {}".format(i)
                # Execute meatjet
                try:
                    output = subprocess.check_output([meat_place] + args.meat_config.split() + ["--infile={}".format(tmp.name)])
                    if "DATA COMPARE ERROR" in output:
                        raise ValueError("")
                except subprocess.CalledProcessError as e:
                    print e.output
                    if "DATA COMPARE ERROR" in e.output:
                        with open("failing_file.infl", "wb+") as f:
                            f.write(combi_data)

                        print "Found failure. Exiting..."
                        exit(0)
