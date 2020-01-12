// Copyright © 2019-2020 Mikel Cazorla Pérez
// This file is part of f7disk,
// licensed under the terms of GPLv2.

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "u.h"
#include "f7disk.h"
#include "ptable.h"

static char const *strtype(int type);

void
tablebrief(int argc, char **argv)
{
	if (argc != 3) {
		usage();
		exit(1);
	}

	PartEntry p[4];

	{
		int fd;
		fd = open(argv[2], O_RDONLY);
		if (fd == -1) {
			perror("Cannot open the requested device/image file");
			exit(1);
		}
		if (!read_ptable(fd, p)) {
			close(fd);
			exit(1);
		}
		close(fd);
	}

	printf("# Boot Type %10s %10s %10s Description\n"
		, "Start"
		, "Size"
		, "End"
	);

	for (int entry = 0; entry < 4; ++entry) {
		printf("%d  %02Xh  %02Xh %10lld %10lld"
			, entry
			, p[entry].boot
			, p[entry].type
			, p[entry].start
			, p[entry].size
		);

		if (0 < p[entry].size) {
			printf(
				" %10lld"
				, p[entry].start + (p[entry].size - 1)
			);
		} else {
			printf("%11s", "N/A");
		}

		{
			char const *str = strtype(p[entry].type);
			if (str != nil)
				printf(" %s", str);
		}

		printf("\n");
	}
}

int
read_ptable(int fd, PartEntry *p)
{
	ssize_t n;
	uchar mbr[512];

	if ((off_t)-1 == lseek(fd, 0, SEEK_SET)) {
		perror("Could not seek the file offset");
		return 0;
	}

	n = read(fd, mbr, 512);
	if (n != 512) {
		if (n < 0)
			perror("Cannot read the requested device/image file");
		else
			fprintf(stderr, "Cannot read a whole MBR (%zd byte/s read).\n", n);
		return 0;
	}

	if ((mbr[510] | (mbr[511] << 8)) != 0xAA55) {
		fprintf(stderr, "Magic number (AA55h) not found.\n");
		return 0;
	}

	for (int entry = 0; entry < 4; ++entry) {
		int addr = 0x1BE + entry * 0x10;

		p[entry].boot = mbr[addr++];
		// Ignoring CHS start sector.
		addr += 3;
		p[entry].type = mbr[addr++];
		// Ignoring CHS end sector.
		addr += 3;
		p[entry].start =
			mbr[addr]
			| (((vlong)mbr[addr + 1]) << 8)
			| (((vlong)mbr[addr + 2]) << 16)
			| (((vlong)mbr[addr + 3]) << 24);
		addr += 4;
		p[entry].size =
			mbr[addr]
			| (((vlong)mbr[addr + 1]) << 8)
			| (((vlong)mbr[addr + 2]) << 16)
			| (((vlong)mbr[addr + 3]) << 24);
		// addr += 4;
	}

	// It is not assumed that partitions are ordered.
	// Segmented addresses beyond the greatest LBA are OK.
	// GPT protective MBR partitions are allowed to overlap.
	for (int a = 0; a < (4 - 1); ++a) {
		if (p[a].type == 0x00 || p[a].type == 0xEE)
			continue;

		for (int b = a + 1; b < 4; ++b)
		if (
			p[b].type != 0x00 && p[b].type != 0xEE
			&& p[a].start < p[b].start + p[b].size
			&& p[b].start < p[a].start + p[a].size
		) {
			fprintf(stderr, "Overlapping partitions detected.\n");
			return 0;
		}
	}

	off_t sectors = lseek(fd, 0, SEEK_END);
	if (sectors == (off_t)-1) {
		perror("Could not retrieve the file size");
		return 0;
	}
	sectors /= 512;

	// GPT protective MBR partitions are allowed to exceed the disk size.
	for (int i = 0; i < 4; ++i)
		if (
			p[i].type != 0x00 && p[i].type != 0xEE
			&& sectors < p[i].start + p[i].size
		) {
			fprintf(stderr, "At least one partition is larger than the file.\n");
			return 0;
		}

	return 1;
}

static char const *
strtype(int type)
{
	char const * str;

	switch (type) {
	case 0x00:
		str = "(disabled)";
		break;
	case 0x01:
		str = "FAT-12";
		break;
	case 0x04:
		str = "DOS 3.0 FAT-16 (< 32 MiB)";
		break;
	case 0x05:
		str = "Extended";
		break;
	case 0x06:
		str = "DOS 3.31 FAT-16";
		break;
	case 0x07:
		str = "exFAT/HPFS/NTFS";
		break;
	case 0x0B:
		str = "W95 FAT-32";
		break;
	case 0x0C:
		str = "W95 FAT-32 (LBA)";
		break;
	case 0x0E:
		str = "W95 FAT-16 (LBA)";
		break;
	case 0x0F:
		str = "W95 Extended (LBA)";
		break;
	case 0x82:
		str = "Linux swap";
		break;
	case 0x83:
		str = "Linux";
		break;
	case 0xEE:
		str = "GPT protective MBR";
		break;
	case 0xF7:
		str = "Mark 1";
		break;
	default:
		str = "";
	}

	return str;
}
