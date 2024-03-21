#!/usr/bin/env python2.7

# setup logger
import logging
_log = logging.getLogger("meatjet")
_log.setLevel(logging.DEBUG) #TODO: reduce to info
# define a Handler which writes INFO messages or higher to the sys.stderr
console = logging.StreamHandler()
console.setLevel(logging.INFO) 
_log.addHandler(console)

from time import sleep
import threading
import getopt
import sys
import os, glob
import Queue
import yaml
import session_desc
from collections import OrderedDict
import engine


#
# Counting dictionaries - original job file names will be keys
#
total_job_files = 0
total_completed_job_files = 0
all_jobs_completed = False
sessions_created = OrderedDict()
sessions_completed = OrderedDict()

###############################################################################
# Name:     inc_sessions_created
# Desc:     Callback function for sessions_desc, in order to increment the
#           total number of sessions created for a job file
# Ret:      None
###############################################################################
def inc_sessions_created(job_file):
    global sessions_created
    sessions_created[job_file] += 1

###############################################################################
# Name:     job_done_callback
# Desc:     Callback function for seaside in order to indicate job done
# Ret:      None
###############################################################################
def job_done_callback(job_id):
    global sessions_completed
    global sessions_created
    global all_jobs_completed
    global total_job_files
    global total_completed_job_files

    key = sessions_completed.keys()[job_id]

    if key:
        sessions_completed[key] += 1
        _log.info("[%d] of [%d] jobs complete for %s" % (sessions_completed[key], sessions_created[key], key))
    else:
        _log.info("The associated key for job id %d doesn't exist" % job_id)
        return

    if sessions_completed[key] > sessions_created[key]:
        _log.error("Error: more completed than created!")
    elif sessions_completed[key] == sessions_created[key]:
        total_completed_job_files += 1
        _log.info("Total job_files completed [%d] out of [%d]" % (total_completed_job_files, total_job_files))

    if total_completed_job_files == total_job_files:
        all_jobs_completed = True

###############################################################################
# Name:     parse_jobfile
# Desc:     Parse a jobfile, store as a session descriptor, add to Q, 
#           increment total_job_files if valid
# Ret:      None
###############################################################################
def parse_jobfile(path, sd_queue):
    global total_job_files

    sd = session_desc.session_desc()
    with open(path, 'r') as f:
        doc = yaml.safe_load(f)

    # Stores everything in the job file into session descriptor
    sd = session_desc.session_desc(**doc)
    sd.job_file = path

    if not sd.input_src:
        _log.error("Error: No input source was provided for %s" % path)
        return

    if sd.input_src in "fileset" and not os.path.exists(sd.input_path):
        _log.error("Error: Path [%s] provided in jobfile [%s] does not exist" % (sd.input_src, path))
        return

    # Add job instance!    
    total_job_files += 1
    sd_queue.put(sd)

###############################################################################
# Name:     kickoff_job_parsing 
# Desc:     Entry point for thread parsing job descriptors
# Ret:      None
###############################################################################
def kickoff_job_parsing(job_dir, job_queue, sd_queue):
    while not job_queue.empty():
        # Pull top job off queue
        try:
            job = job_queue.get_nowait()
        except Queue.Empty:
            _log.info("Nothing found in queue")
            return

        fullpath = job_dir + "/" + job
        parse_jobfile(fullpath, sd_queue)

###############################################################################
# Name:     kickoff_sd_passing
# Desc:     Entry point for threads that take session descriptors & send 
#           to c-side queue
###############################################################################
def kickoff_sd_passing(sd_queue, jq_parse_done):
    # Check queue as long as it's not empty and as long as job_done isn't set
    while not jq_parse_done.isSet() or not sd_queue.empty():
        try:
            sd = sd_queue.get_nowait()
        except Queue.Empty:
            jq_parse_done.wait(1)
            continue
       
        # Add instance of this job_file into the dictionaries
        if not sd.job_file in sessions_created:
            sessions_created[sd.job_file] = 0
            sessions_completed[sd.job_file] = 0

        # Store dictionary ID
        dict_id = sessions_created.keys().index(sd.job_file)

        # If input_src is a file/dir
        if sd.input_src in "fileset":
            sd.add_fileset_to_seaside_queue(dict_id, inc_sessions_created)
        elif sd.input_src == "cpr_test_gen.pl":
            # Use Chris' script somehow
            pass
        elif sd.input_src == "dumb_generator":
            pass

    _log.debug("kickoff_sd_passing done")

###############################################################################
# Main function
###############################################################################
def main(argv):
    log_dir = "/root/meat"                     # inital log file name
    job_directory = "./jobs"                    # path to location of job files
    job_queue = Queue.Queue()                   # a queue of all jobX.txt files to parse
    sd_queue = Queue.Queue()                    # a queue pf all session descs that will be passed to QAT
    jq_thread_completed = threading.Event()     # threading event for when all threads parsing jobs finish

    # Parse command line options
    try:
        opts, args = getopt.getopt(argv, "h", ["dir=", "log=", "help"])
    except getopt.GetoptError:
        print 'usage: meatjet.py --dir /path/to/job/files --log /log/meat'
        sys.exit(2)

    # Process flags
    for opt, arg in opts:
        if opt in ('-h', '--help'):
            print 'usage: meatjet.py --dir /path/to/job/files --log /root/meat'
            sys.exit()
        elif opt in "--dir":
            job_directory = arg
        elif opt in "--log":
            log_dir = arg
        else:
            assert False, "unhandled option"

    #setup python file logger 
    filehandler=logging.FileHandler("meatjet_py.log",'w') #create log file
    filehandler.setLevel(logging.DEBUG)
    formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
    filehandler.setFormatter(formatter)
    _log.addHandler(filehandler)

    # Create log directories
    if not os.path.isdir(log_dir):
        _log.debug("Log dir doesn't exist. Creating it now.")
        os.makedirs(log_dir)

    # Initialize callback and threads
    engine.set_cb(job_done_callback)
    engine.init_hw(log_dir)

    # Fill job_queue with all job files in specified directory
    #change to absolute path (rtpike)
    job_directory = os.path.abspath(job_directory)
    os.chdir(job_directory)
    for file in glob.glob('*.yaml'):
        _log.info("processing file %s" % file)
        job_queue.put(file)

    # Thread for Job Queue
    jq_thread = threading.Thread(target=kickoff_job_parsing, args=(job_directory, job_queue, sd_queue))
    jq_thread.start()

    # Thread for Session Descriptor Passing
    sd_thread = threading.Thread(target=kickoff_sd_passing, args=(sd_queue, jq_thread_completed))
    sd_thread.start()

    # Block main thread on job queue parsing, signal when complete
    jq_thread.join()
    jq_thread_completed.set()

    # Block main thread on session desc passing
    sd_thread.join()

    # cajones - should this be another thread where main blocks on it? Probably. Could remove many globals
    count = 0
    while not all_jobs_completed:
        count += 1
        if count % 10000 == 0:
            _log.debug("sleep count: %d" % count)
        sleep(1)

    engine.threads_finish()

# kick off main
if __name__ == "__main__":
    main(sys.argv[1:])
