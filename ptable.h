// Copyright © 2019-2020 Mikel Cazorla Pérez
// This file is part of f7disk,
// licensed under the terms of GPLv2.

typedef struct {
	int boot;
	int type;
	vlong start;
	vlong size;
} PartEntry;

int read_ptable(int fd, PartEntry *p);
