#ifndef APP_ERROR_H
#define APP_ERROR_H

enum error_e
{
    E_SUCCESS,
    E_ERROR,          /* General error */
    E_PERM,           /* Operation not permitted */
    E_NOENT,          /* No such file or directory */
    E_SRCH,           /* No such process */
    E_INTR,           /* Interrupted system call */
    E_IO,             /* I/O error */
    E_NXIO,           /* No such device or address */
    E_2BIG,           /* Argument list too long */
    E_NOEXEC,         /* Exec format error */
    E_BADF,           /* Bad file number */
    E_CHILD,          /* No child processes */
    E_AGAIN,          /* Try again */
    E_NOMEM,          /* Out of memory */
    E_ACCES,          /* Permission denied */
    E_FAULT,          /* Bad address */
    E_NOTBLK,         /* Block device required */
    E_BUSY,           /* Device or resource busy */
    E_EXIST,          /* File exists */
    E_XDEV,           /* Cross-device link */
    E_NODEV,          /* No such device */
    E_NOTDIR,         /* Not a directory */
    E_ISDIR,          /* Is a directory */
    E_INVAL,          /* Invalid argument */
    E_NFILE,          /* File table overflow */
    E_MFILE,          /* Too many open files */
    E_NOTTY,          /* Not a typewriter */
    E_TXTBSY,         /* Text file busy */
    E_FBIG,           /* File too large */
    E_NOSPC,          /* No space left on device */
    E_SPIPE,          /* Illegal seek */
    E_ROFS,           /* Read-only file system */
    E_MLINK,          /* Too many links */
    E_PIPE,           /* Broken pipe */
    E_DOM,            /* Math argument out of domain of func */
    E_RANGE,          /* Math result not representable */

    E_DEADLOCK,       /* Resource deadlock would occur */
    E_WOULDBLOCK,     /* Operation would block */
    E_NODATA,         /* No data available */
    E_TIME,           /* Timer expired */
    E_NOBUFS,         /* No buffer space available */

    // my own stuff
    E_NULL_PTR,
    E_END_OF_ITER,
    E_IN_PROGRESS,
    E_ALREADY_DONE,
    E_TIMEOUT,

    // config server errors
    // CONFIG_SERVER_ERROR_BASE

    E_WIFI_LOGINS_NOT_SET,
    E_WIFI_LOGINS_INVALID
};

#endif