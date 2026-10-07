
#define CREATE_TRACE_POINTS
#include "trace.h"

#ifdef CONFIG_SYNO_NFSD_TRACE
EXPORT_TRACEPOINT_SYMBOL(syno_nfsd4_dispatch);
EXPORT_TRACEPOINT_SYMBOL(syno_nfsd_dispatch);
#endif /* CONFIG_SYNO_NFSD_TRACE */