/*
 * Memory management functions.
 *
 * Copyright 2000-2007 Willy Tarreau <w@1wt.eu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <types/applet.h>
#include <types/cli.h>
#include <types/global.h>
#include <types/stats.h>

#include <common/config.h>
#include <common/debug.h>
#include <common/memory.h>
#include <common/mini-clist.h>
#include <common/standard.h>

#include <proto/applet.h>
#include <proto/cli.h>
#include <proto/channel.h>
#include <proto/log.h>
#include <proto/stream_interface.h>
#include <proto/stats.h>

static struct list pools = LIST_HEAD_INIT(pools);
int mem_poison_byte = -1;

/* Try to find an existing shared pool with the same characteristics and
 * returns it, otherwise creates this one. NULL is returned if no memory
 * is available for a new creation. Two flags are supported :
 *   - MEM_F_SHARED to indicate that the pool may be shared with other users
 *   - MEM_F_EXACT to indicate that the size must not be rounded up
 */
struct pool_head *create_pool(char *name, unsigned int size, unsigned int flags)
{
	struct pool_head *pool;
	struct pool_head *entry;
	struct list *start;
	unsigned int align;

	/* We need to store a (void *) at the end of the chunks. Since we know
	 * that the malloc() function will never return such a small size,
	 * let's round the size up to something slightly bigger, in order to
	 * ease merging of entries. Note that the rounding is a power of two.
	 * This extra (void *) is not accounted for in the size computation
	 * so that the visible parts outside are not affected.
	 */

	if (!(flags & MEM_F_EXACT)) {
		align = 16;
		size  = ((size + POOL_EXTRA + align - 1) & -align) - POOL_EXTRA;
	}

	start = &pools;
	pool = NULL;

	list_for_each_entry(entry, &pools, list) {
		if (entry->size == size) {
			/* either we can share this place and we take it, or
			 * we look for a sharable one or for the next position
			 * before which we will insert a new one.
			 */
			if (flags & entry->flags & MEM_F_SHARED) {
				/* we can share this one */
				pool = entry;
				DPRINTF(stderr, "Sharing %s with %s\n", name, pool->name);
				break;
			}
		}
		else if (entry->size > size) {
			/* insert before this one */
			start = &entry->list;
			break;
		}
	}

	if (!pool) {
		pool = CALLOC(1, sizeof(*pool));
		if (!pool)
			return NULL;
		if (name)
			strlcpy2(pool->name, name, sizeof(pool->name));
		pool->size = size;
		pool->flags = flags;
		LIST_ADDQ(start, &pool->list);
	}
	pool->users++;
	return pool;
}

/* Allocates new entries for pool <pool> until there are at least <avail> + 1
 * available, then returns the last one for immediate use, so that at least
 * <avail> are left available in the pool upon return. NULL is returned if the
 * last entry could not be allocated. It's important to note that at least one
 * allocation is always performed even if there are enough entries in the pool.
 * A call to the garbage collector is performed at most once in case malloc()
 * returns an error, before returning NULL.
 */
void *pool_refill_alloc(struct pool_head *pool, unsigned int avail)
{
	void *ptr = NULL;
	int failed = 0;

	/* stop point */
	avail += pool->used;

	while (1) {
		if (pool->limit && pool->allocated >= pool->limit)
			return NULL;

		ptr = MALLOC(pool->size + POOL_EXTRA);
		if (!ptr) {
			pool->failed++;
			if (failed)
				return NULL;
			failed++;
			pool_gc2();
			continue;
		}
		if (++pool->allocated > avail)
			break;

		*POOL_LINK(pool, ptr) = (void *)pool->free_list;
		pool->free_list = ptr;
	}
	pool->used++;
#ifdef DEBUG_MEMORY_POOLS
	/* keep track of where the element was allocated from */
	*POOL_LINK(pool, ptr) = (void *)pool;
#endif
	return ptr;
}

/*
 * This function frees whatever can be freed in pool <pool>.
 */
void pool_flush2(struct pool_head *pool)
{
	void *temp, *next;
	if (!pool)
		return;

	next = pool->free_list;
	while (next) {
		temp = next;
		next = *POOL_LINK(pool, temp);
		pool->allocated--;
		FREE(temp);
	}
	pool->free_list = next;

	/* here, we should have pool->allocate == pool->used */
}

/*
 * This function frees whatever can be freed in all pools, but respecting
 * the minimum thresholds imposed by owners. It takes care of avoiding
 * recursion because it may be called from a signal handler.
 */
void pool_gc2()
{
	static int recurse;
	struct pool_head *entry;

	if (recurse++)
		goto out;

	list_for_each_entry(entry, &pools, list) {
		void *temp, *next;
		//qfprintf(stderr, "Flushing pool %s\n", entry->name);
		next = entry->free_list;
		while (next &&
		       (int)(entry->allocated - entry->used) > (int)entry->minavail) {
			temp = next;
			next = *POOL_LINK(entry, temp);
			entry->allocated--;
			FREE(temp);
		}
		entry->free_list = next;
	}
 out:
	recurse--;
}

/*
 * This function destroys a pool by freeing it completely, unless it's still
 * in use. This should be called only under extreme circumstances. It always
 * returns NULL if the resulting pool is empty, easing the clearing of the old
 * pointer, otherwise it returns the pool.
 * .
 */
void *pool_destroy2(struct pool_head *pool)
{
	if (pool) {
		pool_flush2(pool);
		if (pool->used)
			return pool;
		pool->users--;
		if (!pool->users) {
			LIST_DEL(&pool->list);
			FREE(pool);
		}
	}
	return NULL;
}

/* This function dumps memory usage information into the trash buffer. */
void dump_pools_to_trash()
{
	struct pool_head *entry;
	unsigned long allocated, used;
	int nbpools;

	allocated = used = nbpools = 0;
	chunk_printf(&trash, "Dumping pools usage. Use SIGQUIT to flush them.\n");
	list_for_each_entry(entry, &pools, list) {
		chunk_appendf(&trash, "  - Pool %s (%d bytes) : %d allocated (%u bytes), %d used, %d failures, %d users%s\n",
			 entry->name, entry->size, entry->allocated,
		         entry->size * entry->allocated, entry->used, entry->failed,
			 entry->users, (entry->flags & MEM_F_SHARED) ? " [SHARED]" : "");

		allocated += entry->allocated * entry->size;
		used += entry->used * entry->size;
		nbpools++;
	}
	chunk_appendf(&trash, "Total: %d pools, %lu bytes allocated, %lu used.\n",
		 nbpools, allocated, used);
}

/* Dump statistics on pools usage. */
void dump_pools(void)
{
	dump_pools_to_trash();
	qfprintf(stderr, "%s", trash.str);
}

/* This function returns the total number of failed pool allocations */
int pool_total_failures()
{
	struct pool_head *entry;
	int failed = 0;

	list_for_each_entry(entry, &pools, list)
		failed += entry->failed;
	return failed;
}

/* This function returns the total amount of memory allocated in pools (in bytes) */
unsigned long pool_total_allocated()
{
	struct pool_head *entry;
	unsigned long allocated = 0;

	list_for_each_entry(entry, &pools, list)
		allocated += entry->allocated * entry->size;
	return allocated;
}

/* This function returns the total amount of memory used in pools (in bytes) */
unsigned long pool_total_used()
{
	struct pool_head *entry;
	unsigned long used = 0;

	list_for_each_entry(entry, &pools, list)
		used += entry->used * entry->size;
	return used;
}

/* This function dumps memory usage information onto the stream interface's
 * read buffer. It returns 0 as long as it does not complete, non-zero upon
 * completion. No state is used.
 */
static int cli_io_handler_dump_pools(struct appctx *appctx)
{
	struct stream_interface *si = appctx->owner;

	dump_pools_to_trash();
	if (bi_putchk(si_ic(si), &trash) == -1) {
		si_applet_cant_put(si);
		return 0;
	}
	return 1;
}

static int cli_parse_show_pools(char **args, struct appctx *appctx, void *private)
{
	return 0;
}

/* register cli keywords */
static struct cli_kw_list cli_kws = {{ },{
	{ { "show", "pools",  NULL }, "show pools     : report information about the memory pools usage", cli_parse_show_pools, cli_io_handler_dump_pools },
	{{},}
}};

#ifdef __VMS
void __memory_init(void)
#else
__attribute__((constructor))
static void __memory_init(void)
#endif
{
	cli_register_kw(&cli_kws);
}

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 */
