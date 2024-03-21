#!/usr/bin/python
import os
import engine
import struct

import logging
_log = logging.getLogger("meatjet")

MAX_INPUT_SIZE = (64 * 1024)

class session_desc:
    def __init__(self, **entries):
        self.input_src = ""
        self.input_path = ""
        self.input_entire_data = ""
        self.input_data = ""
        self.input_offs = [0, 0]
        self.job_file = ""
        self.input_sz = MAX_INPUT_SIZE
        self.__dict__.update(entries)

        if self.input_sz > MAX_INPUT_SIZE:
            _log.info("Requested size is greater than max allowable size")
            self.input_sz = MAX_INPUT_SIZE

    ###########################################################################
    # @func     read_entire_data
    # @desc     Reads an entire file into memory, regardless of size. This is
    #           stored in self.input_entire_data
    #
    # @ret      None
    ###########################################################################
    def read_entire_data(self, input_file):
        if not input_file:
            return

        f = open(input_file, "rb")
        self.input_entire_data = f.read()
        f.close()

    ###########################################################################
    # @func     create_input_file_list
    # @desc     Creates a list of input files if input_src is a file or dir
    #
    # @ret      Returns the final list of input files
    ###########################################################################
    def create_input_file_list(self):
        list = []
        if not self.input_path:
            return []

        # If file, return file name, otherwise add all files in dir
        if os.path.isfile(self.input_path):
            list.append(self.input_path)
        elif os.path.isdir(self.input_path):
            for filename in os.listdir(self.input_path):
                if not os.path.isdir(filename):
                    list.append(self.input_path + "/" + filename)
        else:
            _log.error("Error. Unknown source")
            return []

        return list

    ###########################################################################
    # @func     add_fileset_to_seaside_queue
    # @desc     Adds a new session/ctx to the seaside queue
    #
    # @param    dict_id     ID of the job_file key in sessions dictionaries
    #
    # @ret      None
    ###########################################################################
    def add_fileset_to_seaside_queue(self, dict_id, inc_sessions_cb):
        file_list = []
        file_list = self.create_input_file_list()

        if not file_list:
            _log.error("Error: Could not generate a file list!")
            return

        for infile in file_list:
            self.read_entire_data(infile)        # Read into self.input_entire_data
            entire_len = len(self.input_entire_data)
            offs = self.input_offs[0]

            _log.info("%s (%d)" % (infile, entire_len))

            # Check if it is even possible to send requested input size
            if entire_len <= self.input_sz:
                _log.info("Size of [%d] is smaller than requested input size" % entire_len)
                self.input_data = self.input_entire_data
                inc_sessions_cb(self.job_file)
                self.add_to_seaside_queue(dict_id)
                continue

            # Check if the offsets are incrementing
            if len(self.input_offs) < 2 or self.input_offs[1] == 0:
                _log.info("Sliding window not requested -- Sending single job")
                self.input_data = self.input_entire_data[offs:offs + self.input_sz]
                inc_sessions_cb(self.job_file)
                self.add_to_seaside_queue(dict_id)
                continue
                
            # Now run through each offset
            # This grabs slices of the entire data [offset : input_sz+offset]
            inc = self.input_offs[1]

            while (offs + self.input_sz) <= entire_len:
                _log.info("\tWindow: [%d] to [%d]" % (offs, (offs + self.input_sz)))
                self.input_data = self.input_entire_data[offs:(offs + self.input_sz)]
                inc_sessions_cb(self.job_file)
                self.add_to_seaside_queue(dict_id)
                offs += inc
            

    ###########################################################################
    # @func     add_to_seaside_queue
    # @desc     Adds a new session/ctx to the seaside queue
    #
    # @param    dict_id     ID of the job_file key in sessions dictionaries
    #
    # @ret      None
    ###########################################################################
    def add_to_seaside_queue(self, dict_id):
        # Fill context with required information
        ctx = engine.get_context()
        engine.fill_context(ctx, 
                            bytearray(self.input_data),
                            len(self.input_data),
                            dict_id,
                            self.direction, 
                            self.complevel, 
                            self.hufftype, 
                            self.state, 
                            self.deflateWinSize, 
                            self.starvation,
                            self.job_file)

        # Set buffer information: [start, end, step]
        engine.set_dst_buff(ctx, self.buffers[0], self.buffers[1], self.buffers[2])

        # Enqueue context!
        engine.enc_context(ctx)



        


