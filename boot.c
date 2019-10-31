// Copyright © 2019 Mikel Cazorla Pérez — All Rights Reserved.

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "u.h"
#include "f7disk.h"
#include "ptable.h"

void
f7_cpboot(int argc, char **argv)
{
	off_t size[2], reqsectors;
	int fd[2];
	PartEntry p[4];

	fd[0] = -1;
	fd[1] = -1;

	if (argc != 4) {
		usage();
		goto cleanup;
	}

	fd[0] = open(argv[2], O_CLOEXEC | O_RDWR);
	if (fd[0] == -1)
		goto openerror;
	fd[1] = open(argv[3], O_CLOEXEC | O_RDONLY);
	if (fd[1] == -1)
		goto openerror;

	if (!read_ptable(fd[0], p))
		goto cleanup;

	do {
		if ((size[0] = lseek(fd[0], 0, SEEK_END)) == (off_t)-1)
			perror("Could not retrieve the drive file size");
		else if ((off_t)-1 == lseek(fd[0], 0, SEEK_SET))
			perror("Could not seek the drive file offset");
		else
			break;

		goto cleanup;
	} while (0);

	do {
		if ((size[1] = lseek(fd[1], 0, SEEK_END)) == (off_t)-1)
			perror("Could not retrieve the bootloader file size");
		else if ((off_t)-1 == lseek(fd[1], 0, SEEK_SET))
			perror("Could not seek the bootloader file offset");
		else if (size[1] < 512)
			fprintf(
				stderr
				, "The bootloader has less than 512 bytes (cannot contain a MBR).\n"
			);
		else
			break;

		goto cleanup;
	} while (0);

	reqsectors = size[1] / 512 + (size[1] % 512 != 0? 1: 0);
	if (size[0] / 512 < reqsectors) {
		fprintf(
			stderr
			, "The drive has not enough sectors (%ld < %jd).\n"
			, size[0] / 512
			, reqsectors
		);

		goto cleanup;
	}

	{
		int worst = -1;

		// It is not assumed that partitions are ordered.
		// GPT protective MBR partitions are ignored.
		for (int i = 0; i < 4; ++i) {
			if (p[i].type == 0x00 || p[i].type == 0xEE)
				continue;

			if (worst < 0 || p[i].start < p[worst].start)
				worst = i;
		}

		if (0 <= worst && p[worst].start < reqsectors) {
			fprintf(
				stderr
				, "Not enough free sectors (%lld < %jd).\n"
				, p[worst].start
				, reqsectors
			);

			goto cleanup;
		}
	}

	do {
		ssize_t n;
		size_t count;
		off_t rem;
		struct stat statbuf;
		uchar *buf;

		do {
			if (fstat(fd[0], &statbuf) < 0)
				perror("Could not use stat over the file");
			else if ((buf = (uchar *)malloc(statbuf.st_blksize)) == nil)
				fprintf(stderr, "Could not allocate the copy buffer.\n");
			else
				break;

			goto cleanup;
		} while (0);

		size[1] -= (6 + 4 * 16);
		rem = size[1];
		{
			count = 512;
			n = read(fd[1], buf, count);
			if ((size_t)n == count) do {
				if (buf[510] != 0x55 || buf[511] != 0xAA) {
					fprintf(stderr, "MBR magic number not found in the bootloader.\n");
					goto rwerror;
				}

				// Just before the disk signature.
				count = 0x1B8;
				n = write(fd[0], buf, count);
				if (0 < n)
					rem -= n;

				if ((size_t)n != count)
					break;

				// Just after the partition table.
				if ((off_t)-1 == lseek(fd[0], 0x1FE, SEEK_SET)) {
					perror("Could not seek the drive file offset");
					goto rwerror;
				}

				count = 2;
				n = write(fd[0], &buf[0x1FE], count);
				if (0 < n)
					rem -= n;
			} while(0);

			do {
				if (n < 0)
					perror("Could not copy the MBR");
				else if ((size_t)n < count)
					fprintf(stderr, "Could not copy the MBR.\n");
				else
					break;

				if (rem < size[1])
					fprintf(
						stderr
						, "WARNING: %jd/%jd bytes were actually copied.\n"
						, rem
						, size[1]
					);

				goto rwerror;
			} while(0);
		}

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
					perror("Could not copy the whole bootloader");
				else if ((size_t)n < count)
					fprintf(stderr, "Could not copy the whole bootloader.\n");
				else
					break;

				fprintf(
					stderr
					, "WARNING: %jd/%jd bytes were actually copied.\n"
					, rem
					, size[1]
				);

				goto rwerror;
			} while(0);
		}

		free(buf);
		break;
rwerror:
		free(buf);
		goto cleanup;
	} while(0);

	close(fd[1]);
	close(fd[0]);
	return;

openerror:
	perror("Cannot open the requested device/image file");
cleanup:
	if (0 <= fd[1])
		close(fd[1]);
	if (0 <= fd[0])
		close(fd[0]);
	exit(1);
}
