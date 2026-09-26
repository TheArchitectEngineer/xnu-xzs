/* EL0 interactive shell. Commands stay here; the kernel only dispatches syscalls. */

#define SYS_EXIT 1
#define SYS_FORK 2
#define SYS_READ 3
#define SYS_WRITE 4
#define SYS_OPEN 5
#define SYS_CLOSE 6
#define SYS_WAIT4 7
#define SYS_CHDIR 12
#define SYS_REBOOT 55
#define SYS_EXECVE 59
#define SYS_XZS_DIAG 58
#define SYS_GETDIRENTRIES 196

#define LINE_MAX 128
#define ARG_MAX 8
#define CWD_MAX 96

extern long xzs_svc(long nr, long a, long b, long c, long d);
extern int xzs_split_args(char *line, char **argv, int max_argc);

static int
slen(const char *s)
{
	int n = 0;
	while (s[n] != 0) {
		n++;
	}
	return n;
}

static int
seq(const char *a, const char *b)
{
	while (*a != 0 && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

static void
wr(const char *s)
{
	(void)xzs_svc(SYS_WRITE, 1, (long)s, slen(s), 0);
}

static void
werr(const char *s)
{
	(void)xzs_svc(SYS_WRITE, 2, (long)s, slen(s), 0);
}

static int
drain_to_eol(char *tmp, long r, long i, int *swallow_lf)
{
	for (;;) {
		for (; i < r; i++) {
			if (tmp[i] == '\r') {
				*swallow_lf = 1;
				return 0;
			}
			if (tmp[i] == '\n') {
				*swallow_lf = 0;
				return 0;
			}
		}
		r = xzs_svc(SYS_READ, 0, (long)tmp, 64, 0);
		i = 0;
		if (r <= 0) {
			return -1;
		}
	}
}

static int
read_line(char *buf, int cap, int *swallow_lf)
{
	int n = 0;

	for (;;) {
		char tmp[64];
		long r = xzs_svc(SYS_READ, 0, (long)tmp, (long)sizeof(tmp), 0);
		long i;

		if (r < 0) {
			buf[0] = 0;
			return -1;
		}
		if (r == 0) {
			buf[n] = 0;
			return n;
		}
		for (i = 0; i < r; i++) {
			char c = tmp[i];

			if (c == '\n' && *swallow_lf) {
				*swallow_lf = 0;
				continue;
			}
			*swallow_lf = 0;
			if (c == '\r' || c == '\n') {
				if (c == '\r') {
					*swallow_lf = 1;
				}
				buf[n] = 0;
				return n;
			}
			if (c == 0x08 || c == 0x7f) {
				if (n > 0) {
					n--;
					wr("\b \b");
				}
				continue;
			}
			if (n + 1 >= cap) {
				(void)drain_to_eol(tmp, r, i, swallow_lf);
				buf[0] = 0;
				wr("line too long\n");
				return -2;
			}
			buf[n++] = c;
		}
	}
}

static void
cmd_help(void)
{
	wr("help echo pwd cd ls cat display dsi clocks irq fb mem exit\n");
}

static void
cmd_diag(long which)
{
	(void)xzs_svc(SYS_XZS_DIAG, which, 0, 0, 0);
}

static long
display_which(int argc, char **argv)
{
	if (argc == 1) {
		return 1;
	}
	if (argc == 2 && seq(argv[1], "status")) {
		return 1;
	}
	if (argc == 2 && seq(argv[1], "regs")) {
		return 25;
	}
	if (argc == 2 && seq(argv[1], "m3-dryrun")) {
		return 22;
	}
	if (argc == 3 && seq(argv[1], "m3-run")) {
		if (seq(argv[2], "dryrun")) {
			return 22;
		}
		if (seq(argv[2], "pll")) {
			return 23;
		}
		if (seq(argv[2], "full")) {
			return 24;
		}
	}
	if (argc == 2 && seq(argv[1], "m4-status")) {
		return 28;
	}
	if (argc == 2 && seq(argv[1], "m4-dryrun")) {
		return 29;
	}
	if (argc == 3 && seq(argv[1], "m4-run")) {
		if (seq(argv[2], "dryrun")) {
			return 29;
		}
		if (seq(argv[2], "basic")) {
			return 30;
		}
		if (seq(argv[2], "full")) {
			return 31;
		}
	}
	if (argc == 2 && (seq(argv[1], "p1-status") || seq(argv[1], "gpio-status"))) {
		return 32;
	}
	if (argc == 3 && seq(argv[1], "gpio") && seq(argv[2], "status")) {
		return 32;
	}
	if (argc == 2 && seq(argv[1], "p1-dryrun")) {
		return 33;
	}
	if (argc >= 2 && seq(argv[1], "p1-run")) {
		return 34;
	}
	if (argc == 3 && seq(argv[1], "spmi") && seq(argv[2], "status")) {
		return 35;
	}
	if (argc == 2 && seq(argv[1], "spmi-status")) {
		return 35;
	}
	if ((argc == 4 && seq(argv[1], "pmic") && seq(argv[2], "lab") && seq(argv[3], "status")) ||
	    (argc == 3 && seq(argv[1], "lab") && seq(argv[2], "status")) ||
	    (argc == 2 && seq(argv[1], "lab-status"))) {
		return 36;
	}
	if ((argc == 4 && seq(argv[1], "pmic") && seq(argv[2], "ibb") && seq(argv[3], "status")) ||
	    (argc == 3 && seq(argv[1], "ibb") && seq(argv[2], "status")) ||
	    (argc == 2 && seq(argv[1], "ibb-status"))) {
		return 37;
	}
	if (argc == 2 && seq(argv[1], "p2-dryrun")) {
		return 38;
	}
	if (argc >= 2 && (seq(argv[1], "p2-config") || seq(argv[1], "p2-config-only"))) {
		return 39;
	}
	if (argc >= 2 && seq(argv[1], "p2-run")) {
		return 40;
	}
	if (argc >= 2 && (seq(argv[1], "m5-status") || (argc >= 3 && seq(argv[1], "m5") && seq(argv[2], "status")))) {
		return 41;
	}
	if (argc >= 2 && (seq(argv[1], "m5-dryrun") || (argc >= 3 && seq(argv[1], "m5") && seq(argv[2], "dryrun")))) {
		return 42;
	}
	if (argc >= 2 && (seq(argv[1], "m5-stage1") || (argc >= 3 && seq(argv[1], "m5") && seq(argv[2], "stage1")))) {
		return 43;
	}
	if (argc >= 2 && (seq(argv[1], "m5-run") || (argc >= 3 && seq(argv[1], "m5") && seq(argv[2], "run")))) {
		return 44;
	}
	if (argc >= 2 && (seq(argv[1], "m6-dryrun") || (argc >= 3 && seq(argv[1], "m6") && seq(argv[2], "dryrun")))) {
		return 45;
	}
	if (argc >= 2 && (seq(argv[1], "m6-stage1") || (argc >= 3 && seq(argv[1], "m6") && seq(argv[2], "stage1")))) {
		return 46;
	}
	if (argc >= 2 && (seq(argv[1], "m6-stage2") || (argc >= 3 && seq(argv[1], "m6") && seq(argv[2], "stage2")))) {
		return 47;
	}
	if (argc >= 2 && (seq(argv[1], "m6-run") || (argc >= 3 && seq(argv[1], "m6") && seq(argv[2], "run")))) {
		return 48;
	}
	if (argc >= 2 && (seq(argv[1], "m8-status") || (argc >= 3 && seq(argv[1], "m8") && seq(argv[2], "status")))) {
		return 50;
	}
	if (argc >= 2 && (seq(argv[1], "m8-dryrun") || (argc >= 3 && seq(argv[1], "m8") && seq(argv[2], "dryrun")))) {
		return 51;
	}
	if (argc == 3 && seq(argv[1], "power")) {
		if (seq(argv[2], "status")) {
			return 7;
		}
		if (seq(argv[2], "mmagic-on")) {
			return 8;
		}
		if (seq(argv[2], "mdss-on")) {
			return 9;
		}
	}
	return -1;
}

static long
dsi_which(int argc, char **argv)
{
	if (argc == 1) {
		return 2;
	}
	if (argc == 2 && seq(argv[1], "pll")) {
		return 26;
	}
	if (argc == 2 && seq(argv[1], "phy")) {
		return 27;
	}
	return -1;
}

static long
clocks_which(int argc, char **argv)
{
	if (argc == 1) {
		return 3;
	}
	if (argc == 3 && seq(argv[1], "display") && seq(argv[2], "status")) {
		return 3;
	}
	if (argc == 2 && seq(argv[1], "mdss-ahb-status")) {
		return 13;
	}
	if (argc == 2 && seq(argv[1], "mdss-ahb-debug")) {
		return 14;
	}
	if (argc == 2 && seq(argv[1], "mdss-critical-status")) {
		return 15;
	}
	if (argc == 2 && seq(argv[1], "mmagic-ahb-on")) {
		return 16;
	}
	if (argc == 2 && seq(argv[1], "mmagic-cfg-ahb-on")) {
		return 17;
	}
	if (argc == 2 && seq(argv[1], "mmagic-mdss-noc-on")) {
		return 18;
	}
	if (argc == 2 && seq(argv[1], "mmagic-mdss-axi-on")) {
		return 19;
	}
	if (argc == 2 && seq(argv[1], "mdss-ahb-on")) {
		return 10;
	}
	if (argc == 2 && seq(argv[1], "mdss-axi-on")) {
		return 11;
	}
	if (argc == 2 && seq(argv[1], "mdp-on")) {
		return 12;
	}
	return -1;
}

static void
cmd_echo(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		if (i > 1) {
			wr(" ");
		}
		wr(argv[i]);
	}
	wr("\n");
}

static void
cmd_pwd(const char *cwd)
{
	wr(cwd);
	wr("\n");
}

static int
copy_str(char *dst, int cap, const char *src)
{
	int n = slen(src);
	int i;

	if (n + 1 > cap) {
		return -1;
	}
	for (i = 0; i < n; i++) {
		dst[i] = src[i];
	}
	dst[n] = 0;
	return 0;
}

static void
cmd_cd(int argc, char **argv, char *cwd)
{
	char next[CWD_MAX];
	const char *path;
	long rc;

	if (argc != 2) {
		werr("cd: usage: cd path\n");
		return;
	}
	path = argv[1];
	if (seq(path, ".")) {
		return;
	}
	if (path[0] == '/') {
		if (copy_str(next, CWD_MAX, path) != 0) {
			werr("cd: path too long\n");
			return;
		}
	} else if (seq(path, "..")) {
		int n = slen(cwd);
		int i;
		if (seq(cwd, "/")) {
			return;
		}
		while (n > 1 && cwd[n - 1] != '/') {
			n--;
		}
		if (n > 1) {
			n--;
		}
		for (i = 0; i < n; i++) {
			next[i] = cwd[i];
		}
		next[n] = 0;
		if (next[0] == 0) {
			next[0] = '/';
			next[1] = 0;
		}
		path = next;
	} else {
		int n = slen(cwd);
		int m = slen(path);
		int i;
		int slash = (n > 1);

		if (n + slash + m + 1 > CWD_MAX) {
			werr("cd: path too long\n");
			return;
		}
		for (i = 0; i < n; i++) {
			next[i] = cwd[i];
		}
		if (slash) {
			next[n++] = '/';
		}
		for (i = 0; i < m; i++) {
			next[n++] = path[i];
		}
		next[n] = 0;
		path = next;
	}
	rc = xzs_svc(SYS_CHDIR, (long)path, 0, 0, 0);
	if (rc < 0) {
		werr("cd: failed\n");
		return;
	}
	(void)copy_str(cwd, CWD_MAX, path);
}

static void
cmd_ls(int argc, char **argv, const char *cwd)
{
	const char *path = (argc > 1) ? argv[1] : cwd;
	long fd;
	char buf[256];
	long base = 0;

	fd = xzs_svc(SYS_OPEN, (long)path, 0, 0, 0);
	if (fd < 0) {
		werr("ls: ");
		werr(path);
		werr(": open failed\n");
		return;
	}
	for (;;) {
		long n = xzs_svc(SYS_GETDIRENTRIES, fd, (long)buf, (long)sizeof(buf), (long)&base);
		long off = 0;
		if (n < 0) {
			werr("ls: read failed\n");
			break;
		}
		if (n == 0) {
			break;
		}
		while (off + 8 <= n) {
			unsigned short reclen = *(unsigned short *)(buf + off + 4);
			unsigned char namlen = *(unsigned char *)(buf + off + 7);
			if (reclen < 9 || off + reclen > n ||
			    (unsigned long)namlen + 8 > reclen || namlen > 63) {
				break;
			}
			{
				char name[64];
				int i;
				for (i = 0; i < namlen; i++) {
					name[i] = buf[off + 8 + i];
				}
				name[namlen] = 0;
				wr(name);
				wr("\n");
			}
			off += reclen;
		}
	}
	(void)xzs_svc(SYS_CLOSE, fd, 0, 0, 0);
}

static void
cmd_cat(int argc, char **argv)
{
	long fd;
	char buf[128];

	if (argc != 2) {
		werr("cat: usage: cat file\n");
		return;
	}
	fd = xzs_svc(SYS_OPEN, (long)argv[1], 0, 0, 0);
	if (fd < 0) {
		werr("cat: ");
		werr(argv[1]);
		werr(": open failed\n");
		return;
	}
	for (;;) {
		long n = xzs_svc(SYS_READ, fd, (long)buf, (long)sizeof(buf), 0);
		if (n < 0) {
			werr("cat: read failed\n");
			break;
		}
		if (n == 0) {
			break;
		}
		(void)xzs_svc(SYS_WRITE, 1, (long)buf, n, 0);
	}
	(void)xzs_svc(SYS_CLOSE, fd, 0, 0, 0);
}

__attribute__((always_inline)) static inline int
join2(char *dst, int cap, const char *a, const char *b)
{
	int na = slen(a);
	int nb = slen(b);
	int i;
	int slash = (na > 0 && a[na - 1] != '/');

	if (na + slash + nb + 1 > cap) {
		return -1;
	}
	for (i = 0; i < na; i++) {
		dst[i] = a[i];
	}
	if (slash) {
		dst[na++] = '/';
	}
	for (i = 0; i < nb; i++) {
		dst[na++] = b[i];
	}
	dst[na] = 0;
	return 0;
}

static void
run_external(int argc, char **argv)
{
	char path[CWD_MAX];
	const char *file = argv[0];
	long pid;
	int status = 0;
	char *envp[4];

	envp[0] = "PATH=/bin:/sbin";
	envp[1] = "HOME=/";
	envp[2] = "TERM=xzs";
	envp[3] = 0;

	if (file[0] != '/') {
		if (join2(path, CWD_MAX, "/bin", file) != 0) {
			werr(file);
			werr(": command not found\n");
			return;
		}
		file = path;
	}
	pid = xzs_svc(SYS_FORK, 0, 0, 0, 0);
	if (pid < 0) {
		werr("fork: failed\n");
		return;
	}
	if (pid == 0) {
		(void)xzs_svc(SYS_EXECVE, (long)file, (long)argv, (long)envp, 0);
		werr(argv[0]);
		werr(": command not found\n");
		(void)xzs_svc(SYS_EXIT, 127, 0, 0, 0);
		for (;;) {
		}
	}
	(void)xzs_svc(SYS_WAIT4, pid, (long)&status, 0, 0);
}

static long
parse_long(const char *s)
{
	long val = 0;
	while (*s >= '0' && *s <= '9') {
		val = val * 10 + (*s - '0');
		s++;
	}
	return val;
}

void
xzs_d7t2_shell(void)
{
	char line[LINE_MAX];
	char *argv[ARG_MAX];
	char cwd[CWD_MAX];

	cwd[0] = '/';
	cwd[1] = 0;
	{
	int swallow_lf = 0;
	for (;;) {
		int argc;
		int got;

		wr("xzs# ");
		got = read_line(line, LINE_MAX, &swallow_lf);
		if (got < 0) {
			continue;
		}
		argc = xzs_split_args(line, argv, ARG_MAX);
		if (argc < 0) {
			werr("too many arguments\n");
			continue;
		}
		if (argc == 0) {
			continue;
		}
		if (seq(argv[0], "help")) {
			cmd_help();
		} else if (seq(argv[0], "echo")) {
			cmd_echo(argc, argv);
		} else if (seq(argv[0], "pwd")) {
			cmd_pwd(cwd);
		} else if (seq(argv[0], "cd")) {
			cmd_cd(argc, argv, cwd);
		} else if (seq(argv[0], "ls")) {
			cmd_ls(argc, argv, cwd);
		} else if (seq(argv[0], "cat")) {
			cmd_cat(argc, argv);
		} else if (seq(argv[0], "display")) {
			long which = display_which(argc, argv);
			if (which < 0) {
				werr("display: usage\n");
			} else {
				cmd_diag(which);
			}
		} else if (seq(argv[0], "dsi")) {
			long which = dsi_which(argc, argv);
			if (which < 0) {
				werr("dsi: usage: dsi [pll|phy]\n");
			} else {
				cmd_diag(which);
			}
		} else if (seq(argv[0], "clocks")) {
			long which = clocks_which(argc, argv);
			if (which < 0) {
				werr("clocks: usage\n");
			} else {
				cmd_diag(which);
			}
		} else if (seq(argv[0], "irq")) {
			cmd_diag(4);
		} else if (seq(argv[0], "fb")) {
			cmd_diag(5);
		} else if (seq(argv[0], "mem")) {
			cmd_diag(6);
		} else if (seq(argv[0], "xzsfs")) {
			if (argc >= 3 && seq(argv[1], "ubc")) {
				char full_path[CWD_MAX];
				const char *p = argv[2];
				if (p[0] != '/') {
					if (join2(full_path, CWD_MAX, cwd, p) == 0) {
						p = full_path;
					}
				}
				(void)xzs_svc(SYS_XZS_DIAG, 20, (long)p, 0, 0);
			} else if (argc >= 5 && seq(argv[1], "pagecheck")) {
				char full_path[CWD_MAX];
				const char *p = argv[2];
				if (p[0] != '/') {
					if (join2(full_path, CWD_MAX, cwd, p) == 0) {
						p = full_path;
					}
				}
				long off = parse_long(argv[3]);
				long sz = parse_long(argv[4]);
				(void)xzs_svc(SYS_XZS_DIAG, 21, (long)p, off, sz);
			} else {
				werr("usage: xzsfs ubc <path> | xzsfs pagecheck <path> <offset> <size>\n");
			}
		} else if (seq(argv[0], "exit")) {
			(void)xzs_svc(SYS_EXIT, 0, 0, 0, 0);
		} else if (seq(argv[0], "reboot")) {
			(void)xzs_svc(SYS_REBOOT, 0, 0, 0, 0);
		} else {
			run_external(argc, argv);
		}
	}
	}
}
