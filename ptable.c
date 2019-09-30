#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "f7disk.h"

typedef unsigned char uchar;
typedef long long vlong;

#ifndef nil
	#define nil NULL
#endif

typedef struct {
	int boot;
	int type;
	vlong start;
	vlong size;
} Entry;

static char const *strtype(int type);

void
tablebrief(int argc, char **argv)
{
	if (argc != 3) {
		usage();
		exit(1);
	}

	int fd;
	ssize_t n;
	uchar mbr[512];
	Entry entries[4];

	fd = open(argv[2], O_CLOEXEC | O_RDONLY);
	if (fd == -1) {
		perror("Cannot open the requested device/image file");
		exit(1);
	}

	n = read(fd, mbr, 512);
	if (n != 512) {
		if (n < 0)
			perror("Cannot read the requested device/image file");
		else
			fprintf(stderr, "Cannot read a whole MBR (%zd byte/s read).\n", n);
		exit(1);
	}

	close(fd);

	if ((mbr[510] | (mbr[511] << 8)) != 0xAA55) {
		fprintf(stderr, "Magic number (AA55h) not found.\n");
		exit(1);
	}

	printf("# Boot Type %10s %10s %10s Description\n"
		, "Start"
		, "Size"
		, "End"
	);

	for (int e = 0; e < 4; ++e) {
		int addr = 0x1BE + e * 0x10;

		entries[e].boot = mbr[addr++];
		// Ignoring CHS start sector.
		addr += 3;
		entries[e].type = mbr[addr++];
		// Ignoring CHS end sector.
		addr += 3;
		entries[e].start =
			mbr[addr]
			| (((vlong)mbr[addr + 1]) << 8)
			| (((vlong)mbr[addr + 2]) << 16)
			| (((vlong)mbr[addr + 3]) << 24);
		addr += 4;
		entries[e].size =
			mbr[addr]
			| (((vlong)mbr[addr + 1]) << 8)
			| (((vlong)mbr[addr + 2]) << 16)
			| (((vlong)mbr[addr + 3]) << 24);
		// addr += 4;

		printf("%d  %02Xh  %02Xh %10lld %10lld"
			, e
			, entries[e].boot
			, entries[e].type
			, entries[e].start
			, entries[e].size
		);

		if (0 < entries[e].size) {
			printf(" %10lld", entries[e].start + (entries[e].size - 1));
		} else {
			printf("%11s", "N/A");
		}

		{
			char const *str = strtype(entries[e].type);
			if (str != nil)
				printf(" %s", str);
		}

		printf("\n");
	}
}

static char const *
strtype(int type)
{
	char const * str;

	switch (type) {
	case 0x00:
		str = "(disabled)";
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
		str = nil;
	}

	return str;
}
