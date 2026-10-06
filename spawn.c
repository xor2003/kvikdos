#include "kvikdos.h"

int run_dos_child_subprocess(const char *dos_filename, const char *dos_args, const char *env, const char *env_end, const DirState *dir_state, unsigned char *exit_code_out) {
  char self_exe[LINUX_PATH_SIZE];
  char drive_arg[16];
  char mount_arg[LINUX_PATH_SIZE + 32];
  char *argv_child[256];
  char *owned_args[200];
  int argc = 0, owned_count = 0, i, status;
  pid_t pid;
  ssize_t got;
  const char *p;

  if (!dos_filename || !*dos_filename || !exit_code_out) { errno = EINVAL; return -1; }
  got = readlink("/proc/self/exe", self_exe, sizeof(self_exe) - 1);
  if (got <= 0 || got >= (ssize_t)sizeof(self_exe) - 1) {
    errno = ENOENT;
    return -1;
  }
  self_exe[got] = '\0';

  argv_child[argc++] = self_exe;
  for (i = 0; i < DRIVE_COUNT; ++i) {
    const char *mount = dir_state->linux_mount_dir[i];
    const char case_c = dir_state->case_mode[i] == CASE_MODE_LOWERCASE ? '-' : ':';
    char cwd_mount[LINUX_PATH_SIZE];
    if (!mount) continue;
    if (*mount == '\0') {
      /* An empty mount means "the program's directory" (cwd). A bare
       * --mount=E: would re-parse to the placeholder and end up unmounted for
       * a DOS-path child, so serialize the real cwd instead. */
      char *cw = getcwd(cwd_mount, sizeof(cwd_mount) - 1);
      size_t n;
      if (!cw) continue;
      n = strlen(cw);
      if (n + 1 < sizeof(cwd_mount) && (n == 0 || cw[n - 1] != '/')) { cw[n++] = '/'; cw[n] = '\0'; }
      mount = cw;
    }
    snprintf(mount_arg, sizeof(mount_arg), "--mount=%c%c%s", 'A' + i, case_c, mount);
    owned_args[owned_count] = xstrdup(mount_arg);
    if (!owned_args[owned_count]) goto alloc_fail;
    argv_child[argc++] = owned_args[owned_count++];
  }
  snprintf(drive_arg, sizeof(drive_arg), "--drive=%c:", dir_state->drive);
  owned_args[owned_count] = xstrdup(drive_arg);
  if (!owned_args[owned_count]) goto alloc_fail;
  argv_child[argc++] = owned_args[owned_count++];

  if (env && env_end && env < env_end) {
    for (p = env; p < env_end && *p != '\0';) {
      const char *q = memchr(p, '\0', env_end - p);
      size_t n;
      char *ea;
      if (!q) break;
      n = (size_t)(q - p);
      ea = (char*)malloc(n + 7);
      if (!ea) goto alloc_fail;
      memcpy(ea, "--env=", 6);
      memcpy(ea + 6, p, n);
      ea[n + 6] = '\0';
      owned_args[owned_count++] = ea;
      argv_child[argc++] = ea;
      p = q + 1;
    }
  }

  argv_child[argc++] = (char*)dos_filename;
  if (dos_args && *dos_args) argv_child[argc++] = (char*)dos_args;
  argv_child[argc] = NULL;

  pid = fork();
  if (pid < 0) goto child_fail;
  if (pid == 0) {
    execv(self_exe, argv_child);
    _exit(127);
  }
  if (waitpid(pid, &status, 0) < 0) goto child_fail;
  if (WIFEXITED(status)) *exit_code_out = (unsigned char)WEXITSTATUS(status);
  else *exit_code_out = 252;
  for (i = 0; i < owned_count; ++i) free(owned_args[i]);
  return 0;

 alloc_fail:
  errno = ENOMEM;
 child_fail:
  for (i = 0; i < owned_count; ++i) free(owned_args[i]);
  return -1;
}

int run_with_wine(const char *prog_filename, const char *const *args, const char *linux_cwd) {
  const char *argv_child[512];
  unsigned argc = 0;
  int status;
  pid_t pid;
  argv_child[argc++] = "wine";
  argv_child[argc++] = prog_filename;
  if (args) {
    while (*args && argc + 1 < (sizeof(argv_child) / sizeof(argv_child[0]))) argv_child[argc++] = *args++;
  }
  argv_child[argc] = NULL;
  pid = fork();
  if (pid < 0) {
    perror("error: fork for wine failed");
    return 1;
  }
  if (pid == 0) {
    if (linux_cwd && *linux_cwd && chdir(linux_cwd) != 0) {
      perror("error: failed to chdir for wine");
      _exit(127);
    }
    execvp("wine", (char * const*)argv_child);
    perror("error: failed to execute wine");
    _exit(127);
  }
  if (waitpid(pid, &status, 0) < 0) {
    perror("error: waitpid for wine failed");
    return 1;
  }
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return 1;
}

int has_wine_in_path(void) {
  const char *path = getenv("PATH");
  const char *p, *q;
  char buf[LINUX_PATH_SIZE];
  size_t n;
  if (!path || !*path) return 0;
  for (p = path;; p = q + 1) {
    q = strchr(p, ':');
    if (!q) q = p + strlen(p);
    n = (size_t)(q - p);
    if (n == 0) {
      if (sizeof(buf) > 5) {
        memcpy(buf, "./wine", 7);
        if (access(buf, X_OK) == 0) return 1;
      }
    } else if (n + 1 + 4 + 1 <= sizeof(buf)) {
      memcpy(buf, p, n);
      buf[n++] = '/';
      memcpy(buf + n, "wine", 5);
      if (access(buf, X_OK) == 0) return 1;
    }
    if (*q == '\0') break;
  }
  return 0;
}

int is_linux_native_executable(const char *path) {
  int fd;
  unsigned char h[4];
  ssize_t got;
  if (!path) return 0;
  fd = open(path, O_RDONLY);
  if (fd < 0) return 0;
  got = read(fd, h, sizeof(h));
  close(fd);
  if (got >= 4 && h[0] == 0x7f && h[1] == 'E' && h[2] == 'L' && h[3] == 'F') return 1;
  if (got >= 2 && h[0] == '#' && h[1] == '!') return 1;
  return 0;
}

int run_native_execvp(const char *prog_filename, const char *const *args) {
  const char *argv_child[512];
  unsigned argc = 0;
  int status;
  pid_t pid;
  argv_child[argc++] = prog_filename;
  if (args) {
    while (*args && argc + 1 < (sizeof(argv_child) / sizeof(argv_child[0]))) argv_child[argc++] = *args++;
  }
  argv_child[argc] = NULL;
  pid = fork();
  if (pid < 0) {
    perror("error: fork failed");
    return 1;
  }
  if (pid == 0) {
    execvp(prog_filename, (char * const*)argv_child);
    perror("error: failed to execute native program");
    _exit(127);
  }
  if (waitpid(pid, &status, 0) < 0) {
    perror("error: waitpid failed");
    return 1;
  }
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return 1;
}
