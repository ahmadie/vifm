/* vifm
 * Copyright (C) 2015 xaizek.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "fswatch.h"

#include <stdlib.h> /* free() malloc() */

#ifdef HAVE_INOTIFY

#include <sys/inotify.h> /* IN_* inotify_* */
#include <unistd.h> /* close() read() */

#include <errno.h> /* EAGAIN errno */
#include <stddef.h> /* NULL */
#include <stdint.h> /* uint32_t */
#include <time.h> /* time_t time() */

#include "../compat/fs_limits.h"
#include "trie.h"

/* TODO: consider implementation that could reuse already available descriptor
 *       by just removing old watch and then adding a new one. */

/* Watcher data. */
struct fswatch_t
{
	int fd;       /* File descriptor for inotify. */
	trie_t stats; /* Tree to keep track of per file frequency of notifications. */
};

/* Per file statistics information. */
typedef struct
{
	time_t last_update;  /* Time of the last change to the file. */
	time_t banned_until; /* Moment until notifications should be ignored. */
	uint32_t ban_mask;   /* Events right before the ban. */
	int count;           /* How many times file changed continuously in the last
	                        several seconds. */
}
notif_stat_t;

static int update_file_stats(fswatch_t *w, const struct inotify_event *e,
		time_t now);

fswatch_t *
fswatch_create(const char path[])
{
	int wd;

	fswatch_t *const w = malloc(sizeof(*w));
	if(w == NULL)
	{
		return NULL;
	}

	/* Create tree to collect update frequency statistics. */
	w->stats = trie_create();
	if(w->stats == NULL_TRIE)
	{
		trie_free_with_data(w->stats);
		free(w);
		return NULL;
	}

	/* Create inotify instance. */
	w->fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if(w->fd == -1)
	{
		trie_free_with_data(w->stats);
		free(w);
		return NULL;
	}

	/* Add directory to watch. */
	wd = inotify_add_watch(w->fd, path, IN_ATTRIB | IN_MODIFY | IN_CREATE |
			IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_EXCL_UNLINK |
			IN_CLOSE_WRITE);
	if(wd == -1)
	{
		close(w->fd);
		trie_free_with_data(w->stats);
		free(w);
		return NULL;
	}

	return w;
}

void
fswatch_free(fswatch_t *w)
{
	if(w != NULL)
	{
		trie_free_with_data(w->stats);
		close(w->fd);
		free(w);
	}
}

int
fswatch_changed(fswatch_t *w, int *error)
{
	enum { BUF_LEN = (10 * (sizeof(struct inotify_event) + NAME_MAX + 1)) };

	char buf[BUF_LEN];
	int nread;
	int changed;
	const time_t now = time(NULL);

	changed = 0;
	*error = 0;
	do
	{
		char *p;
		struct inotify_event *e;

		/* Receive a package of events. */
		nread = read(w->fd, buf, BUF_LEN);
		if(nread < 0)
		{
			if(errno == EAGAIN)
			{
				break;
			}

			*error = 1;
			break;
		}

		/* And process each of them separately. */
		for(p = buf; p < buf + nread; p += sizeof(struct inotify_event) + e->len)
		{
			e = (struct inotify_event *)p;
			if(update_file_stats(w, e, now))
			{
				changed = 1;
			}
		}
	}
	while(nread != 0);

	return changed;
}

/* Updates information about a file event is about.  Returns non-zero if this is
 * an interesting event that's worth attention (e.g. re-reading information from
 * file system), otherwise zero is returned. */
static int
update_file_stats(fswatch_t *w, const struct inotify_event *e, time_t now)
{
	enum { HITS_TO_BAN_AFTER = 5, BAN_SECS = 5 };

	const uint32_t IMPORTANT_EVENTS = IN_CREATE | IN_DELETE | IN_MOVED_FROM
	                                | IN_MOVED_TO;

	const char *const fname = (e->len == 0U) ? "." : e->name;
	void *data;
	notif_stat_t *stats;

	/* See if we already know this file and retrieve associated information if
	 * so. */
	if(trie_get(w->stats, fname, &data) != 0)
	{
		notif_stat_t *const stats = malloc(sizeof(*stats));
		if(stats != NULL)
		{
			stats->last_update = now;
			stats->banned_until = 0U;
			stats->count = 1;
			if(trie_set(w->stats, fname, stats) != 0)
			{
				free(stats);
			}
		}

		return 1;
	}

	stats = data;

	/* Unban entry on any of the "important" events. */
	if(e->mask & IMPORTANT_EVENTS)
	{
		stats->banned_until = 0U;
		stats->count = 1;
	}

	/* Ignore events during banned period, unless it's something new. */
	if(now < stats->banned_until && !(e->mask & ~stats->ban_mask))
	{
		return 0;
	}

	/* Treat events happened in the next second as a sequence. */
	stats->count = (now - stats->last_update <= 1U) ? (stats->count + 1) : 1;

	/* Files that cause relatively long sequence of events are banned for a
	 * while. */
	if(stats->count > HITS_TO_BAN_AFTER)
	{
		stats->ban_mask = e->mask;
		stats->banned_until = now + BAN_SECS;
	}

	stats->last_update = now;

	return 1;
}

#else

#include "filemon.h"

#include <string.h> /* strdup() */

/* Watcher data. */
struct fswatch_t
{
	filemon_t filemon; /* Stamp based monitoring. */
	char *path;        /* Path to the file being watched. */
};

fswatch_t *
fswatch_create(const char path[])
{
	fswatch_t *const w = malloc(sizeof(*w));
	if(w == NULL)
	{
		return NULL;
	}

	if(filemon_from_file(path, &w->filemon) != 0)
	{
		free(w);
		return NULL;
	}

	w->path = strdup(path);
	if(w->path == NULL)
	{
		free(w);
		return NULL;
	}

	return w;
}

void
fswatch_free(fswatch_t *w)
{
	if(w != NULL)
	{
		free(w->path);
		free(w);
	}
}

int
fswatch_changed(fswatch_t *w, int *error)
{
	int changed;

	filemon_t filemon;
	if(filemon_from_file(w->path, &filemon) != 0)
	{
		*error = 1;
		return 1;
	}

	*error = 0;
	changed = !filemon_equal(&w->filemon, &filemon);

	filemon_assign(&w->filemon, &filemon);

	return changed;
}

#endif

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
