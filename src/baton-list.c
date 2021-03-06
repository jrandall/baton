/**
 * Copyright (C) 2013, 2014, 2015 Genome Research Ltd. All rights
 * reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @author Keith James <kdj@sanger.ac.uk>
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include <jansson.h>

#include "config.h"
#include "baton.h"
#include "json.h"
#include "log.h"

static int acl_flag        = 0;
static int avu_flag        = 0;
static int checksum_flag   = 0;
static int contents_flag   = 0;
static int debug_flag      = 0;
static int help_flag       = 0;
static int replicate_flag  = 0;
static int silent_flag     = 0;
static int size_flag       = 0;
static int timestamp_flag  = 0;
static int unbuffered_flag = 0;
static int unsafe_flag     = 0;
static int verbose_flag    = 0;
static int version_flag    = 0;

int do_list_paths(FILE *input, option_flags oflags);

int main(int argc, char *argv[]) {
    option_flags oflags = 0;
    int exit_status = 0;
    char *json_file = NULL;
    FILE *input     = NULL;

    while (1) {
        static struct option long_options[] = {
            // Flag options
            {"acl",        no_argument, &acl_flag,        1},
            {"avu",        no_argument, &avu_flag,        1},
            {"checksum",   no_argument, &checksum_flag,   1},
            {"contents",   no_argument, &contents_flag,   1},
            {"debug",      no_argument, &debug_flag,      1},
            {"help",       no_argument, &help_flag,       1},
            {"replicate",  no_argument, &replicate_flag,  1},
            {"silent",     no_argument, &silent_flag,     1},
            {"size",       no_argument, &size_flag,       1},
            {"timestamp",  no_argument, &timestamp_flag,  1},
            {"unbuffered", no_argument, &unbuffered_flag, 1},
            {"unsafe",     no_argument, &unsafe_flag,     1},
            {"verbose",    no_argument, &verbose_flag,    1},
            {"version",    no_argument, &version_flag,    1},
            // Indexed options
            {"file",      required_argument, NULL, 'f'},
            {0, 0, 0, 0}
        };

        int option_index = 0;
        int c = getopt_long_only(argc, argv, "f:",
                                 long_options, &option_index);

        /* Detect the end of the options. */
        if (c == -1) break;

        switch (c) {
            case 'f':
                json_file = optarg;
                break;

            case '?':
                // getopt_long already printed an error message
                break;

            default:
                // Ignore
                break;
        }
    }

    if (acl_flag)       oflags = oflags | PRINT_ACL;
    if (avu_flag)       oflags = oflags | PRINT_AVU;
    if (checksum_flag)  oflags = oflags | PRINT_CHECKSUM;
    if (contents_flag)  oflags = oflags | PRINT_CONTENTS;
    if (replicate_flag) oflags = oflags | PRINT_REPLICATE;
    if (size_flag)      oflags = oflags | PRINT_SIZE;
    if (timestamp_flag) oflags = oflags | PRINT_TIMESTAMP;
    if (unsafe_flag)    oflags = oflags | UNSAFE_RESOLVE;

    const char *help =
        "Name\n"
        "    baton-list\n"
        "\n"
        "Synopsis\n"
        "\n"
        "    baton-list [--acl] [--avu] [--checksum] [--contents]\n"
        "               [--file <JSON file>]\n"
        "               [--replicate] [--silent] [--size]\n"
        "               [--timestamp] [--unbuffered] [--unsafe]\n"
        "               [--verbose] [--version]\n"
        "\n"
        "Description\n"
        "    Lists data objects and collections described in a JSON\n"
        "    input file.\n"
        "\n"
        "    --acl         Print access control lists in output.\n"
        "    --avu         Print AVU lists in output.\n"
        "    --checksum    Print data object checksums in output.\n"
        "    --contents    Print collection contents in output.\n"
        "    --file        The JSON file describing the data objects and\n"
        "                  collections. Optional, defaults to STDIN.\n"
        "    --replicate   Print data object replicates.\n"
        "    --silent      Silence warning messages.\n"
        "    --size        Print data object sizes in output.\n"
        "    --timestamp   Print timestamps in output.\n"
        "    --unbuffered  Flush print operations for each JSON object.\n"
        "    --unsafe      Permit unsafe relative iRODS paths.\n"
        "    --verbose     Print verbose messages to STDERR.\n"
        "    --version     Print the version number and exit.\n";

    if (help_flag) {
        printf("%s\n",help);
        exit(0);
    }

    if (version_flag) {
        printf("%s\n", VERSION);
        exit(0);
    }

    if (debug_flag)   set_log_threshold(DEBUG);
    if (verbose_flag) set_log_threshold(NOTICE);
    if (silent_flag)  set_log_threshold(FATAL);

    declare_client_name(argv[0]);
    input = maybe_stdin(json_file);

    int status = do_list_paths(input, oflags);
    if (status != 0) exit_status = 5;

    exit(exit_status);
}

int do_list_paths(FILE *input, option_flags oflags) {
    int item_count  = 0;
    int error_count = 0;

    rodsEnv env;
    rcComm_t *conn = rods_login(&env);
    if (!conn) goto error;

    while (!feof(input)) {
        size_t jflags = JSON_DISABLE_EOF_CHECK | JSON_REJECT_DUPLICATES;
        json_error_t load_error;
        json_t *target = json_loadf(input, jflags, &load_error);
        if (!target) {
            if (!feof(input)) {
                logmsg(ERROR, "JSON error at line %d, column %d: %s",
                       load_error.line, load_error.column, load_error.text);
            }

            continue;
        }

        item_count++;
        if (!json_is_object(target)) {
            logmsg(ERROR, "Item %d in stream was not a JSON object; skipping",
                   item_count);
            error_count++;
            json_decref(target);
            continue;
        }

        baton_error_t path_error;
        char *path = json_to_path(target, &path_error);

        if (add_error_report(target, &path_error)) {
            error_count++;
            print_json(target);
        }
        else {
            rodsPath_t rods_path;
            resolve_rods_path(conn, &env, &rods_path, path, oflags,
                              &path_error);
            if (add_error_report(target, &path_error)) {
                error_count++;
                print_json(target);
            }
            else {
                baton_error_t error;
                json_t *results = list_path(conn, &rods_path, oflags, &error);

                if (add_error_report(target, &error)) {
                    error_count++;
                    print_json(target);
                }
                else {
                    print_json(results);
                    json_decref(results);
                }

                if (rods_path.rodsObjStat) free(rods_path.rodsObjStat);
            }
        }

        if (unbuffered_flag) fflush(stdout);

        json_decref(target);
        if (path) free(path);
    } // while

    rcDisconnect(conn);

    if (error_count > 0) {
        logmsg(WARN, "Processed %d items with %d errors",
               item_count, error_count);
    }
    else {
        logmsg(DEBUG, "Processed %d items with %d errors",
               item_count, error_count);
    }

    return error_count;

error:
    if (conn) rcDisconnect(conn);

    logmsg(ERROR, "Processed %d items with %d errors", item_count, error_count);

    return 1;
}
