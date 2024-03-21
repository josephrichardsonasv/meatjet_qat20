#include "main.h"
#include "cpr.h"

// Global log file descriptor
FILE *g_log_fd;

size_t get_file_size(char *filename)
{
    size_t sz;
    struct stat st;

    stat(filename, &st);
    sz = st.st_size;

    return sz;
}

bool file_exists(char *filename)
{
    struct stat st;

    if (lstat(filename, &st))
    {
        return false;
    }

    return true;
}

int get_num_files(char *dirname)
{
    DIR *dirp;
    struct dirent *dp;
    int num_files;
    struct stat st;
    char fullpath[MAX_FILE_LEN];

    num_files = 0;

    if ((dirp = opendir(dirname)) == NULL)
    {
        MG_LOG_PRINT(g_log_fd, "Error: Coult not open dir %s\n", dirname);
        return 0;
    }

    do
    {
        if ((dp = readdir(dirp)) != NULL)
        {
            strcpy(fullpath, dirname);
            strcat(fullpath, "/");
            strcat(fullpath, dp->d_name);

            // Skip current and parent directories
            if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
            {
                continue;
            }

            stat(fullpath, &st);

            if (S_ISDIR(st.st_mode))
            {
                num_files += get_num_files(fullpath);
            }
            else
            {
                num_files++;
            }

        }
    } while (dp != NULL);

    closedir(dirp);

    return num_files;
}

int populate_file_list(char ***list, char *dirname, int start_idx)
{
    DIR *dirp;
    struct dirent *dp;
    struct stat st;
    char fullpath[MAX_FILE_LEN];
    int idx;

    idx = start_idx;

    if ((dirp = opendir(dirname)) == NULL)
    {
        MG_LOG_PRINT(g_log_fd, "Error: Could not open dir %s\n", dirname);
        return 0;
    }

    do
    {
        if ((dp = readdir(dirp)) != NULL)
        {
            strcpy(fullpath, dirname);
            strcat(fullpath, "/");
            strcat(fullpath, dp->d_name);

            // Skip current and parent directories
            if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
            {
                continue;
            }

            stat(fullpath, &st);

            if (S_ISDIR(st.st_mode))
            {
                 idx = populate_file_list(list, fullpath, idx);
            }
            else
            {
                strcpy((*list)[idx], fullpath);
                idx++;
            }

        }
    } while (dp != NULL);

    closedir(dirp);

    return idx;
}

static void opts_init(struct mg_options *opts)
{
    opts->threads = MAX_THREAD_COUNT;
    opts->min_cpr_lvl = 1;
    opts->max_cpr_lvl = 9;
    opts->obs = 0;
    opts->obs_step = 0;
    opts->underflow = false;
    opts->ibc = 0;
    opts->ibc_step = 0;
    opts->use_dir = false;
    opts->decomp_only = false;
    opts->dynamic_only = false;
    opts->static_only = false;
    opts->debug = false;
    opts->stateless = false;
    strcpy(opts->log, "");
    opts->processes = 1;
    opts->zlibcompare = 0;
}

static char doc[] = "Meatjet!";
static char args_doc[] = "";

static struct argp_option argp_opts[] = {
    {"infile",          'i',    "FILE",    0, "Input file {required, or -d}", 1},
    {"directory",       'd',    "DIR",     0, "Directory to compress {required, or -i}", 1},
    {"log",             0x16,   "LOG",     0, "Log file name (defaults to mg_${parameters}.log)", 1},
    {"threads",         't',    "THDS",    0, "Number of threads", 2},
    {"comp-lvl",        'c',    "COMPLVL", 0, "Compression level", 2},
    {"obs",             'o',    "OBS",     0, "Output buffer size", 3},
    {"obs-step",        0x10,   "OBSSTEP", 0, "Output buffer size, step value b/w ctxs", 3},
    {"underflow",       'u',    NULL,      0, "Underflow test, does not issue overflow contexts", 4},
    {"ibc",             0x14,   "IBC",     0, "Input byte count. Must be in underflow mode", 4},
    {"ibc-step",        0x15,   "IBCSTEP", 0, "Input byte count, step value b/w ctxs (underflow)", 4},
    {"decomp-only",     0x11,   NULL,      0, "Decompression-only", 5},
    {"dynamic-only",    0x12,   NULL,      0, "Dynamic compression only", 5},
    {"static-only",     0x13,   NULL,      0, "Static compression only", 5},
    {"debug",           0x17,   NULL,      0, "Debug mode, Saves good jobs as if they failed",4},
    {"stateless",	's',	NULL,	   0, "Stateless testing, Stateful is the default",5},
    {"processes",       'p',    "N",       0, "Number of Processes", 1},
    {"zlibcompare",	'z',	"zlib",	   0, "Do a Zlib compare on this percent of HW Compressions", 4},	
    {0,0,0,0,0,0}
};

static int parse_opt(int key, char *arg, struct argp_state *state)
{
    struct mg_options *opts = state->input;

    switch (key)
    {
        case 'i':
            strncpy(opts->input_file, arg, MAX_FILE_LEN);
            break;
        case 'd':
            strncpy(opts->dir, arg, MAX_FILE_LEN);
            opts->use_dir = true;
            break;
        case 't':
            opts->threads = atoi(arg);
            if (opts->threads > MAX_THREAD_COUNT) {
                opts->threads = MAX_THREAD_COUNT;
            }
            break;
        case 'o':
            opts->obs = atoi(arg);
            if (opts->obs < MIN_OBS_VALUE) {
                opts->obs = MIN_OBS_VALUE;
            }
            break;
        case 'c':
            opts->min_cpr_lvl = atoi(arg);
            opts->max_cpr_lvl = opts->min_cpr_lvl;
            if (opts->max_cpr_lvl > 9 || opts->min_cpr_lvl < 1) {
                opts->max_cpr_lvl = 9;
                opts->min_cpr_lvl = 1;
            }
            break;
        case 'u':
            opts->underflow = true;
            break;
        case 0x10:
            opts->obs_step = atoi(arg);
            break;
        case 0x11:
            opts->decomp_only = true;
            break;
        case 0x12:
            opts->dynamic_only = true;
            break;
        case 0x13:
            opts->static_only = true;
            break;
        case 0x14:
            opts->ibc = atoi(arg);
            break;
        case 0x15:
            opts->ibc_step = atoi(arg);
            break;
        case 0x16:
            strncpy(opts->log, arg, MAX_FILE_LEN);
            break;
        case 0x17:
            opts->debug = true;
            break;
	case 'z':
	    opts->zlibcompare = atoi(arg);
	    if (opts->zlibcompare > 100)
		    opts->zlibcompare = 100;
	    break;
    	case 's':
	        opts->stateless = true;
            break;
        case ARGP_KEY_ARG:
            break;
        case ARGP_KEY_END:
            break;
        case 'p':
            opts->processes = atoi(arg);
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static int parse_cmdln(int argc, char **argv, struct mg_options *opts)
{
    static struct argp argp = {argp_opts, parse_opt, doc, args_doc, 0, 0, 0};

    return argp_parse(&argp, argc, argv, 0, 0, opts);
}

int main(int argc, char* argv[])
{
    CpaStatus status;
    struct mg_options opts;

    opts_init(&opts);

    parse_cmdln(argc, argv, &opts);

    // Start the logging
    if (strcmp(opts.log, "") == 0)
    {
        sprintf(opts.log, "mg_%s.log", opts.use_dir ? basename(opts.dir) : basename(opts.input_file));
    }

    g_log_fd = fopen(opts.log, "w");
    if (g_log_fd == NULL)
    {
        printf("ERROR: could not create the logfile\n");
        return -1;
    }

    // Make sure we have input files
    if (!file_exists(opts.input_file) && !opts.use_dir)
    {
        MG_LOG_PRINT(g_log_fd, "Error: No input file/directory specified!\n");
        return -1;
    }

    // Make sure we don't choose dyanmic-only AND static-only
    if (opts.dynamic_only && opts.static_only)
    {
        MG_LOG_PRINT(g_log_fd, "Error: Cannot choose static-only AND dynamic-only!\n");
        return -1;
    }

    // If we are in decompression-only mode, we can cheat here a little
    //      - enable static-only
    //      - disable dyanmic-only
    //
    //      We do this so we don't send 2x identical contexts later on (static/dynamic)
    //      and the logic doesn't need to change to support decomp-only
    if (opts.decomp_only)
    {
        MG_LOG_PRINT(g_log_fd, "Info: Decompression-only... static/dynamic support is irrelevant\n");
        opts.min_cpr_lvl = 1;
        opts.max_cpr_lvl = 1;
        opts.static_only = true;
        opts.dynamic_only = false;
    }

    for (uint32_t i = 1 ; i < opts.processes ; i ++ )
    {
        if (!fork())
            break;
    }

    status = cpr_start(&opts);
    if (status != CPA_STATUS_SUCCESS)
    {
        MG_LOG_PRINT(g_log_fd, "\nExecution failed! Logs will provide further detail\n");
    }

    fclose(g_log_fd);

    return status;
}
