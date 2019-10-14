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
	SLOTS = 0x2,
	FIRST = 0x4,
	SIZE = 0x8,
	EVERY = 0x10,
} Options;

typedef struct {
	int count;
	uint bitmap;
	vlong first;
	vlong size;
	vlong every;
} MetaF7;

#define LBA_MAX (4LL * 1024 * 1024 * 1024 - 1) // (a.k.a. 2^32 - 1).
#define DIST_MAX (32LL * 1024 * 2 - 1) // (a.k.a. 2^16 - 1).

static int f7_read_header(
	int fd
	, PartEntry const *p
	, int entry
	, uchar *header
);
static int f7_retrieve_meta(uchar *header, MetaF7 *meta);
// Do not change multiple bits at the same time
// (the reset command is an exception).
static int f7_write_bitmap(
	int fd
	, PartEntry const *p
	, int entry
	, uint bitmap
);
static vlong atolba(char *str);
static long atol2(char *str);
static void shortensectors(vlong sectors, vlong *n, int *unit);
static char const *strunit(int unit);

void
f7_clear(int argc, char **argv)
{
	int fd;
	int entry;
	int slot;
	PartEntry p[4];
	uchar header[24];
	MetaF7 meta;
	uint bitmap;

	if (argc != 5) {
		usage();
		exit(1);
	}

	entry = atol2(argv[3]);
	slot = atol2(argv[4]);
	if (
		entry < 0 || 3 < entry
		|| slot < 0 || 15 < slot
	) {
		usage();
		exit(1);
	}

	fd = open(argv[2], O_CLOEXEC | O_RDWR);
	if (fd == -1) {
		perror("Cannot open the requested device/image file");
		exit(1);
	}

	if (
		!read_ptable(fd, p)
		|| !f7_read_header(fd, p, entry, header)
		|| !f7_retrieve_meta(header, &meta)
	) {
		close(fd);
		exit(1);
	}

	bitmap = 0x1;
	for (int i = 0; i < slot; ++i)
		bitmap = bitmap << 1;
	bitmap = ~bitmap & meta.bitmap & 0xFFFF;

	do {
		if (meta.count <= slot)
			fprintf(stderr, "There is only %d slots.\n", meta.count);
		else if (bitmap == meta.bitmap)
			fprintf(stderr, "The slot #%d was already cleared.\n", slot);
		else
			break;

		close(fd);
		exit(1);
	} while (0);

	if (!f7_write_bitmap(fd, p, entry, bitmap)) {
		close(fd);
		exit(1);
	}

	close(fd);
}

void
f7_load(int argc, char **argv)
{
	// This code assumes that LBA_MAX fits in the off_t type.
	// Also, it assumes that the max off_t value fits in the size_t type.

	off_t size, reqsectors;
	int fd[2];
	int entry;
	int slot;
	PartEntry p[4];
	uchar header[24];
	MetaF7 meta;
	uint bitmap;

	if (argc != 6) {
		usage();
		exit(1);
	}

	entry = atol2(argv[3]);
	slot = atol2(argv[4]);
	if (
		entry < 0 || 3 < entry
		|| slot < 0 || 15 < slot
	) {
		usage();
		exit(1);
	}

	fd[0] = open(argv[2], O_CLOEXEC | O_RDWR);
	if (fd[0] == -1) {
		perror("Cannot open the requested device/image file");
		exit(1);
	}
	fd[1] = open(argv[5], O_CLOEXEC | O_RDONLY);
	if (fd[1] == -1) {
		perror("Cannot open the requested device/image file");
		close(fd[0]);
		exit(1);
	}

	if (
		!read_ptable(fd[0], p)
		|| !f7_read_header(fd[0], p, entry, header)
		|| !f7_retrieve_meta(header, &meta)
	) {
		close(fd[1]);
		close(fd[0]);
		exit(1);
	}

	bitmap = 0x1;
	for (int i = 0; i < slot; ++i)
		bitmap = bitmap << 1;
	bitmap = (bitmap | meta.bitmap) & 0xFFFF;

	do {
		if (meta.count <= slot)
			fprintf(stderr, "There is only %d slots.\n", meta.count);
		else if (bitmap == meta.bitmap)
			fprintf(stderr, "The slot #%d was already active.\n", slot);
		else if ((size = lseek(fd[1], 0, SEEK_END)) == (off_t)-1)
			perror("Could not retrieve the payload file size");
		else if ((off_t)-1 == lseek(fd[1], 0, SEEK_SET))
			perror("Could not seek the payload file offset");
		else
			break;

		close(fd[1]);
		close(fd[0]);
		exit(1);
	} while (0);

	reqsectors = size / 512 + (size % 512 != 0? 1: 0);
	if (meta.size < reqsectors) {
		fprintf(
			stderr
			, "The number of sectors to load exceed the slot capacity (%jd > %lld).\n"
			, reqsectors
			, meta.size
		);

		close(fd[1]);
		close(fd[0]);
		exit(1);
	}

	{
		ssize_t n;
		size_t count;
		off_t rem;
		struct stat statbuf;
		uchar *buf;

		do {
			if (fstat(fd[0], &statbuf) < 0)
				perror("Could not use stat over the file");
			else if ((off_t)-1 == lseek(fd[0], (p[entry].start + meta.first + slot * meta.every) * 512, SEEK_SET))
				perror("Could not seek the file offset");
			else if ((buf = (uchar *)malloc(statbuf.st_blksize)) == nil)
				fprintf(stderr, "Could not allocate the copy buffer.\n");
			else
				break;

			close(fd[1]);
			close(fd[0]);
			exit(1);
		} while (0);

		rem = size;
		while (0 < rem) {
			if (statbuf.st_blksize < rem)
				count = statbuf.st_blksize;
			else
				count = rem;

			n = read(fd[1], buf, count);
			if ((size_t)n == count) {
				n = write(fd[0], buf, count);
				if (0 < n)
					rem -= n;
			}

			do {
				if (n < 0)
					perror("Could not copy the payload");
				else if ((size_t)n < count)
					fprintf(stderr, "Could not copy the payload.\n");
				else
					break;

				if (rem < size)
					fprintf(
						stderr
						, "WARNING: %jd/%jd bytes were actually copied.\n"
						, rem
						, size
					);

				free(buf);
				close(fd[1]);
				close(fd[0]);
				exit(1);
			} while(0);
		}

		free(buf);
	}

	if (!f7_write_bitmap(fd[0], p, entry, bitmap)) {
		close(fd[1]);
		close(fd[0]);
		exit(1);
	}

	close(fd[1]);
	close(fd[0]);
}

void
f7_brief(int argc, char **argv)
{
	int fd;
	int entry;
	PartEntry p[4];
	uchar header[24];
	MetaF7 meta;

	if (argc != 4) {
		usage();
		exit(1);
	}

	entry = atol2(argv[3]);
	if (entry < 0 || 3 < entry) {
		usage();
		exit(1);
	}

	fd = open(argv[2], O_CLOEXEC | O_RDONLY);
	if (fd == -1) {
		perror("Cannot open the requested device/image file");
		exit(1);
	}

	if (
		!read_ptable(fd, p)
		|| !f7_read_header(fd, p, entry, header)
	) {
		close(fd);
		exit(1);
	}
	close(fd);

	if (!f7_retrieve_meta(header, &meta))
		exit(1);

	{
		int i;
		vlong v;
		int unit;
		int active;

		active = 0;
		for (i = 0; i < meta.count; ++i)
			if (meta.bitmap >> i & 0x1)
				++active;
		for (; i < 16; ++i)
			if (meta.bitmap >> i & 0x1)
				fprintf(stderr, "WARNING: Slot bit #%d set, but should be unused.\n", i);

		printf("Active slots = %d/%d\n", active, meta.count);
		printf("Bitmap = %04X\n", meta.bitmap);
		shortensectors(meta.first, &v, &unit);
		printf("First = +%lld%s\n", v, strunit(unit));
		shortensectors(meta.size, &v, &unit);
		printf("Size = %lld%s\n", v, strunit(unit));
		shortensectors(meta.every, &v, &unit);
		printf("Every = %lld%s\n", v, strunit(unit));
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
	int count;
	vlong first;
	vlong size;
	vlong every;

	if (argc < 4) {
		usage();
		exit(1);
	}

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
			long lcount;
			o = SLOTS;

			lcount = atol2(argv[i + 1]);
			if (lcount < 1 || 16 < lcount) {
				usage();
				exit(1);
			}
			count = lcount;
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
	if ((options & SLOTS) == 0) {
		usage();
		exit(1);
	}

	fd = open(argv[2], O_CLOEXEC | O_RDWR);
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
			size = partsize / count;
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
				, "'Every' cannot be greater than 'size' by 32 MiB or more.\n"
			);
		else if (partsize < (count - 1) * every + size)
			fprintf(
				stderr
				, "The partition is too small for %d slot/s.\n"
				, count
			);
		else
			break;

		close(fd);
		exit(1);
	} while (0);

	if ((options & DRYRUN) != 0) {
		vlong v;
		int unit;

		printf("Slots = %d\n", count);
		shortensectors(first, &v, &unit);
		printf("First = +%lld%s\n", v, strunit(unit));
		shortensectors(size, &v, &unit);
		printf("Size = %lld%s\n", v, strunit(unit));
		shortensectors(every, &v, &unit);
		printf("Every = %lld%s\n", v, strunit(unit));

		close(fd);
		exit(0);
	}

	{
		// This code assumes that LBA_MAX fits in the off_t type.

		int i;
		uchar header[24];
		vlong padding = every - size;
		ssize_t n;
		uchar const type = 0xF7;

		i = 0;
		header[i++] = 0xF7; // Type
		header[i++] = 0x00; // Version
		header[i++] = 'S';
		header[i++] = 'Y';
		header[i++] = 'S';
		header[i++] = 'I';
		header[i++] = 'M';
		header[i++] = 'G';

		for (int j = 0; j < 4; ++j) {
			header[i++] = (uchar)(first & 0xFF);
			first = first >> 8;
		}

		for (int j = 0; j < 4; ++j) {
			header[i++] = (uchar)(size & 0xFF);
			size = size >> 8;
		}

		header[i++] = (uchar)(padding & 0xFF);
		header[i++] = (uchar)(padding >> 8 & 0xFF);

		header[i++] = 0; // reserved.
		header[i++] = (uchar)(count - 1); // high nibble reserved.

		header[i++] = 0; // reserved.
		header[i++] = 0; // reserved.

		// Slots usage bitmap.
		header[i++] = 0;
		header[i++] = 0;

		if ((off_t)-1 == lseek(fd, 446 + 0x10 * entry + 4, SEEK_SET)) {
			perror("Could not seek the file offset.");
			close(fd);
			exit(1);
		}

		n = write(fd, &type, 1);
		do {
			if (n < 0)
				perror("Could not change the partition type");
			else if (n != 1)
				fprintf(stderr, "Error changing the partition type.\n");
			else
				break;

			close(fd);
			exit(1);
		} while(0);

		if ((off_t)-1 == lseek(fd, p[entry].start * 512, SEEK_SET)) {
			perror("Could not seek the file offset.");
			close(fd);
			exit(1);
		}

		n = write(fd, header, 24);
		do {
			if (n < 0)
				perror("Could not write the F7h header");
			else if (n != 24)
				fprintf(stderr, "Error writing the F7h header (%zd bytes written).\n", n);
			else
				break;

			close(fd);
			exit(1);
		} while(0);
	}

	close(fd);
}

void
f7_reset(int argc, char **argv)
{
	int fd;
	int entry;
	PartEntry p[4];
	uchar header[24];
	MetaF7 meta;

	if (argc != 4) {
		usage();
		exit(1);
	}

	entry = atol2(argv[3]);
	if (entry < 0 || 3 < entry) {
		usage();
		exit(1);
	}

	fd = open(argv[2], O_CLOEXEC | O_RDWR);
	if (fd == -1) {
		perror("Cannot open the requested device/image file");
		exit(1);
	}

	// The header and its meta struct are ignored.
	// They are retrieved only to be sure it is possible.
	if (
		!read_ptable(fd, p)
		|| !f7_read_header(fd, p, entry, header)
		|| !f7_retrieve_meta(header, &meta)
		|| !f7_write_bitmap(fd, p, entry, 0x00)
	) {
		close(fd);
		exit(1);
	}

	close(fd);
}

static int
f7_read_header(int fd, PartEntry const *p, int entry, uchar *header)
{
	// This code assumes that LBA_MAX fits in the off_t type.

	ssize_t n;

	switch (p[entry].type) {
	case 0xF7:
		break;
	case 0x00:
		fprintf(stderr, "Disabled partition.\n");
		return 0;
	default:
		fprintf(stderr, "Not a F7h partition.\n");
		return 0;
	}

	if ((off_t)-1 == lseek(fd, p[entry].start * 512, SEEK_SET)) {
		perror("Could not seek the file offset.");
		return 0;
	}

	n = read(fd, header, 24);
	do {
		if (n < 0)
			perror("Could not read the F7h header");
		else if (n != 24)
			fprintf(stderr, "Error reading the F7h header (%zd bytes read).\n", n);
		else
			break;

		return 0;
	} while(0);
	return 1;
}

static int f7_retrieve_meta(uchar *header, MetaF7 *meta)
{
	int i;
	vlong padding;

	i = 0;
	uchar type = header[i++];
	uchar version = header[i++];

	do {
		if (type != 0xF7) // Type
			fprintf(stderr, "Header signature not found.\n");
		else if (
			header[i++] != 'S'
			|| header[i++] != 'Y'
			|| header[i++] != 'S'
			|| header[i++] != 'I'
			|| header[i++] != 'M'
			|| header[i++] != 'G'
		)
			fprintf(stderr, "Unknown subtype.\n");
		else if (version != 0x00)
			fprintf(stderr, "Unknown version.\n");
		else
			break;

		return 0;
	} while(0);

	meta->first =
		header[i]
		| ((vlong)header[i + 1]) << 8
		| ((vlong)header[i + 2]) << 16
		| ((vlong)header[i + 3]) << 24
	;
	i += 4;

	meta->size =
		header[i]
		| ((vlong)header[i + 1]) << 8
		| ((vlong)header[i + 2]) << 16
		| ((vlong)header[i + 3]) << 24
	;
	i += 4;

	padding =
		header[i]
		| ((vlong)header[i + 1]) << 8
	;
	i += 2;

	i++; // reserved.

	meta->count = header[i++]; // high nibble reserved.
	++meta->count;

	i++; // reserved.
	i++; // reserved.

	// Slots usage bitmap.
	meta->bitmap =
		header[i]
		| ((uint)header[i + 1]) << 8
	;
	i += 2;

	meta->every = meta->size + padding;
	return 1;
}

static int
f7_write_bitmap(
	int fd
	, PartEntry const *p
	, int entry
	, uint bitmap
)
{
	// This code assumes that LBA_MAX fits in the off_t type.

	ssize_t n;
	uchar buf[2];

	switch (p[entry].type) {
	case 0xF7:
		break;
	case 0x00:
		fprintf(stderr, "Disabled partition.\n");
		return 0;
	default:
		fprintf(stderr, "Not a F7h partition.\n");
		return 0;
	}

	buf[0] = bitmap & 0xFF;
	buf[1] = bitmap >> 8 & 0xFF;

	if ((off_t)-1 == lseek(fd, p[entry].start * 512 + (24 - 2), SEEK_SET)) {
		perror("Could not seek the file offset.");
		return 0;
	}

	n = write(fd, buf, 2);
	do {
		if (n < 0)
			perror("Could not update the slot bitmap");
		else if (n != 2)
			fprintf(stderr, "Error updating the slot bitmap (%zd bytes written).\n", n);
		else
			break;

		return 0;
	} while(0);
	return 1;
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
