// Copyright © 2019 Mikel Cazorla Pérez — All Rights Reserved.

typedef struct {
	int boot;
	int type;
	vlong start;
	vlong size;
} PartEntry;

int read_ptable(int fd, PartEntry *p);
