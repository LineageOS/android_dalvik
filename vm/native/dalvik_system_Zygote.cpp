/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * dalvik.system.Zygote
 */
#include "Dalvik.h"
#include "Thread.h"
#include "native/InternalNativePriv.h"

#include <selinux/android.h>

#include <signal.h>
#if (__GNUC__ == 4 && __GNUC_MINOR__ == 7)
#include <sys/resource.h>
#endif
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <stdio.h>
#include <grp.h>
#include <errno.h>
#include <paths.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/personality.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <linux/fs.h>
#include <cutils/fs.h>
#include <cutils/sched_policy.h>
#include <cutils/multiuser.h>
#include <sched.h>
#include <sys/utsname.h>
#include <sys/capability.h>

#ifdef HAVE_ANDROID_OS
#include "fd_utils-inl.h"
#include <cutils/properties.h>
#endif

#if defined(HAVE_PRCTL)
# include <sys/prctl.h>
#endif

#define ZYGOTE_LOG_TAG "Zygote"

/* must match values in dalvik.system.Zygote */
enum {
    DEBUG_ENABLE_DEBUGGER           = 1,
    DEBUG_ENABLE_CHECKJNI           = 1 << 1,
    DEBUG_ENABLE_ASSERT             = 1 << 2,
    DEBUG_ENABLE_SAFEMODE           = 1 << 3,
    DEBUG_ENABLE_JNI_LOGGING        = 1 << 4,
};

/* must match values in dalvik.system.Zygote */
enum {
    MOUNT_EXTERNAL_NONE = 0,
    MOUNT_EXTERNAL_SINGLEUSER = 1,
    MOUNT_EXTERNAL_MULTIUSER = 2,
    MOUNT_EXTERNAL_MULTIUSER_ALL = 3,
};

/*
 * This signal handler is for zygote mode, since the zygote
 * must reap its children
 */
static void sigchldHandler(int s)
{
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        /* Log process-death status that we care about.  In general it is not
           safe to call ALOG(...) from a signal handler because of possible
           reentrancy.  However, we know a priori that the current implementation
           of ALOG() is safe to call from a SIGCHLD handler in the zygote process.
           If the ALOG() implementation changes its locking strategy or its use
           of syscalls within the lazy-init critical section, its use here may
           become unsafe. */
        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status)) {
                ALOG(LOG_DEBUG, ZYGOTE_LOG_TAG, "Process %d exited cleanly (%d)",
                    (int) pid, WEXITSTATUS(status));
            } else {
                IF_ALOGV(/*should use ZYGOTE_LOG_TAG*/) {
                    ALOG(LOG_VERBOSE, ZYGOTE_LOG_TAG,
                        "Process %d exited cleanly (%d)",
                        (int) pid, WEXITSTATUS(status));
                }
            }
        } else if (WIFSIGNALED(status)) {
            if (WTERMSIG(status) != SIGKILL) {
                ALOG(LOG_DEBUG, ZYGOTE_LOG_TAG,
                    "Process %d terminated by signal (%d)",
                    (int) pid, WTERMSIG(status));
            } else {
                IF_ALOGV(/*should use ZYGOTE_LOG_TAG*/) {
                    ALOG(LOG_VERBOSE, ZYGOTE_LOG_TAG,
                        "Process %d terminated by signal (%d)",
                        (int) pid, WTERMSIG(status));
                }
            }
#ifdef WCOREDUMP
            if (WCOREDUMP(status)) {
                ALOG(LOG_INFO, ZYGOTE_LOG_TAG, "Process %d dumped core",
                    (int) pid);
            }
#endif /* ifdef WCOREDUMP */
        }

        /*
         * If the just-crashed process is the system_server, bring down zygote
         * so that it is restarted by init and system server will be restarted
         * from there.
         */
        if (pid == gDvm.systemServerPid) {
            ALOG(LOG_INFO, ZYGOTE_LOG_TAG,
                "Exit zygote because system server (%d) has terminated",
                (int) pid);
            kill(getpid(), SIGKILL);
        }
    }

    if (pid < 0) {
        ALOG(LOG_WARN, ZYGOTE_LOG_TAG,
            "Zygote SIGCHLD error in waitpid: %s",strerror(errno));
    }
}

/*
 * configure sigchld handler for the zygote process
 * This is configured very late, because earlier in the dalvik lifecycle
 * we can fork() and exec() for the verifier/optimizer, and we
 * want to waitpid() for those rather than have them be harvested immediately.
 *
 * This ends up being called repeatedly before each fork(), but there's
 * no real harm in that.
 */
static void setSignalHandler()
{
    int err;
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = sigchldHandler;

    err = sigaction (SIGCHLD, &sa, NULL);

    if (err < 0) {
        ALOGW("Error setting SIGCHLD handler: %s", strerror(errno));
    }
}

/*
 * Set the SIGCHLD handler back to default behavior in zygote children
 */
static void unsetSignalHandler()
{
    int err;
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = SIG_DFL;

    err = sigaction (SIGCHLD, &sa, NULL);

    if (err < 0) {
        ALOGW("Error unsetting SIGCHLD handler: %s", strerror(errno));
    }
}

/*
 * Calls POSIX setgroups() using the int[] object as an argument.
 * A NULL argument is tolerated.
 */

static int setgroupsIntarray(ArrayObject* gidArray)
{
    gid_t *gids;
    u4 i;
    s4 *contents;

    if (gidArray == NULL) {
        return 0;
    }

    /* just in case gid_t and u4 are different... */
    gids = (gid_t *)alloca(sizeof(gid_t) * gidArray->length);
    contents = (s4 *)(void *)gidArray->contents;

    for (i = 0 ; i < gidArray->length ; i++) {
        gids[i] = (gid_t) contents[i];
    }

    return setgroups((size_t) gidArray->length, gids);
}

/*
 * Sets the resource limits via setrlimit(2) for the values in the
 * two-dimensional array of integers that's passed in. The second dimension
 * contains a tuple of length 3: (resource, rlim_cur, rlim_max). NULL is
 * treated as an empty array.
 *
 * -1 is returned on error.
 */
static int setrlimitsFromArray(ArrayObject* rlimits)
{
    u4 i;
    struct rlimit rlim;

    if (rlimits == NULL) {
        return 0;
    }

    memset (&rlim, 0, sizeof(rlim));

    ArrayObject** tuples = (ArrayObject **)(void *)rlimits->contents;

    for (i = 0; i < rlimits->length; i++) {
        ArrayObject * rlimit_tuple = tuples[i];
        s4* contents = (s4 *)(void *)rlimit_tuple->contents;
        int err;

        if (rlimit_tuple->length != 3) {
            ALOGE("rlimits array must have a second dimension of size 3");
            return -1;
        }

        rlim.rlim_cur = contents[1];
        rlim.rlim_max = contents[2];

        err = setrlimit(contents[0], &rlim);

        if (err < 0) {
            return -1;
        }
    }

    return 0;
}

/*
 * Create a private mount namespace and bind mount appropriate emulated
 * storage for the given user.
 */
static int mountEmulatedStorage(uid_t uid, u4 mountMode) {
    // See storage config details at http://source.android.com/tech/storage/
    userid_t userid = multiuser_get_user_id(uid);

    // Create a second private mount namespace for our process
    if (unshare(CLONE_NEWNS) == -1) {
        ALOGE("Failed to unshare(): %s", strerror(errno));
        return -1;
    }

    // Create bind mounts to expose external storage
    if (mountMode == MOUNT_EXTERNAL_MULTIUSER
            || mountMode == MOUNT_EXTERNAL_MULTIUSER_ALL) {
        // These paths must already be created by init.rc
        const char* source = getenv("EMULATED_STORAGE_SOURCE");
        const char* target = getenv("EMULATED_STORAGE_TARGET");
        const char* legacy = getenv("EXTERNAL_STORAGE");
        if (source == NULL || target == NULL || legacy == NULL) {
            ALOGE("Storage environment undefined; unable to provide external storage");
            return -1;
        }

        // Prepare source paths
        char source_user[PATH_MAX];
        char target_user[PATH_MAX];

        // /mnt/shell/emulated/0
        snprintf(source_user, PATH_MAX, "%s/%d", source, userid);
        // /storage/emulated/0
        snprintf(target_user, PATH_MAX, "%s/%d", target, userid);

        if (fs_prepare_dir(source_user, 0000, 0, 0) == -1
                || fs_prepare_dir(target_user, 0000, 0, 0) == -1) {
            return -1;
        }

        if (mountMode == MOUNT_EXTERNAL_MULTIUSER_ALL) {
            // Mount entire external storage tree for all users
            if (TEMP_FAILURE_RETRY(
                    mount(source, target, NULL, MS_BIND, NULL)) == -1) {
                ALOGE("Failed to mount %s to %s: %s", source, target, strerror(errno));
                return -1;
            }
        } else {
            // Only mount user-specific external storage
            if (TEMP_FAILURE_RETRY(
                    mount(source_user, target_user, NULL, MS_BIND, NULL)) == -1) {
                ALOGE("Failed to mount %s to %s: %s", source_user, target_user, strerror(errno));
                return -1;
            }
        }

        if (fs_prepare_dir(legacy, 0000, 0, 0) == -1) {
            return -1;
        }

        // Finally, mount user-specific path into place for legacy users
        if (TEMP_FAILURE_RETRY(
                mount(target_user, legacy, NULL, MS_BIND | MS_REC, NULL)) == -1) {
            ALOGE("Failed to mount %s to %s: %s", target_user, legacy, strerror(errno));
            return -1;
        }

    } else {
        ALOGE("Mount mode %d unsupported", mountMode);
        return -1;
    }

    return 0;
}

/* native public static int fork(); */
static void Dalvik_dalvik_system_Zygote_fork(const u4* args, JValue* pResult)
{
    pid_t pid;

    if (!gDvm.zygote) {
        dvmThrowIllegalStateException(
            "VM instance not started with -Xzygote");

        RETURN_VOID();
    }

    if (!dvmGcPreZygoteFork()) {
        ALOGE("pre-fork heap failed");
        dvmAbort();
    }

    setSignalHandler();

    dvmDumpLoaderStats("zygote");
    pid = fork();

#ifdef HAVE_ANDROID_OS
    if (pid == 0) {
        /* child process */
        extern int gMallocLeakZygoteChild;
        gMallocLeakZygoteChild = 1;
    }
#endif

    RETURN_INT(pid);
}

/*
 * Enable/disable debug features requested by the caller.
 *
 * debugger
 *   If set, enable debugging; if not set, disable debugging.  This is
 *   easy to handle, because the JDWP thread isn't started until we call
 *   dvmInitAfterZygote().
 * checkjni
 *   If set, make sure "check JNI" is enabled.
 * assert
 *   If set, make sure assertions are enabled.  This gets fairly weird,
 *   because it affects the result of a method called by class initializers,
 *   and hence can't affect pre-loaded/initialized classes.
 * safemode
 *   If set, operates the VM in the safe mode. The definition of "safe mode" is
 *   implementation dependent and currently only the JIT compiler is disabled.
 *   This is easy to handle because the compiler thread and associated resources
 *   are not requested until we call dvmInitAfterZygote().
 */
static void enableDebugFeatures(u4 debugFlags)
{
    ALOGV("debugFlags is 0x%02x", debugFlags);

    gDvm.jdwpAllowed = ((debugFlags & DEBUG_ENABLE_DEBUGGER) != 0);

    if ((debugFlags & DEBUG_ENABLE_CHECKJNI) != 0) {
        /* turn it on if it's not already enabled */
        dvmLateEnableCheckedJni();
    }

    if ((debugFlags & DEBUG_ENABLE_JNI_LOGGING) != 0) {
        gDvmJni.logThirdPartyJni = true;
    }

    if ((debugFlags & DEBUG_ENABLE_ASSERT) != 0) {
        /* turn it on if it's not already enabled */
        dvmLateEnableAssertions();
    }

    if ((debugFlags & DEBUG_ENABLE_SAFEMODE) != 0) {
#if defined(WITH_JIT)
        /* turn off the jit if it is explicitly requested by the app */
        if (gDvm.executionMode == kExecutionModeJit)
            gDvm.executionMode = kExecutionModeInterpFast;
#endif
    }

#ifdef HAVE_ANDROID_OS
    if ((debugFlags & DEBUG_ENABLE_DEBUGGER) != 0) {
        /* To let a non-privileged gdbserver attach to this
         * process, we must set its dumpable bit flag. However
         * we are not interested in generating a coredump in
         * case of a crash, so also set the coredump size to 0
         * to disable that
         */
        if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) < 0) {
            ALOGE("could not set dumpable bit flag for pid %d: %s",
                 getpid(), strerror(errno));
        } else {
            char prop_value[PROPERTY_VALUE_MAX];
            property_get("persist.debug.trace",prop_value,"0");
            struct rlimit rl;
            if(prop_value[0] == '1') {
                ALOGE("setting RLIM to infinity for process %d",getpid());
                rl.rlim_cur = RLIM_INFINITY;
            } else {
                rl.rlim_cur = 0;
            }
            rl.rlim_max = RLIM_INFINITY;
            if (setrlimit(RLIMIT_CORE, &rl) < 0) {
                ALOGE("could not disable core file generation for pid %d: %s",
                    getpid(), strerror(errno));
            }
        }
    }
#endif
}

/*
 * Set Linux capability flags.
 *
 * Returns 0 on success, errno on failure.
 */
static int setCapabilities(int64_t permitted, int64_t effective)
{
#ifdef HAVE_ANDROID_OS
    struct __user_cap_header_struct capheader;
    struct __user_cap_data_struct capdata[_LINUX_CAPABILITY_U32S_3];

    memset(&capheader, 0, sizeof(capheader));
    memset(&capdata, 0, sizeof(capdata));

    capheader.version = _LINUX_CAPABILITY_VERSION_3;
    capheader.pid = 0;

    capdata[0].effective = effective & 0xffffffffULL;
    capdata[0].permitted = permitted & 0xffffffffULL;
    capdata[1].effective = (uint64_t)effective >> 32;
    capdata[1].permitted = (uint64_t)permitted >> 32;

    ALOGV("CAPSET perm=%llx eff=%llx", permitted, effective);
    if (capset(&capheader, capdata) != 0)
        return errno;
#endif /*HAVE_ANDROID_OS*/

    return 0;
}

/*
 * Set SELinux security context.
 *
 * Returns 0 on success, -1 on failure.
 */
static int setSELinuxContext(uid_t uid, bool isSystemServer,
                             const char *seInfo, const char *niceName)
{
#ifdef HAVE_ANDROID_OS
    return selinux_android_setcontext(uid, isSystemServer, seInfo, niceName);
#else
    return 0;
#endif
}

static bool needsNoRandomizeWorkaround() {
#if !defined(__arm__)
    return false;
#else
    int major;
    int minor;
    struct utsname uts;
    if (uname(&uts) == -1) {
        return false;
    }

    if (sscanf(uts.release, "%d.%d", &major, &minor) != 2) {
        return false;
    }

    // Kernels before 3.4.* need the workaround.
    return (major < 3) || ((major == 3) && (minor < 4));
#endif
}

#ifdef HAVE_ANDROID_OS

// Utility to close down the Zygote socket file descriptors while
// the child is still running as root with Zygote's privileges.  Each
// descriptor (if any) is closed via dup2(), replacing it with a valid
// (open) descriptor to /dev/null.

static void detachDescriptors(ArrayObject* fdsToClose) {
    if (!fdsToClose) {
        return;
    }
    size_t count = fdsToClose->length;
    int *ar = (int *) (void *) fdsToClose->contents;
    if (!ar) {
        ALOG(LOG_ERROR, ZYGOTE_LOG_TAG, "Bad fd array");
        dvmAbort();
    }
    size_t i;
    int devnull;
    for (i = 0; i < count; i++) {
        if (ar[1] < 0) {
            continue;
        }
        devnull = open("/dev/null", O_RDWR);
        if (devnull < 0) {
            ALOG(LOG_ERROR, ZYGOTE_LOG_TAG, "Failed to open /dev/null");
            dvmAbort();
        }
        ALOG(LOG_VERBOSE, ZYGOTE_LOG_TAG, "Switching descriptor %d to /dev/null", ar[i]);
        if (dup2(devnull, ar[i]) < 0) {
            ALOG(LOG_ERROR, ZYGOTE_LOG_TAG, "Failed dup2() on descriptor %d", ar[i]);
            dvmAbort();
        }
        close(devnull);
    }
}

#endif

/*
 * Basic KSM Support
 */
#ifndef MADV_MERGEABLE
#define MADV_MERGEABLE 12
#endif

static inline void pushAnonymousPagesToKSM(void)
{
    FILE *fp;
    char section[100];
    char perms[5];
    unsigned long start, end, misc;
    int ch, offset;

    fp = fopen("/proc/self/maps","r");

    if (fp != NULL) {
        while (fscanf(fp, "%lx-%lx %4s %lx %lx:%lx %ld",
                 &start, &end, perms, &misc, &misc, &misc, &misc) == 7)
        {
            /* Read the sections name striping any preceeding spaces
               and truncating to 100char (99 + \0)*/
            section[0] = 0;
            offset = 0;
            while(1)
            {
                ch = fgetc(fp);
                if (ch == '\n' || ch == EOF) {
                    break;
                }
                if ((offset == 0) && (ch == ' ')) {
                    continue;
                }
                if ((offset + 1) < 100) {
                    section[offset]=ch;
                    section[offset+1]=0;
                    offset++;
                }
            }
            /* now decide if we want to scan the section or not:
               for now we scan Anonymous (sections with no file name) stack and
               heap sections*/
            if (( section[0] == 0) ||
               (strcmp(section,"[stack]") == 0) ||
               (strcmp(section,"[heap]") == 0))
            {
                /* The section matches pass it into madvise */
                madvise((void*) start, (size_t) end-start, MADV_MERGEABLE);
            }
            if (ch == EOF) {
                break;
            }
        }
        fclose(fp);
    }
}

#ifdef HAVE_ANDROID_OS
// The list of open zygote file descriptors.
static FileDescriptorTable* gOpenFdTable = NULL;
#endif

/*
 * Utility routine to fork zygote and specialize the child process.
 */
static pid_t forkAndSpecializeCommon(const u4* args, bool isSystemServer, bool legacyFork)
{
    pid_t pid;

    uid_t uid = (uid_t) args[0];
    gid_t gid = (gid_t) args[1];
    ArrayObject* gids = (ArrayObject *)args[2];
    u4 debugFlags = args[3];
    ArrayObject *rlimits = (ArrayObject *)args[4];
    ArrayObject *fdsToClose = NULL;
    u4 mountMode = MOUNT_EXTERNAL_NONE;
    int64_t permittedCapabilities, effectiveCapabilities;
    char *seInfo = NULL;
    char *niceName = NULL;

    if (isSystemServer) {
        /*
         * Don't use GET_ARG_LONG here for now.  gcc is generating code
         * that uses register d8 as a temporary, and that's coming out
         * scrambled in the child process.  b/3138621
         */
        //permittedCapabilities = GET_ARG_LONG(args, 5);
        //effectiveCapabilities = GET_ARG_LONG(args, 7);
        permittedCapabilities = args[5] | (int64_t) args[6] << 32;
        effectiveCapabilities = args[7] | (int64_t) args[8] << 32;
    } else {
        mountMode = args[5];
        permittedCapabilities = effectiveCapabilities = 0;
        StringObject* seInfoObj = (StringObject*)args[6];
        if (seInfoObj) {
            seInfo = dvmCreateCstrFromString(seInfoObj);
            if (!seInfo) {
                ALOGE("seInfo dvmCreateCstrFromString failed");
                dvmAbort();
            }
        }
        StringObject* niceNameObj = (StringObject*)args[7];
        if (niceNameObj) {
            niceName = dvmCreateCstrFromString(niceNameObj);
            if (!niceName) {
                ALOGE("niceName dvmCreateCstrFromString failed");
                dvmAbort();
            }
        }
        if (!legacyFork) {
          fdsToClose = (ArrayObject *)args[8];
        }
    }

    if (!gDvm.zygote) {
        dvmThrowIllegalStateException(
            "VM instance not started with -Xzygote");

        return -1;
    }

    if (!dvmGcPreZygoteFork()) {
        ALOGE("pre-fork heap failed");
        dvmAbort();
    }

    setSignalHandler();

    dvmDumpLoaderStats("zygote");

    sigset_t sigchld;
    sigemptyset(&sigchld);
    sigaddset(&sigchld, SIGCHLD);

    // Temporarily block SIGCHLD during forks. The SIGCHLD handler might
    // log, which would result in the logging FDs we close being
    // reopened.
    // This would cause failures because the FDs are not whitelisted.
    //
    // Note that the zygote process is single threaded at this
    // point.
    if (sigprocmask(SIG_BLOCK, &sigchld, NULL) == -1) {
        ALOGE("sigprocmask(SIG_SETMASK, { SIGCHLD }) failed:  %s", strerror(errno));
        dvmAbort();
    }


    // Close any logging related FDs before we start evaluating the list of
    // file descriptors.
    __android_log_close();

#ifdef HAVE_ANDROID_OS
    // If this is the first fork for this zygote, create the open FD table.
    // If it isn't, we just need to check whether the list of open files
    // has changed (and it shouldn't in the normal case).
    if (gOpenFdTable == NULL) {
        gOpenFdTable = FileDescriptorTable::Create();
        if (gOpenFdTable == NULL) {
            ALOGE("Unable to construct file descriptor table.");
            dvmAbort();
        }
    } else if (!gOpenFdTable->Restat()) {
        ALOGE("Unable to restat file descriptor table.");
        dvmAbort();
    }
#endif

    pid = fork();

    if (pid == 0) {
        int err;
        /* The child process */

#ifdef HAVE_ANDROID_OS
        extern int gMallocLeakZygoteChild;
        gMallocLeakZygoteChild = 1;

        // Unhook from the Zygote sockets immediately

        detachDescriptors(fdsToClose);

        /* keep caps across UID change, unless we're staying root */
        if (uid != 0) {
            err = prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0);

            if (err < 0) {
                ALOGE("cannot PR_SET_KEEPCAPS: %s", strerror(errno));
                dvmAbort();
            }
        }

        for (int i = 0; prctl(PR_CAPBSET_READ, i, 0, 0, 0) >= 0; i++) {
            err = prctl(PR_CAPBSET_DROP, i, 0, 0, 0);
            if (err < 0) {
                if (errno == EINVAL) {
                    ALOGW("PR_CAPBSET_DROP %d failed: %s. "
                          "Please make sure your kernel is compiled with "
                          "file capabilities support enabled.",
                          i, strerror(errno));
                } else {
                    ALOGE("PR_CAPBSET_DROP %d failed: %s.", i, strerror(errno));
                    dvmAbort();
                }
            }
        }

        // Re-open all remaining open file descriptors so that they aren't
        // shared with the zygote across a fork.
        if (!gOpenFdTable->ReopenOrDetach()) {
            ALOGE("Unable to reopen whitelisted descriptors.");
            dvmAbort();
        }

        if (sigprocmask(SIG_UNBLOCK, &sigchld, NULL) == -1) {
            ALOGE("sigprocmask(SIG_SETMASK, { SIGCHLD }) failed: %s", strerror(errno));
            dvmAbort();
        }
#endif /* HAVE_ANDROID_OS */

        if (mountMode != MOUNT_EXTERNAL_NONE) {
            err = mountEmulatedStorage(uid, mountMode);
            if (err < 0) {
                ALOGE("cannot mountExternalStorage(): %s", strerror(errno));

                if (errno == ENOTCONN || errno == EROFS) {
                    // When device is actively encrypting, we get ENOTCONN here
                    // since FUSE was mounted before the framework restarted.
                    // When encrypted device is booting, we get EROFS since
                    // FUSE hasn't been created yet by init.
                    // In either case, continue without external storage.
                } else {
                    dvmAbort();
                }
            }
        }

        err = setgroupsIntarray(gids);
        if (err < 0) {
            ALOGE("cannot setgroups(): %s", strerror(errno));
            dvmAbort();
        }

        err = setrlimitsFromArray(rlimits);
        if (err < 0) {
            ALOGE("cannot setrlimit(): %s", strerror(errno));
            dvmAbort();
        }

        err = setresgid(gid, gid, gid);
        if (err < 0) {
            ALOGE("cannot setresgid(%d): %s", gid, strerror(errno));
            dvmAbort();
        }

        err = setresuid(uid, uid, uid);
        if (err < 0) {
            ALOGE("cannot setresuid(%d): %s", uid, strerror(errno));
            dvmAbort();
        }

        if (needsNoRandomizeWorkaround()) {
            int current = personality(0xffffFFFF);
            int success = personality((ADDR_NO_RANDOMIZE | current));
            if (success == -1) {
                ALOGW("Personality switch failed. current=%d error=%d\n", current, errno);
            }
        }

        err = setCapabilities(permittedCapabilities, effectiveCapabilities);
        if (err != 0) {
            ALOGE("cannot set capabilities (%llx,%llx): %s",
                permittedCapabilities, effectiveCapabilities, strerror(err));
            dvmAbort();
        }

        err = set_sched_policy(0, SP_DEFAULT);
        if (err < 0) {
            ALOGE("cannot set_sched_policy(0, SP_DEFAULT): %s", strerror(-err));
            dvmAbort();
        }

        err = setSELinuxContext(uid, isSystemServer, seInfo, niceName);
        if (err < 0) {
            ALOGE("cannot set SELinux context: %s\n", strerror(errno));
            dvmAbort();
        }

        // Set the comm to a nicer name.
        if (isSystemServer && niceName == NULL) {
            dvmSetThreadName("system_server");
        } else {
            dvmSetThreadName(niceName);
        }
        // These free(3) calls are safe because we know we're only ever forking
        // a single-threaded process, so we know no other thread held the heap
        // lock when we forked.
        free(seInfo);
        free(niceName);

        /*
         * Our system thread ID has changed.  Get the new one.
         */
        Thread* thread = dvmThreadSelf();
        thread->systemTid = dvmGetSysThreadId();

        /* configure additional debug options */
        enableDebugFeatures(debugFlags);

        unsetSignalHandler();
        gDvm.zygote = false;
        if (!dvmInitAfterZygote()) {
            ALOGE("error in post-zygote initialization");
            dvmAbort();
        }
        pushAnonymousPagesToKSM();
    } else if (pid > 0) {
        /* the parent process */
        free(seInfo);
        free(niceName);

        // We blocked SIGCHLD prior to a fork, we unblock it here.
        if (sigprocmask(SIG_UNBLOCK, &sigchld, NULL) == -1) {
            ALOGE("sigprocmask(SIG_SETMASK, { SIGCHLD }) failed: %s", strerror(errno));
            dvmAbort();
        }

    }

    return pid;
}

/*
 * We must expose both flavors of the native forking code for a while,
 * as this Dalvik change must be reviewed and checked in before the
 * libcore and frameworks changes.  The legacy fork API function and
 * registration can be removed later.
 */

/*
 * native public static int nativeForkAndSpecialize(int uid, int gid,
 *     int[] gids, int debugFlags, int[][] rlimits, int mountExternal,
 *     String seInfo, String niceName, int[] fdsToClose);
 */
static void Dalvik_dalvik_system_Zygote_forkAndSpecialize(const u4* args,
    JValue* pResult)
{
    pid_t pid;

    pid = forkAndSpecializeCommon(args, false, false);

    RETURN_INT(pid);
}

/*
 * native public static int nativeForkSystemServer(int uid, int gid,
 *     int[] gids, int debugFlags, int[][] rlimits,
 *     long permittedCapabilities, long effectiveCapabilities);
 */
static void Dalvik_dalvik_system_Zygote_forkSystemServer(
        const u4* args, JValue* pResult)
{
    pid_t pid;
    pid = forkAndSpecializeCommon(args, true, true);

    /* The zygote process checks whether the child process has died or not. */
    if (pid > 0) {
        int status;

        ALOGI("System server process %d has been created", pid);
        gDvm.systemServerPid = pid;
        /* There is a slight window that the system server process has crashed
         * but it went unnoticed because we haven't published its pid yet. So
         * we recheck here just to make sure that all is well.
         */
        if (waitpid(pid, &status, WNOHANG) == pid) {
            ALOGE("System server process %d has died. Restarting Zygote!", pid);
            kill(getpid(), SIGKILL);
        }
    }
    RETURN_INT(pid);
}

const DalvikNativeMethod dvm_dalvik_system_Zygote[] = {
    { "nativeFork", "()I",
      Dalvik_dalvik_system_Zygote_fork },
    { "nativeForkAndSpecialize", "(II[II[[IILjava/lang/String;Ljava/lang/String;[I)I",
      Dalvik_dalvik_system_Zygote_forkAndSpecialize },
    { "nativeForkSystemServer", "(II[II[[IJJ)I",
      Dalvik_dalvik_system_Zygote_forkSystemServer },
    { NULL, NULL, NULL },
};
