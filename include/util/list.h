#ifndef WLR_UTIL_LIST_H
#define WLR_UTIL_LIST_H

#include <wayland-server-core.h>

#define wlr_list_get_next(pos, head, member) \
	(&pos->member == (head) ? NULL : wl_container_of((pos)->member.next, (pos), member))

#endif
