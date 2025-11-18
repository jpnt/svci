#pragma once
typedef enum {
	SVC_OK = 0,
	SVC_ERR_INVALID = -1,
	SVC_ERR_NOTFOUND = -2,
	SVC_ERR_UNSUPPORTED = -3,
	SVC_ERR_BACKEND = -4,
	SVC_ERR_TIMEOUT = -5,
	SVC_ERR_PERMISSION = -6,
	SVC_ERR_ENV = -7,
	SVC_ERR_IO = -8,
	SVC_ERR_UNKNOWN = -9
} svc_rc;

// svc_rc svc_enable(const char *);
// svc_rc svc_disable(const char *);
svc_rc svc_start(const char *);
svc_rc svc_stop(const char *);
svc_rc svc_restart(const char *);
svc_rc svc_list(void); // prints to stdout
