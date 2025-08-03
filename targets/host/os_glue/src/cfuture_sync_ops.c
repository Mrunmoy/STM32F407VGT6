#include "cfuture_sync_ops.h"

#include "adapters/cfuture_posix.h"

const cfuture_sync_ops_t *cfuture_sync_ops_get(void)
{
    return cfuture_posix_sync_ops();
}
