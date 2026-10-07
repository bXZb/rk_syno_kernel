// SPDX-License-Identifier: GPL-2.0
/*
 * Tty buffer allocation management
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/ratelimit.h>
#ifdef CONFIG_SYNO_MICROP_COMMAND_V2
#include <linux/interrupt.h>
#include <linux/synobios.h>
#include <linux/kfifo.h>
#include <linux/spinlock.h>
#include <linux/syno_microp.h>
#endif /* CONFIG_SYNO_MICROP_COMMAND_V2 */


#define MIN_TTYB_SIZE	256
#define TTYB_ALIGN_MASK	255

#ifdef CONFIG_SYNO_MICROP_COMMAND_V2

static DEFINE_KFIFO(syno_microp_fifo, unsigned char, SYNO_MICROP_FIFO_SIZE);
static DEFINE_SPINLOCK(syno_microp_fifo_lock);

/* Force write to kfifo, drop old data if full */
static int syno_microp_fifo_force_in(const unsigned char *buf, unsigned int len)
{
	unsigned long flags = 0;
	int ret = -1;
	unsigned int need_space = 0;
	unsigned char dummy[256] = {0};
	unsigned int to_remove = 0;
	unsigned int removed = 0;
	unsigned int available = 0;

	if (!buf || 0 == len || len > SYNO_MICROP_FIFO_SIZE) {
		goto END;
	}
	spin_lock_irqsave(&syno_microp_fifo_lock, flags);

	/* If space is not enough, clear some old data */
	available = kfifo_avail(&syno_microp_fifo);
	if (available < len) {
		need_space = len - available;

		while (need_space > 0 && !kfifo_is_empty(&syno_microp_fifo)) {
			to_remove = min(need_space, (unsigned int)(sizeof(dummy)));
			removed = kfifo_out(&syno_microp_fifo, dummy, to_remove);
			need_space -= removed;
			if (removed == 0) break; /* prevent infinite loop */
		}
	}

	ret = kfifo_in(&syno_microp_fifo, buf, len);
	spin_unlock_irqrestore(&syno_microp_fifo_lock, flags);

END:
	return ret;
}

/* Read data from kfifo */
int syno_microp_kfifo_read(unsigned char *buf, int buf_size)
{
	unsigned long flags = 0;
	int copied = -1;
	unsigned int kfifo_len = 0;

	if (!buf || buf_size <= 0) {
		goto END;
	}

	spin_lock_irqsave(&syno_microp_fifo_lock, flags);
	kfifo_len = kfifo_len(&syno_microp_fifo);
	copied = kfifo_out(&syno_microp_fifo, buf, buf_size);
	spin_unlock_irqrestore(&syno_microp_fifo_lock, flags);

END:
	return copied;
}
EXPORT_SYMBOL(syno_microp_kfifo_read);

/* Module cleanup */
void syno_microp_kfifo_cleanup(void)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&syno_microp_fifo_lock, flags);
	kfifo_reset(&syno_microp_fifo);
	spin_unlock_irqrestore(&syno_microp_fifo_lock, flags);
}
EXPORT_SYMBOL(syno_microp_kfifo_cleanup);
#endif /* CONFIG_SYNO_MICROP_COMMAND_V2 */

/*
 * Byte threshold to limit memory consumption for flip buffers.
 * The actual memory limit is > 2x this amount.
 */
#define TTYB_DEFAULT_MEM_LIMIT	(640 * 1024UL)

/*
 * We default to dicing tty buffer allocations to this many characters
 * in order to avoid multiple page allocations. We know the size of
 * tty_buffer itself but it must also be taken into account that the
 * the buffer is 256 byte aligned. See tty_buffer_find for the allocation
 * logic this must match
 */

#define TTY_BUFFER_PAGE	(((PAGE_SIZE - sizeof(struct tty_buffer)) / 2) & ~0xFF)

/**
 *	tty_buffer_lock_exclusive	-	gain exclusive access to buffer
 *	tty_buffer_unlock_exclusive	-	release exclusive access
 *
 *	@port: tty port owning the flip buffer
 *
 *	Guarantees safe use of the line discipline's receive_buf() method by
 *	excluding the buffer work and any pending flush from using the flip
 *	buffer. Data can continue to be added concurrently to the flip buffer
 *	from the driver side.
 *
 *	On release, the buffer work is restarted if there is data in the
 *	flip buffer
 */

void tty_buffer_lock_exclusive(struct tty_port *port)
{
	struct tty_bufhead *buf = &port->buf;

	atomic_inc(&buf->priority);
	mutex_lock(&buf->lock);
}
EXPORT_SYMBOL_GPL(tty_buffer_lock_exclusive);

void tty_buffer_unlock_exclusive(struct tty_port *port)
{
	struct tty_bufhead *buf = &port->buf;
	int restart;

	restart = buf->head->commit != buf->head->read;

	atomic_dec(&buf->priority);
	mutex_unlock(&buf->lock);
	if (restart)
		queue_work(system_unbound_wq, &buf->work);
}
EXPORT_SYMBOL_GPL(tty_buffer_unlock_exclusive);

/**
 *	tty_buffer_space_avail	-	return unused buffer space
 *	@port: tty port owning the flip buffer
 *
 *	Returns the # of bytes which can be written by the driver without
 *	reaching the buffer limit.
 *
 *	Note: this does not guarantee that memory is available to write
 *	the returned # of bytes (use tty_prepare_flip_string_xxx() to
 *	pre-allocate if memory guarantee is required).
 */

int tty_buffer_space_avail(struct tty_port *port)
{
	int space = port->buf.mem_limit - atomic_read(&port->buf.mem_used);
	return max(space, 0);
}
EXPORT_SYMBOL_GPL(tty_buffer_space_avail);

static void tty_buffer_reset(struct tty_buffer *p, size_t size)
{
	p->used = 0;
	p->size = size;
	p->next = NULL;
	p->commit = 0;
	p->read = 0;
	p->flags = 0;
}

/**
 *	tty_buffer_free_all		-	free buffers used by a tty
 *	@port: tty port to free from
 *
 *	Remove all the buffers pending on a tty whether queued with data
 *	or in the free ring. Must be called when the tty is no longer in use
 */

void tty_buffer_free_all(struct tty_port *port)
{
	struct tty_bufhead *buf = &port->buf;
	struct tty_buffer *p, *next;
	struct llist_node *llist;
	unsigned int freed = 0;
	int still_used;

	while ((p = buf->head) != NULL) {
		buf->head = p->next;
		freed += p->size;
		if (p->size > 0)
			kfree(p);
	}
	llist = llist_del_all(&buf->free);
	llist_for_each_entry_safe(p, next, llist, free)
		kfree(p);

	tty_buffer_reset(&buf->sentinel, 0);
	buf->head = &buf->sentinel;
	buf->tail = &buf->sentinel;

	still_used = atomic_xchg(&buf->mem_used, 0);
	WARN(still_used != freed, "we still have not freed %d bytes!",
			still_used - freed);
}

/**
 *	tty_buffer_alloc	-	allocate a tty buffer
 *	@port: tty port
 *	@size: desired size (characters)
 *
 *	Allocate a new tty buffer to hold the desired number of characters.
 *	We round our buffers off in 256 character chunks to get better
 *	allocation behaviour.
 *	Return NULL if out of memory or the allocation would exceed the
 *	per device queue
 */

static struct tty_buffer *tty_buffer_alloc(struct tty_port *port, size_t size)
{
	struct llist_node *free;
	struct tty_buffer *p;

	/* Round the buffer size out */
	size = __ALIGN_MASK(size, TTYB_ALIGN_MASK);

	if (size <= MIN_TTYB_SIZE) {
		free = llist_del_first(&port->buf.free);
		if (free) {
			p = llist_entry(free, struct tty_buffer, free);
			goto found;
		}
	}

	/* Should possibly check if this fails for the largest buffer we
	   have queued and recycle that ? */
	if (atomic_read(&port->buf.mem_used) > port->buf.mem_limit)
		return NULL;
	p = kmalloc(sizeof(struct tty_buffer) + 2 * size, GFP_ATOMIC);
	if (p == NULL)
		return NULL;

found:
	tty_buffer_reset(p, size);
	atomic_add(size, &port->buf.mem_used);
	return p;
}

/**
 *	tty_buffer_free		-	free a tty buffer
 *	@port: tty port owning the buffer
 *	@b: the buffer to free
 *
 *	Free a tty buffer, or add it to the free list according to our
 *	internal strategy
 */

static void tty_buffer_free(struct tty_port *port, struct tty_buffer *b)
{
	struct tty_bufhead *buf = &port->buf;

	/* Dumb strategy for now - should keep some stats */
	WARN_ON(atomic_sub_return(b->size, &buf->mem_used) < 0);

	if (b->size > MIN_TTYB_SIZE)
		kfree(b);
	else if (b->size > 0)
		llist_add(&b->free, &buf->free);
}

/**
 *	tty_buffer_flush		-	flush full tty buffers
 *	@tty: tty to flush
 *	@ld:  optional ldisc ptr (must be referenced)
 *
 *	flush all the buffers containing receive data. If ld != NULL,
 *	flush the ldisc input buffer.
 *
 *	Locking: takes buffer lock to ensure single-threaded flip buffer
 *		 'consumer'
 */

void tty_buffer_flush(struct tty_struct *tty, struct tty_ldisc *ld)
{
	struct tty_port *port = tty->port;
	struct tty_bufhead *buf = &port->buf;
	struct tty_buffer *next;

	atomic_inc(&buf->priority);

	mutex_lock(&buf->lock);
	/* paired w/ release in __tty_buffer_request_room; ensures there are
	 * no pending memory accesses to the freed buffer
	 */
	while ((next = smp_load_acquire(&buf->head->next)) != NULL) {
		tty_buffer_free(port, buf->head);
		buf->head = next;
	}
	buf->head->read = buf->head->commit;

	if (ld && ld->ops->flush_buffer)
		ld->ops->flush_buffer(tty);

	atomic_dec(&buf->priority);
	mutex_unlock(&buf->lock);
}

/**
 *	tty_buffer_request_room		-	grow tty buffer if needed
 *	@port: tty port
 *	@size: size desired
 *	@flags: buffer flags if new buffer allocated (default = 0)
 *
 *	Make at least size bytes of linear space available for the tty
 *	buffer. If we fail return the size we managed to find.
 *
 *	Will change over to a new buffer if the current buffer is encoded as
 *	TTY_NORMAL (so has no flags buffer) and the new buffer requires
 *	a flags buffer.
 */
static int __tty_buffer_request_room(struct tty_port *port, size_t size,
				     int flags)
{
	struct tty_bufhead *buf = &port->buf;
	struct tty_buffer *b, *n;
	int left, change;

	b = buf->tail;
	if (b->flags & TTYB_NORMAL)
		left = 2 * b->size - b->used;
	else
		left = b->size - b->used;

	change = (b->flags & TTYB_NORMAL) && (~flags & TTYB_NORMAL);
	if (change || left < size) {
		/* This is the slow path - looking for new buffers to use */
		n = tty_buffer_alloc(port, size);
		if (n != NULL) {
			n->flags = flags;
			buf->tail = n;
			/* paired w/ acquire in flush_to_ldisc(); ensures
			 * flush_to_ldisc() sees buffer data.
			 */
			smp_store_release(&b->commit, b->used);
			/* paired w/ acquire in flush_to_ldisc(); ensures the
			 * latest commit value can be read before the head is
			 * advanced to the next buffer
			 */
			smp_store_release(&b->next, n);
		} else if (change)
			size = 0;
		else
			size = left;
	}
	return size;
}

int tty_buffer_request_room(struct tty_port *port, size_t size)
{
	return __tty_buffer_request_room(port, size, 0);
}
EXPORT_SYMBOL_GPL(tty_buffer_request_room);

/**
 *	tty_insert_flip_string_fixed_flag - Add characters to the tty buffer
 *	@port: tty port
 *	@chars: characters
 *	@flag: flag value for each character
 *	@size: size
 *
 *	Queue a series of bytes to the tty buffering. All the characters
 *	passed are marked with the supplied flag. Returns the number added.
 */

int tty_insert_flip_string_fixed_flag(struct tty_port *port,
		const unsigned char *chars, char flag, size_t size)
{
	int copied = 0;
	do {
		int goal = min_t(size_t, size - copied, TTY_BUFFER_PAGE);
		int flags = (flag == TTY_NORMAL) ? TTYB_NORMAL : 0;
		int space = __tty_buffer_request_room(port, goal, flags);
		struct tty_buffer *tb = port->buf.tail;
		if (unlikely(space == 0))
			break;
		memcpy(char_buf_ptr(tb, tb->used), chars, space);
		if (~tb->flags & TTYB_NORMAL)
			memset(flag_buf_ptr(tb, tb->used), flag, space);
		tb->used += space;
		copied += space;
		chars += space;
		/* There is a small chance that we need to split the data over
		   several buffers. If this is the case we must loop */
	} while (unlikely(size > copied));
	return copied;
}
EXPORT_SYMBOL(tty_insert_flip_string_fixed_flag);

/**
 *	tty_insert_flip_string_flags	-	Add characters to the tty buffer
 *	@port: tty port
 *	@chars: characters
 *	@flags: flag bytes
 *	@size: size
 *
 *	Queue a series of bytes to the tty buffering. For each character
 *	the flags array indicates the status of the character. Returns the
 *	number added.
 */

int tty_insert_flip_string_flags(struct tty_port *port,
		const unsigned char *chars, const char *flags, size_t size)
{
	int copied = 0;
	do {
		int goal = min_t(size_t, size - copied, TTY_BUFFER_PAGE);
		int space = tty_buffer_request_room(port, goal);
		struct tty_buffer *tb = port->buf.tail;
		if (unlikely(space == 0))
			break;
		memcpy(char_buf_ptr(tb, tb->used), chars, space);
		memcpy(flag_buf_ptr(tb, tb->used), flags, space);
		tb->used += space;
		copied += space;
		chars += space;
		flags += space;
		/* There is a small chance that we need to split the data over
		   several buffers. If this is the case we must loop */
	} while (unlikely(size > copied));
	return copied;
}
EXPORT_SYMBOL(tty_insert_flip_string_flags);

/**
 *	__tty_insert_flip_char   -	Add one character to the tty buffer
 *	@port: tty port
 *	@ch: character
 *	@flag: flag byte
 *
 *	Queue a single byte to the tty buffering, with an optional flag.
 *	This is the slow path of tty_insert_flip_char.
 */
int __tty_insert_flip_char(struct tty_port *port, unsigned char ch, char flag)
{
	struct tty_buffer *tb;
	int flags = (flag == TTY_NORMAL) ? TTYB_NORMAL : 0;

	if (!__tty_buffer_request_room(port, 1, flags))
		return 0;

	tb = port->buf.tail;
	if (~tb->flags & TTYB_NORMAL)
		*flag_buf_ptr(tb, tb->used) = flag;
	*char_buf_ptr(tb, tb->used++) = ch;

	return 1;
}
EXPORT_SYMBOL(__tty_insert_flip_char);

/**
 *	tty_schedule_flip	-	push characters to ldisc
 *	@port: tty port to push from
 *
 *	Takes any pending buffers and transfers their ownership to the
 *	ldisc side of the queue. It then schedules those characters for
 *	processing by the line discipline.
 */

void tty_schedule_flip(struct tty_port *port)
{
	struct tty_bufhead *buf = &port->buf;

	/* paired w/ acquire in flush_to_ldisc(); ensures
	 * flush_to_ldisc() sees buffer data.
	 */
	smp_store_release(&buf->tail->commit, buf->tail->used);
	queue_work(system_unbound_wq, &buf->work);
}
EXPORT_SYMBOL(tty_schedule_flip);

/**
 *	tty_prepare_flip_string		-	make room for characters
 *	@port: tty port
 *	@chars: return pointer for character write area
 *	@size: desired size
 *
 *	Prepare a block of space in the buffer for data. Returns the length
 *	available and buffer pointer to the space which is now allocated and
 *	accounted for as ready for normal characters. This is used for drivers
 *	that need their own block copy routines into the buffer. There is no
 *	guarantee the buffer is a DMA target!
 */

int tty_prepare_flip_string(struct tty_port *port, unsigned char **chars,
		size_t size)
{
	int space = __tty_buffer_request_room(port, size, TTYB_NORMAL);
	if (likely(space)) {
		struct tty_buffer *tb = port->buf.tail;
		*chars = char_buf_ptr(tb, tb->used);
		if (~tb->flags & TTYB_NORMAL)
			memset(flag_buf_ptr(tb, tb->used), TTY_NORMAL, space);
		tb->used += space;
	}
	return space;
}
EXPORT_SYMBOL_GPL(tty_prepare_flip_string);

/**
 *	tty_ldisc_receive_buf		-	forward data to line discipline
 *	@ld:	line discipline to process input
 *	@p:	char buffer
 *	@f:	TTY_* flags buffer
 *	@count:	number of bytes to process
 *
 *	Callers other than flush_to_ldisc() need to exclude the kworker
 *	from concurrent use of the line discipline, see paste_selection().
 *
 *	Returns the number of bytes processed
 */
int tty_ldisc_receive_buf(struct tty_ldisc *ld, const unsigned char *p,
			  char *f, int count)
{
	if (ld->ops->receive_buf2)
		count = ld->ops->receive_buf2(ld->tty, p, f, count);
	else {
		count = min_t(int, count, ld->tty->receive_room);
		if (count && ld->ops->receive_buf)
			ld->ops->receive_buf(ld->tty, p, f, count);
	}
	return count;
}
EXPORT_SYMBOL_GPL(tty_ldisc_receive_buf);

static int
receive_buf(struct tty_port *port, struct tty_buffer *head, int count)
{
	unsigned char *p = char_buf_ptr(head, head->read);
	char	      *f = NULL;
	int n;

	if (~head->flags & TTYB_NORMAL)
		f = flag_buf_ptr(head, head->read);

	n = port->client_ops->receive_buf(port, p, f, count);
	if (n > 0)
		memset(p, 0, n);
	return n;
}

/**
 *	flush_to_ldisc
 *	@work: tty structure passed from work queue.
 *
 *	This routine is called out of the software interrupt to flush data
 *	from the buffer chain to the line discipline.
 *
 *	The receive_buf method is single threaded for each tty instance.
 *
 *	Locking: takes buffer lock to ensure single-threaded flip buffer
 *		 'consumer'
 */

static void flush_to_ldisc(struct work_struct *work)
{
	struct tty_port *port = container_of(work, struct tty_port, buf.work);
	struct tty_bufhead *buf = &port->buf;

	mutex_lock(&buf->lock);

	while (1) {
		struct tty_buffer *head = buf->head;
		struct tty_buffer *next;
		int count;

		/* Ldisc or user is trying to gain exclusive access */
		if (atomic_read(&buf->priority))
			break;

		/* paired w/ release in __tty_buffer_request_room();
		 * ensures commit value read is not stale if the head
		 * is advancing to the next buffer
		 */
		next = smp_load_acquire(&head->next);
		/* paired w/ release in __tty_buffer_request_room() or in
		 * tty_buffer_flush(); ensures we see the committed buffer data
		 */
		count = smp_load_acquire(&head->commit) - head->read;
		if (!count) {
			if (next == NULL)
				break;
			buf->head = next;
			tty_buffer_free(port, head);
			continue;
		}

		count = receive_buf(port, head, count);
		if (!count)
			break;
		head->read += count;
	}

	mutex_unlock(&buf->lock);

}

#ifdef CONFIG_SYNO_MICROP_COMMAND_V2
/*
 * cleanup_buffers_work - free any fully unused buffers on the head chain
 *
 * Runs in process context. It mirrors the cleanup loop used in
 * tty_buffer_flush but does not call into the line discipline. It will
 * advance buf->head and free buffers which have no committed data.
 */
static void cleanup_buffers_work(struct work_struct *work)
{
	struct tty_port *port = container_of(work, struct tty_port, buf.syno_microp_cleanup);
	struct tty_bufhead *buf = &port->buf;
	struct tty_buffer *head = NULL;
	struct tty_buffer *next = NULL;

	mutex_lock(&buf->lock);
	while (1) {
		head = buf->head;
		next = smp_load_acquire(&head->next);
		if (!next)
			break;

		/* if there is committed data, stop */
		if (smp_load_acquire(&head->commit) - head->read)
			break;

		buf->head = next;
		tty_buffer_free(port, head);
	}
	mutex_unlock(&buf->lock);
}
#endif /* CONFIG_SYNO_MICROP_COMMAND_V2 */

#ifdef CONFIG_SYNO_MICROP_COMMAND_V2
void syno_microp_notify_work(struct tty_port *port)
{
	syno_microp_v2_wakeup();
}
EXPORT_SYMBOL(syno_microp_notify_work);
#endif /* CONFIG_SYNO_MICROP_COMMAND_V2 */

#ifdef CONFIG_SYNO_MICROP_COMMAND_V2
/*
 * syno_microp_irq_flip_push - IRQ-path copy of committed flip-buffers
 * @port: tty port
 *
 * Runs in IRQ/atomic context. Copies all committed data from head..tail
 * into the synobios kfifo without taking the buf lock. For the active
 * tail buffer we clear bookkeeping so the producer can continue to use
 * the same buffer; non-tail buffers advance read so cleanup can reclaim
 * them later.
 */
static void syno_microp_irq_flip_push(struct tty_port *port)
{
	struct tty_bufhead *buf = &port->buf;
	struct tty_buffer *b = NULL;
	struct tty_buffer *next = NULL;
	int commit = 0;
	int avail = 0;

	/* Ensure active tail commit reflects any recently written tail->used */
	smp_store_release(&buf->tail->commit, buf->tail->used);

	if (buf->head) {
		b = buf->head;
		while (1) {
			next = smp_load_acquire(&b->next);
			commit = smp_load_acquire(&b->commit);
			avail = commit - b->read;

			if (avail > 0) {
				syno_microp_fifo_force_in(char_buf_ptr(b, b->read), avail);
				if (b == buf->tail) {
					b->used = 0;
					smp_store_release(&b->commit, 0);
					b->read = 0;
				} else {
					b->read += avail;
				}
			}
			if (!next)
				break;
			b = next;
		}
	}

	/* Schedule asynchronous cleanup of empty buffers */
	queue_work(system_unbound_wq, &buf->syno_microp_cleanup);

	/* Notify synobios that data is ready */
	port->syno_microp_notify(port);
}
#endif

/**
 *	tty_flip_buffer_push	-	terminal
 *	@port: tty port to push
 *
 *	Queue a push of the terminal flip buffers to the line discipline.
 *	Can be called from IRQ/atomic context.
 *
 *	In the event of the queue being busy for flipping the work will be
 *	held off and retried later.
 */

void tty_flip_buffer_push(struct tty_port *port)
{
#ifdef CONFIG_SYNO_MICROP_COMMAND_V2
	if (port->syno_microp_notify && !READ_ONCE(port->syno_microp_bypass)) {
		if (likely(in_irq())) {
			syno_microp_irq_flip_push(port);
		} else {
			WARN_ONCE(1, "syno_microp_irq_flip_push called outside IRQ context");
		}
	} else {
#endif /* CONFIG_SYNO_MICROP_COMMAND_V2 */
	tty_schedule_flip(port);
#ifdef CONFIG_SYNO_MICROP_COMMAND_V2
	}
#endif /* CONFIG_SYNO_MICROP_COMMAND_V2 */
}
EXPORT_SYMBOL(tty_flip_buffer_push);

/**
 *	tty_buffer_init		-	prepare a tty buffer structure
 *	@port: tty port to initialise
 *
 *	Set up the initial state of the buffer management for a tty device.
 *	Must be called before the other tty buffer functions are used.
 */

void tty_buffer_init(struct tty_port *port)
{
	struct tty_bufhead *buf = &port->buf;

	mutex_init(&buf->lock);
	tty_buffer_reset(&buf->sentinel, 0);
	buf->head = &buf->sentinel;
	buf->tail = &buf->sentinel;
	init_llist_head(&buf->free);
	atomic_set(&buf->mem_used, 0);
	atomic_set(&buf->priority, 0);
	INIT_WORK(&buf->work, flush_to_ldisc);
#ifdef CONFIG_SYNO_MICROP_COMMAND_V2
	INIT_WORK(&buf->syno_microp_cleanup, cleanup_buffers_work);
#endif
	buf->mem_limit = TTYB_DEFAULT_MEM_LIMIT;
}

/**
 *	tty_buffer_set_limit	-	change the tty buffer memory limit
 *	@port: tty port to change
 *
 *	Change the tty buffer memory limit.
 *	Must be called before the other tty buffer functions are used.
 */

int tty_buffer_set_limit(struct tty_port *port, int limit)
{
	if (limit < MIN_TTYB_SIZE)
		return -EINVAL;
	port->buf.mem_limit = limit;
	return 0;
}
EXPORT_SYMBOL_GPL(tty_buffer_set_limit);

/* slave ptys can claim nested buffer lock when handling BRK and INTR */
void tty_buffer_set_lock_subclass(struct tty_port *port)
{
	lockdep_set_subclass(&port->buf.lock, TTY_LOCK_SLAVE);
}

bool tty_buffer_restart_work(struct tty_port *port)
{
	return queue_work(system_unbound_wq, &port->buf.work);
}

bool tty_buffer_cancel_work(struct tty_port *port)
{
	return cancel_work_sync(&port->buf.work);
}

void tty_buffer_flush_work(struct tty_port *port)
{
	flush_work(&port->buf.work);
}
