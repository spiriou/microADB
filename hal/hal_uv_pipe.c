#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "adb.h"
#include "hal_uv_priv.h"

// FIXME builtin_exec2
#include <spawn.h>
#include <fcntl.h>
#include <errno.h>
#include "builtin/builtin.h"

static void exec_on_data_available(uv_poll_t* handle, int status, int events);

static void adb_hal_pipe_close_callback(uv_handle_t *handle) {
    adb_log("entry\n");
    adb_pipe_t *pipe = container_of(handle, adb_pipe_t, handle);
    close(pipe->handle.io_watcher.fd);

    // char path[16];
    // sprintf(path, "/dev/ai%d", apipe->uid);
    // unlink(path);
    // path[6] = 'o';
    // unlink(path);
    pipe->close_cb(pipe);
}

void adb_hal_pipe_destroy(adb_pipe_t *pipe, void (*close_cb)(adb_pipe_t*)) {
    pipe->close_cb = close_cb;
    close(pipe->write_fd);
    uv_close((uv_handle_t*)&pipe->handle, adb_hal_pipe_close_callback);
}

int adb_hal_pipe_setup(adb_client_t *client, adb_pipe_t *apipe) {
    int ret;
    // int fds[2];
    static int pipe_id = 0;
    // uv_handle_t *handle = (uv_handle_t*)(client+1);

    apipe->handle.data = client; // (uv_handle_t*)(client+1);
    apipe->uid = pipe_id++;
    char path[16];
    sprintf(path, "/dev/ai%d", apipe->uid);

    /* Create pipe for stdin */

    // stdin
    ret = mkfifo(path, 0666);
    adb_log("PIPE1 %d %d\n", ret, errno);

    sprintf(path, "/dev/ai%d", apipe->uid);
    adb_log("try open <%s>\n", path);
    ret = open(path, O_WRONLY);
    adb_log("PIPE1 OPEN %d %d\n", ret, errno);
    apipe->write_fd = ret;

    ret = fcntl(apipe->write_fd, F_GETFD);
    fcntl(apipe->write_fd, F_SETFD, ret | FD_CLOEXEC);
    ret = fcntl(apipe->write_fd, F_GETFL);
    fcntl(apipe->write_fd, F_SETFL, ret | O_NONBLOCK);

    // stdout
    path[6] = 'o';
    ret = mkfifo(path, 0666);
    adb_log("PIPE2 %d %d\n", ret, errno);

#if 0
    adb_log("entry\n");
    ret = pipe(fds);
    adb_log("pipe %d %d\n", ret, errno);
    if (ret) {
        return -1;
    }

    adb_log("PIPE FD %d %d\n", fds[0], fds[1]);
#if 1
    if (fds[0] <= 2) {
        ret = dup2(fds[0], 3);
        adb_log("DUP2 IN %d %d\n", ret, errno);
        close(fds[0]);
        fds[0] = ret;
    }
#endif
    apipe->std_fd[0] = fds[0];
    apipe->write_fd = fds[1];

    ret = fcntl(fds[1], F_GETFD);
    fcntl(fds[1], F_SETFD, ret | FD_CLOEXEC);

    /* Create pipe for stdout */

    ret = pipe(fds);
    if (ret) {
        close(apipe->write_fd);
        close(apipe->std_fd[1]);
        return -1;
    }

#if 1
    if (fds[1] <= 2) {
        ret = dup2(fds[1], 3);
        adb_log("DUP2 OUT %d %d\n", ret, errno);
        close(fds[1]);
        fds[1] = ret;
    }
#endif

    ret = fcntl(fds[0], F_GETFD);
    fcntl(fds[0], F_SETFD, ret | FD_CLOEXEC);
    ret = fcntl(fds[0], F_GETFL);
    fcntl(fds[0], F_SETFL, ret | O_NONBLOCK);

    adb_log("exit\n");

    apipe->std_fd[1] = fds[1];
    ret = uv_poll_init(handle->loop, &apipe->handle, fds[0]);
#endif
    return 0;
}

int adb_hal_pipe_write(adb_pipe_t *pipe, const void *buf, size_t count) {
    return write(pipe->write_fd, buf, count);
}

int adb_hal_pipe_start(adb_pipe_t *pipe, void (*on_data_cb)(adb_pipe_t*, apacket*)) {
  pipe->on_data_cb = on_data_cb;
  return uv_poll_start(&pipe->handle, UV_READABLE, exec_on_data_available);
}

static void exec_on_data_available(uv_poll_t* handle, int status, int events) {
    // adb_log("entry\n");
    int ret;
    apacket_uv_t *ap;
    adb_pipe_t *pipe = container_of(handle, adb_pipe_t, handle);

    adb_client_t *client = (adb_client_t*)pipe->handle.data;

    if (status) {
        adb_log("status error %d\n", status);
        fatal("ERROR");
        pipe->on_data_cb(pipe, NULL);
        return;
    }

    ap = adb_uv_packet_allocate((adb_client_uv_t*)client, 0);
    if (ap == NULL) {
        adb_log("frame allocation failed\n");
        uv_poll_stop(&pipe->handle);
        // pipe->on_data_cb(pipe, NULL);
        return;
    }

    int nread = 0;
    do {
        ret = read(handle->io_watcher.fd, &ap->p.data[nread], 1);

        if (ret == 0) {
            /* EOF */
            break;
        }

        if (ret < 0) {
            adb_log("frame read failed %d %d\n", ret, errno);
            if (errno == EAGAIN) {
                if (nread <= 0) {
                  goto exit_release_packet;
                }
                break;
            }
            goto exit_error;
        }

        if (ap->p.data[nread++] == '\n') {
            ap->p.data[nread++] = '\r';
        }
    }
    while (nread < CONFIG_ADB_PAYLOAD_SIZE-1);

    // if (nread > 0) {
        ap->p.msg.data_length = nread;
        pipe->on_data_cb(pipe, &ap->p);
        return;
    // }

exit_error:
    // pipe->on_data_cb(pipe, NULL);
    ap->p.msg.data_length = 0;
exit_release_packet:
    adb_hal_apacket_release((adb_client_t*)pipe->handle.data, &ap->p);   
}

































static int exec_builtin2(FAR const char *appname, FAR char * const *argv,
                 FAR adb_pipe_t *apipe)
{
  FAR const struct builtin_s *builtin;
  posix_spawnattr_t attr;
  posix_spawn_file_actions_t file_actions;
  struct sched_param param;
  pid_t pid;
  int index;
  int ret;

  /* Verify that an application with this name exists */

  _err("entry: check builtin <%s>\n", appname);

  index = builtin_isavail(appname);
  if (index < 0)
    {
      ret = ENOENT;
      goto errout_with_errno;
    }

  /* Get information about the builtin */

  builtin = builtin_for_index(index);
  if (builtin == NULL)
    {
      ret = ENOENT;
      goto errout_with_errno;
    }

  /* Initialize attributes for task_spawn(). */

  ret = posix_spawnattr_init(&attr);
  if (ret != 0)
    {
      goto errout_with_errno;
    }

  ret = posix_spawn_file_actions_init(&file_actions);
  if (ret != 0)
    {
      goto errout_with_attrs;
    }

  /* Set the correct task size and priority */

  param.sched_priority = builtin->priority;
  ret = posix_spawnattr_setschedparam(&attr, &param);
  if (ret != 0)
    {
      goto errout_with_actions;
    }

  ret = task_spawnattr_setstacksize(&attr, builtin->stacksize);
  if (ret != 0)
    {
      goto errout_with_actions;
    }

  /* If robin robin scheduling is enabled, then set the scheduling policy
   * of the new task to SCHED_RR before it has a chance to run.
   */

#if CONFIG_RR_INTERVAL > 0
  ret = posix_spawnattr_setschedpolicy(&attr, SCHED_RR);
  if (ret != 0)
    {
      goto errout_with_actions;
    }

  ret = posix_spawnattr_setflags(&attr,
                                 POSIX_SPAWN_SETSCHEDPARAM |
                                 POSIX_SPAWN_SETSCHEDULER);
  if (ret != 0)
    {
      goto errout_with_actions;
    }

#else
  ret = posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSCHEDPARAM);
  if (ret != 0)
    {
      goto errout_with_actions;
    }

#endif

  /* Is output being redirected? */

  if (apipe)
    {
      /* Set up to close open redirfile and set to stdout (1) */
      char path[16];
      sprintf(path, "/dev/ai%d", apipe->uid);
      ret = posix_spawn_file_actions_addopen(&file_actions, 0,
                                             path, O_RDONLY, 0644);
      if (ret != 0)
        {
          serr("ERROR: posix_spawn_file_actions_addopen failed IN: %d\n", ret);
          goto errout_with_actions;
        }

      // STDOUT
      path[6] = 'o';
      ret = posix_spawn_file_actions_addopen(&file_actions, 1,
                                             path, O_WRONLY, 0644);
      if (ret != 0)
        {
          serr("ERROR: posix_spawn_file_actions_addopen failed OUT: %d\n", ret);
          goto errout_with_actions;
        }
    }

#ifdef CONFIG_LIBC_EXECFUNCS
  /* Load and execute the application. */

  ret = posix_spawn(&pid, builtin->name, &file_actions, &attr,
                    (argv) ? &argv[1] : (FAR char * const *)NULL, NULL);

  if (ret != 0 && builtin->main != NULL)
#endif
    {
      /* Start the built-in */

      ret = task_spawn(&pid, builtin->name, builtin->main, &file_actions,
                       &attr, (argv) ? &argv[1] : (FAR char * const *)NULL,
                       (FAR char * const *)NULL);
    }

  if (ret != 0)
    {
      serr("ERROR: task_spawn failed: %d\n", ret);
      goto errout_with_actions;
    }

  /* Free attributes and file actions.  Ignoring return values in the case
   * of an error.
   */

  /* Return the task ID of the new task if the task was successfully
   * started.  Otherwise, ret will be ERROR (and the errno value will
   * be set appropriately).
   */

  posix_spawn_file_actions_destroy(&file_actions);
  posix_spawnattr_destroy(&attr);
  return pid;

errout_with_actions:
  posix_spawn_file_actions_destroy(&file_actions);

errout_with_attrs:
  posix_spawnattr_destroy(&attr);

errout_with_errno:
  errno = ret;
  return ERROR;
}


int adb_hal_exec(char * const argv[], adb_pipe_t *pipe, void (*on_data_cb)(adb_pipe_t*, apacket*)) {
    adb_log("entry\n");

    int ret;
    char path[16];
    int read_fd;
    int tmp_fd;
    sprintf(path, "/dev/ao%d", pipe->uid);

    tmp_fd = open(path, O_WRONLY);
    ret = open(path, O_RDONLY);
    read_fd = ret;
    adb_log("PIPE2 OPEN %d %d\n", ret, errno);

    adb_client_uv_t *client = (adb_client_uv_t*)pipe->handle.data;
    // FIXME
    ret = uv_poll_init(((uv_handle_t*)(client+1))->loop, &pipe->handle, ret);
    adb_log("poll_init %d\n", ret);

    ret = fcntl(read_fd, F_GETFD);
    fcntl(read_fd, F_SETFD, ret | FD_CLOEXEC);
    ret = fcntl(read_fd, F_GETFL);
    fcntl(read_fd, F_SETFL, ret | O_NONBLOCK);

    ret = fcntl(tmp_fd, F_GETFD);
    fcntl(tmp_fd, F_SETFD, ret | FD_CLOEXEC);

    ret = exec_builtin2(argv[0], argv, pipe);
    adb_log("exec_builtin %d %d\n", ret, errno);

    close(tmp_fd);

    if (ret < 0) {
        return -1;
    }

    ret = adb_hal_pipe_start(pipe, on_data_cb);

    /* Unlink pipes */
    unlink(path);
    path[6] = 'i';
    unlink(path);
    return 0;
}
