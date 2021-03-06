
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>

#include <err.h>
#include <errno.h>
#include <libprocstat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int needheader = 1;
static int binonly = 0;

static int
getprocstr(pid_t pid, int node, char *str, size_t maxlen) {
	size_t len = maxlen;
	int name[4] = { CTL_KERN, KERN_PROC, node, pid };
	str[0] = '\0';
	int error = sysctl(name, nitems(name), str, &len, NULL, 0);
	if (error != 0) return errno;
	else return 0;
}

static int
getpathname(pid_t pid, char *pathname, size_t maxlen) {
	return getprocstr(pid, KERN_PROC_PATHNAME, pathname, maxlen);
}

static int
getargs(pid_t pid, char *args, size_t maxlen) {
	return getprocstr(pid, KERN_PROC_ARGS, args, maxlen);
}

static void
needsrestart(const struct kinfo_proc *proc, const char *why, const char *note) {
	if (needheader) {
		needheader = 0;
		printf("%5s %5s %16s %7s %s\n", "PID", "JID", "COMMAND", "UPDATED", "ARGS");
	}
	printf("%5d %5d %16s %7s %s\n", proc->ki_pid, proc->ki_jid, proc->ki_comm, why, note);
}

static void
usage(void) {
	printf("usage: checkrestart [-Hb]");
	exit(1);
}

int
main(int argc, char **argv) {
	char ch;
	while ((ch = getopt(argc, argv, "Hb")) != -1)
		switch (ch) {
			case 'H':
				needheader = 0;
				break;
			case 'b':
				binonly = 1;
				break;
			case '?':
			default:
				usage();
		}

	argc -= optind;
	argv += optind;

	// TODO: accept a list of pids
	if (*argv) usage();

	struct procstat *prstat = procstat_open_sysctl();
	if (prstat == NULL) errx(1, "procstat_open()");

	unsigned int cnt;
	struct kinfo_proc *p = procstat_getprocs(prstat, KERN_PROC_PROC, 0, &cnt);
	if (p == NULL) errx(1, "procstat_getprocs()");

	for (unsigned int i = 0; i < cnt; i++) {
		char pathname[PATH_MAX];
		struct kinfo_proc *proc = &p[i];

		if (proc->ki_ppid == 0) continue;

		int error = getpathname(proc->ki_pid, pathname, sizeof(pathname));
		if (error != 0 && error != ENOENT) continue;
		if (error == ENOENT) {
			char args[PATH_MAX];
			(void)getargs(proc->ki_pid, args, sizeof(args));
			needsrestart(proc, "Binary", args);
		} else if (!binonly) {
			unsigned int vmcnt;
			struct kinfo_vmentry *freep = procstat_getvmmap(prstat, proc, &vmcnt);

			for (unsigned j = 0; j < vmcnt; j++) {
				struct kinfo_vmentry *kve = &freep[j];
				if ((kve->kve_protection & KVME_PROT_EXEC) == KVME_PROT_EXEC &&
				    kve->kve_type == KVME_TYPE_VNODE &&
				    kve->kve_path[0] == '\000') {
					needsrestart(proc, "Library", pathname);
					break;
				}
			}

			procstat_freevmmap(prstat, freep);
		}
	}

	procstat_freeprocs(prstat, p);
	procstat_close(prstat);
	return 0;
}
