#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "f7disk.h"

#ifndef nil
	#define nil NULL
#endif

void show_version();
static void not_implemented();

char const *name = "#?";

int
main(int argc, char **argv)
{
	if (argc < 1) {
		fprintf(stderr, "No arguments provided.\n");
		exit(1);
	}

	if (argv[0] != nil && strcmp(argv[0], "") != 0) {
		name = argv[0];
	}

	if (argc < 2) {
		usage();
		exit(1);
	}

	if (argc == 2) {
		if (strcmp(argv[1], "version") == 0) {
			show_version();
		} else {
			int help_mode = strcmp(argv[1], "help") == 0;
			usage();
			if (!help_mode)
				exit(1);
		}
	} else if (strcmp(argv[1], "tablebrief") == 0) {
		tablebrief(argc, argv);
	} else if (strcmp(argv[1], "brief") == 0) {
		//	show_f7_part_brief(file, part);
		not_implemented();
	} else if (strcmp(argv[1], "override") == 0) {
		//	transform_into_f7_part(file, part, n, slotsize, offset, gap);
		not_implemented();
	} else if (strcmp(argv[1], "reset") == 0) {
		//	reset_f7_part(file, part);
		not_implemented();
	} else {
		usage();
		exit(1);
	}
}

void
usage()
{
	fprintf(
		stderr
		, "usage: %s <command>"
		"\nunits: KiB, MiB, GiB, TiB"
		"\ninfo commands: help, version"
		"\ncommands for reading:"
		"\n\ttablebrief <file> # Show a brief of the partition table."
		"\n\tbrief [0-3] <file> # Show a brief of the F7h partition."
		"\ncommands for editing:"
		"\n\toverride <0-3> <file> # Format a existing partition."
		"\n\t\t--start <sector/units> # (Relative to the partition.)"
		"\n\t\t[--gap <sectors/units>] # (Between slots.) It defaults to 0."
		"\n\t\t--n <1-16> # Number of image slots."
		"\n\t\t[--slotsize <sectors/units>] # By default, as much as it can."
		"\n\treset <0-3> <file> # Free the slots of a F7h partition (soft-reset)."
		"\n"
		, name
	);
}

void
show_version()
{
	printf(
		"f7disk v%d.%d.%d"
		" - Copyright © 2019 Mikel Cazorla Pérez, All Rights Reserved.\n"
		, ver_x, ver_y, ver_z
	);
}

static void
not_implemented()
{
	fprintf(stderr, "Not implemented\n");
	exit(1);
}
