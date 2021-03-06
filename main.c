// Copyright © 2019-2020 Mikel Cazorla Pérez
// This file is part of f7disk,
// licensed under the terms of GPLv2.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "u.h"
#include "f7disk.h"

void show_version();

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
	} else if (strcmp(argv[1], "clear") == 0) {
		f7_clear(argc, argv);
	} else if (strcmp(argv[1], "load") == 0) {
		f7_load(argc, argv);
	} else if (strcmp(argv[1], "tablebrief") == 0) {
		tablebrief(argc, argv);
	} else if (strcmp(argv[1], "brief") == 0) {
		f7_brief(argc, argv);
	} else if (strcmp(argv[1], "reset") == 0) {
		f7_reset(argc, argv);
	} else if (strcmp(argv[1], "override") == 0) {
		f7_override(argc, argv);
	} else if (strcmp(argv[1], "cpboot") == 0) {
		f7_cpboot(argc, argv);
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
		, "Usage: %s <command>"
		"\nUnits: KiB, MiB, GiB, TiB"
		"\nInfo commands: help, version"
		"\nSlot management:"
		"\n\tclear <file> <0-3> <0-15> # Free an active slot."
		"\n\tload <file> <0-3> <0-15> <image> # Write an image to a free slot."
		"\nFor reading:"
		"\n\ttablebrief <file> # Show a brief of the partition table."
		"\n\tbrief <file> <0-3> # Show a brief of the F7h partition."
		"\nFor editing:"
		"\n\treset <file> <0-3> # Free the slots of a F7h partition (soft-reset)."
		"\n\toverride <file> <0-3> ... # Format a existing partition."
		"\n\t\t--slots <1-16> # Number of image slots."
		"\n\t\t[--dry-run] # Does not commit any change."
		"\n\t\t[--first <sector/units>] # (Relative to the partition.)"
		"\n\t\t{"
		"\n\t\t--size <sectors/units> # By default, as much as it can."
		"\n\t\t--every <sectors/units> # It defaults to the slot size."
		"\n\t\t}"
		"\nBootloader:"
		"\n\tcpboot <file> <bootloader> # The signature and the ptable are skipped."
		"\n"
		, name
	);
}

void
show_version()
{
	printf(
		"f7disk v%d.%d.%d"
		" - Copyright © 2019-2020 Mikel Cazorla Pérez.\n"
		"f7disk is released under the GPLv2.\n"
		, ver_x, ver_y, ver_z
	);
}
