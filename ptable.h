typedef struct {
	int boot;
	int type;
	vlong start;
	vlong size;
} PartEntry;

int read_ptable(int fd, PartEntry *p);
