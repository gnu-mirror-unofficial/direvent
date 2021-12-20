#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <time.h>

char *filename;
int force;
size_t size = 4096;
struct timespec ts;

void
usage(FILE *fp)
{
	fprintf(fp, "FIXME: no help so far\n");
}

#ifndef SIZE_T_MAX
# define SIZE_T_MAX ((size_t)-1)
#endif

static int
ts_cmp(struct timespec const *a, struct timespec const *b)
{
	if (a->tv_sec < b->tv_sec)
		return -1;
	if (a->tv_sec > b->tv_sec)
		return 1;
	if (a->tv_nsec < b->tv_nsec)
		return -1;
	if (a->tv_nsec > b->tv_nsec)
		return 1;
	return 0;
}

static struct timespec
ts_add(struct timespec const *a, struct timespec const *b)
{
	struct timespec sum;

	sum.tv_sec = a->tv_sec + b->tv_sec;
	sum.tv_nsec = a->tv_nsec + b->tv_nsec;
	if (sum.tv_nsec >= 1e9) {
		++sum.tv_sec;
		sum.tv_nsec -= 1e9;
	}
	return sum;
}

static struct timespec
ts_sub(struct timespec const *a, struct timespec const *b)
{
	struct timespec diff;

	diff.tv_sec = a->tv_sec - b->tv_sec;
	diff.tv_nsec = a->tv_nsec - b->tv_nsec;
	if (diff.tv_nsec < 0)
	{
		--diff.tv_sec;
		diff.tv_nsec += 1e9;
	}

	return diff;
}

int
main(int argc, char **argv)
{
	int c;
	char *p;
	unsigned long n;
	FILE *fp;
	size_t i;
	struct timespec end, stop, diff;
	
	while ((c = getopt(argc, argv, "f:hs:t:")) != EOF) {
		switch (c) {
		case 'f':
			filename = optarg;
			break;
			
		case 'h':
			usage(stdout);
			return 0;
			
	 	case 's':
			errno = 0;
			n = strtoul(optarg, &p, 10);
			if (errno) {
				fprintf(stderr, "invalid size: %s\n", optarg);
				return 2;
			}
			if (*p) {
				if (p[1]) {
					fprintf(stderr,
						"invalid size suffix: %s\n",
						optarg);
					return 2;
				}
				size = n;
				
#                               define XB()				\
				if (SIZE_T_MAX / size < 1024) {		\
					fprintf(stderr,			\
						"size out of range: %s\n", \
						optarg);		\
					return 2;			\
				}					\
				size <<= 10;
				
				switch (*p) {
				case 'g':
				case 'G':
					XB();
					/* fall through */
				case 'm':
				case 'M':
					XB();
					/* fall through */
				case 'k':
				case 'K':
					XB();
					break;

				default:
					fprintf(stderr,
						"invalid size suffix: %s\n",
						optarg);
					return 2;
				}
			}
			break;
			
		case 't':
			if (*optarg == '.') {
				ts.tv_sec = 0;
				p = optarg;
			} else {
				errno = 0;
				n = strtoul(optarg, &p, 10);
				if (errno || (*p && *p != '.')
				    || n > LONG_MAX) {
					fprintf(stderr,
						"invalid time interval: %s\n",
						optarg);
					return 2;
				}
				ts.tv_sec = n;
			}
			if (*p == '.') {
				char *q;
				size_t len;
				p++;
				errno = 0;
				n = strtoul(p, &q, 10);
				if (errno || *q || n > LONG_MAX) {
					fprintf(stderr,
						"invalid time interval: %s\n",
						optarg);
					return 2;
				}
				len = strlen(p);
				if (len > 9)
					ts.tv_nsec = n / 1e9;
				else {
					static unsigned long f[] = {
						1e9, 1e8, 1e7, 1e6, 1e5, 1e4,
						1e3, 1e2, 10, 1
					};
					if (1e9 / n < f[len]) {
						fprintf(stderr,
							"invalid time interval: %s\n",
							optarg);
						return 2;
					}
					ts.tv_nsec = n * f[len];
				}
			} else
				ts.tv_nsec = 0;
			break;
			
		default:
			usage(stderr);
			return 2;
		}
	}

	if (optind != argc) {
		usage(stderr);
		return 2;
	}

	if (filename) {
		if ((fp = fopen(filename, "w")) == NULL) {
			perror(filename);
			return 1;
		}
	} else
		fp = stdout;

	clock_gettime(CLOCK_MONOTONIC, &end);
	end = ts_add(&end, &ts);
	for (i = 0; i < size; i++) {
		c = i & 0xff;
		if (fputc(c, fp) == EOF) {
			perror(filename ? filename : "stdout");
			return 1;
		}
	}
	clock_gettime(CLOCK_MONOTONIC, &stop);
	if (ts_cmp(&end, &stop) > 0) {
		diff = ts_sub(&end, &stop);
		nanosleep(&diff, NULL);
	}
	fclose(fp);
	return 0;
}
