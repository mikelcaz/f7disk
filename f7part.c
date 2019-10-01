#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "u.h"
#include "f7disk.h"
#include "ptable.h"

typedef enum {
	UNKNOWN = 0x0,
	DRYRUN = 0x1,
	NSLOTS = 0x2,
	FIRST = 0x4,
	SIZE = 0x8,
	EVERY = 0x16,
} Options;

#define LBA_MAX (4LL * 1024 * 1024 * 1024 - 1) // (a.k.a. 2^32 - 1).
#define DIST_MAX (32LL * 1024 * 2 - 1) // (a.k.a. 2^16 - 1).

static vlong atolba(char *str);
static long atol2(char *str);
static void shortensectors(vlong sectors, vlong *n, int *unit);
static char const *strunit(int unit);

void
f7_brief(int argc, char **argv)
{
	if (argc != 4) {
		usage();
		exit(1);
	}
}

void
f7_override(int argc, char **argv)
{
	int fd;
	int entry;
	PartEntry p[4];
	vlong partsize;

	int options = 0;
	int n;
	vlong first;
	vlong size;
	vlong every;

	entry = atol2(argv[3]);
	if (entry < 0 || 3 < entry) {
		usage();
		exit(1);
	}

	for (int i = 4; i < argc; i += 1) {
		int o;

		if (strcmp(argv[i], "--dry-run") == 0) {
			o = DRYRUN;
		} else if (argc <= i + 1) {
			o = UNKNOWN;
		} else if (strcmp(argv[i], "--slots") == 0) {
			long ln;
			o = NSLOTS;

			ln = atol2(argv[i + 1]);
			if (ln < 1 || 16 < ln) {
				usage();
				exit(1);
			}
			n = ln;
		} else if (strcmp(argv[i], "--first") == 0) {
			o = FIRST;
			first = atolba(argv[i + 1]);
		} else if (strcmp(argv[i], "--size") == 0) {
			o = SIZE;
			size = atolba(argv[i + 1]);
		} else if (strcmp(argv[i], "--every") == 0) {
			o = EVERY;
			every = atolba(argv[i + 1]);
		} else {
			o = UNKNOWN;
		}

		if (
			o == UNKNOWN
			|| (options & o) != 0
		) {
			usage();
			exit(1);
		}
		if (o != DRYRUN)
			i += 1;

		options |= o;
	}

	if ((options & FIRST) == 0) {
		first = 1;
	}
	if ((options & NSLOTS) == 0) {
		usage();
		exit(1);
	}

	fd = open(argv[2], O_CLOEXEC | O_RDONLY);
	if (fd == -1) {
		perror("Cannot open the requested device/image file");
		exit(1);
	}

	if (!read_ptable(fd, p)) {
		close(fd);
		exit(1);
	}

	do {
		if (p[entry].type == 0x00) {
			fprintf(stderr, "A disabled partition cannot be overridden.\n");
		} else if (p[entry].type == 0xEE) {
			fprintf(stderr, "GPT protective MBR partitions cannot be overriden.\n");
		} else if (
			p[entry].start < 0 || LBA_MAX < p[entry].start
			|| p[entry].size < 0 || LBA_MAX < p[entry].size
		) {
			fprintf(
				stderr
				, "BUG: The partition bounds are impossible!\n"
			);
		} else if (p[entry].start == 0) {
			fprintf(
				stderr
				, "The partition starts at LBA 0, overlapping the MBR.\n"
			);
		} else if (LBA_MAX - p[entry].start < p[entry].size) {
			fprintf(
				stderr
				, "The partition bounds are beyond the addressable limit (2 TiB).\n"
			);
		} else {
			break;
		}

		close(fd);
		exit(1);
	} while (0);

	partsize = p[entry].size;
	do {
		if (partsize < 1) {
			fprintf(
				stderr
				, "At least one sector is required for the F7h header.\n"
			);
		} else if (partsize < first) {
			fprintf(
				stderr
				, "The first slot would start beyond the partition bounds.\n"
			);
		} else {
			break;
		}

		close(fd);
		exit(1);
	} while(0);
	partsize -= first;

	if ((options & (SIZE | EVERY)) == EVERY) {
		size = every;
	} else if ((options & (SIZE | EVERY)) != (SIZE | EVERY)) {
		if ((options & SIZE) == 0)
			size = partsize / n;
		every = size;
	}

	do {
		if (every < size)
			fprintf(
				stderr
				, "'Every' cannot be less than 'size'.\n"
			);
		else if (size < LBA_MAX - DIST_MAX && size + DIST_MAX < every)
			fprintf(
				stderr
				, "'Every' cannot be greater than 'size' by more than 32 MiB.\n"
			);
		else if (partsize < n * every)
			fprintf(
				stderr
				, "The partition is too small for %d slot/s.\n"
				, n
			);
		else
			break;

		close(fd);
		exit(1);
	} while (0);

	if ((options & DRYRUN) != 0) {
		vlong v;
		int unit;

		printf("Slots = %d\n", n);
		shortensectors(first, &v, &unit);
		printf("First = +%lld%s\n", v, strunit(unit));
		shortensectors(size, &v, &unit);
		printf("Size = %lld%s\n", v, strunit(unit));
		shortensectors(every, &v, &unit);
		printf("Every = %lld%s\n", v, strunit(unit));
		shortensectors(0, &v, &unit);

		close(fd);
		exit(0);
	}

	close(fd);
}

static vlong
atolba(char *str)
{
	vlong max_unit;
	vlong lba;
	char *endptr;
	size_t len;
	int powered;

	len = strlen(str);

	if (3 < len && strcmp(&str[len - 2], "iB") == 0) {
		switch(str[len - 3]) {
		case 'K':
			powered = 1;
			break;
		case 'M':
			powered = 2;
			break;
		case 'G':
			powered = 3;
			break;
		case 'T':
			powered = 4;
			break;
		default:
			usage();
			exit(1);
		}
		len -= 3;

		max_unit = 2;
		for (int i = powered; i < 4; ++i)
			max_unit *= 1024;
	} else {
		powered = 0;
		max_unit = LBA_MAX;
	}

	errno = 0;
	lba = strtoll(str, &endptr, 10);
	do {
		if (errno != 0) {
			perror("Error parsing an address");
		} else if (
			endptr != &str[len]
			|| lba < 1
		) {
			// TODO: detect _actual_ zeros/negatives
			// and display a better message.
			usage();
		} else if (max_unit < lba) {
			fprintf(
				stderr
				, "Addresses are limited to 2^32 sectors"
				" (assuming 512B sectors, so 2 TiB).\n"
			);
		} else {
			break;
		}
		exit(1);
	} while (0);

	for (int i = 1; i < powered; ++i)
		lba *= 1024;
	if (0 < powered)
		lba *= (1024 / 512);

	return lba;
}

static long
atol2(char *str)
{
	long n;
	char *endptr;

	errno = 0;
	n = strtol(str, &endptr, 10);

	do {
		if (errno != 0)
			perror("Error parsing numeric argument");
		else if (endptr != &str[strlen(str)])
			fprintf(stderr, "Error parsing numeric argument (%s)\n", str);
		else
			break;
		exit(1);
	} while (0);

	return n;
}

static void
shortensectors(vlong sectors, vlong *n, int *unit)
{
	*n = sectors;
	*unit = 0;

	if (*n == 0)
		return;

	if (*n % 2 == 0) {
		*n /= 2;
		++*unit;
	}

	for (; *unit < 4 && *n != 0 && *n % 1024 == 0; ++*unit)
		*n /= 1024;
}

static char const *
strunit(int unit)
{
	char const * str;

	switch (unit) {
	case 1:
		str = " KiB";
		break;
	case 2:
		str = " MiB";
		break;
	case 3:
		str = " GiB";
		break;
	case 4:
		str = " TiB";
		break;
	default:
		str = "";
	}

	return str;
}
