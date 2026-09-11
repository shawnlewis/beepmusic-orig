#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "beepupdate.h"


static UpdateCode run_script(const char *file, int *status, bool lua) {
    pid_t pid;
    siginfo_t cinfo;
    char *argv[3] = {};
    FILE *child_stdout;
    char *script_name;
    char *t;
    int pipefd[2];
    int i;
    UpdateCode code = UPDATE_ERR;

    argv[0] = bstrdup(lua ? "/usr/bin/lua" : "/bin/sh");
    argv[1] = bstrdup(file);
    argv[2] = NULL;

    for (i = 0; i < (sizeof(argv) / sizeof(argv[0])) - 1; i++) {
        LOG_DEBUG("argv[%d]: %p %s", i, argv[i], argv[i]);
        if (!argv[i]) {
            code = UPDATE_OOM;
            goto done;
        }
    }

    t = script_name = argv[1];
    while(*t) {
        if (*t == '/')
            script_name = t;
        t++;
    }
    // Only increment if it's pointing at / which may not be the case if
    // file is a string without /.
    if (*script_name == '/')
        script_name++;

    if (pipe2(pipefd, 0) == -1) {
        LOG_ERROR("pipe failed: %s", strerror(errno));
        goto done;
    }

    pid = fork();
    if (pid < 0) {
        LOG_ERROR("fork failed: %s", strerror(errno));
        goto done;
    } else if (pid == 0) {
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) == -1 ||
            dup2(pipefd[1], STDERR_FILENO) == -1) {
            LOG_ERROR("dup2 failed: %s", strerror(errno));
            _exit(-1);
        }

        execv(argv[0], argv);
        LOG_ERROR("execv failed: %s", strerror(errno));
        _exit(-1);
    }

    // Note: lua does not flush for every call to print or io.write even when
    // a newline is used.
    close(pipefd[1]);
    child_stdout = fdopen(pipefd[0], "r");
    if (child_stdout) {
        char *line = NULL;
        size_t alloc_size;
        ssize_t line_size;
        while ((line_size = getline(&line, &alloc_size, child_stdout)) != -1) {
            if (line[line_size - 1] == '\n')
                line[line_size - 1] = '\0';
            LOG_DEBUG("%s: %s", script_name, line);
        }
        if (line)
            free(line);
        fclose(child_stdout);
    } else {
        LOG_ERROR("fdopen failed: %s", strerror(errno));
    }

    if (waitid(P_PID, pid, &cinfo, WEXITED) == -1) {
        LOG_ERROR("waitid failed: %s", strerror(errno));
        goto done;
    }

    LOG_DEBUG("si_pid: %d si_uid: %d si_signo: %d si_status: %d si_code: %d",
        cinfo.si_pid, cinfo.si_uid, cinfo.si_signo, cinfo.si_status,
        cinfo.si_code);

    // UPDATE_SCRIPT_TERM is only if either sh or lua terminates abnormally.
    // If either one calls another program and that fails it will still exit
    // normally.  If this function returns UPDATE_SCRIPT_TERM the value in
    // status will be the signal number.
    *status = cinfo.si_status;
    if (cinfo.si_code == CLD_EXITED) {
        if (cinfo.si_status)
            code = UPDATE_SCRIPT_NONZERO;
        else
            code = UPDATE_OK;
    } else {
        LOG_ERROR("script exited abnormally with code: %d", cinfo.si_code);
        code = UPDATE_SCRIPT_TERM;
    }

done:
    for (i = 0; i < (sizeof(argv) / sizeof(argv[0])) - 1; i++) {
        if (argv[i])
            bfree(argv[i]);
    }

    return code;
}

static UpdateCode run_script_file(const char *file, int *status,
        FileType ftype) {
    FILE *stream;
    int rstatus;
    UpdateCode code = UPDATE_FILE_ERR;

    if (!file)
        return UPDATE_BAD_ARG;

    switch (ftype) {
    case FILE_TYPE_LUA:
    case FILE_TYPE_SH:
        break;
    default:
        return UPDATE_BAD_ARG;
    }

    stream = fopen(file, "r");
    if (stream) {
        code = verify_embedded_file(stream, ftype, false, NULL);
        fclose(stream);
    }

    if (code == UPDATE_OK)
        code = run_script(file, &rstatus,
                ftype == FILE_TYPE_LUA ? true : false);

    if (status)
        *status = rstatus;

    return code;
}

static UpdateCode run_script_mem(const void *data, size_t size, int *status,
        FileType ftype) {
    FILE *stream = NULL;
    char *file = NULL;
    int rstatus;
    UpdateCode code;

    if (!data || !size)
        return UPDATE_BAD_ARG;

    switch (ftype) {
    case FILE_TYPE_LUA:
    case FILE_TYPE_SH:
        break;
    default:
        return UPDATE_BAD_ARG;
    }

    code = verify_embedded_mem(data, size, ftype, false, NULL);
    if (code == UPDATE_OK) {
        stream = tmpfopen("w+");
        if (stream) {
            if (fwrite(data, 1, size, stream) == size) {
                file = tmpfpath(stream);
                fseek(stream, 0, SEEK_SET);
            }
        } else {
            code = UPDATE_FILE_ERR;
        }
    }

    if (file) {
        code = run_script(file, &rstatus,
                ftype == FILE_TYPE_LUA ? true : false);
        bfree(file);
        if (status)
            *status = rstatus;
    }

    if (stream)
        tmpfunclose(stream);

    return code;
}

UpdateCode run_shell_script_file(const char *file, int *status) {
    return run_script_file(file, status, FILE_TYPE_SH);
}

UpdateCode run_shell_script_mem(const void *data, size_t size, int *status) {
    return run_script_mem(data, size, status, FILE_TYPE_SH);
}

UpdateCode run_lua_script_file(const char *file, int *status) {
    return run_script_file(file, status, FILE_TYPE_LUA);
}

UpdateCode run_lua_script_mem(const void *data, size_t size, int *status) {
    return run_script_mem(data, size, status, FILE_TYPE_LUA);
}
