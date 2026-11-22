/* SPDX-License-Identifier: MIT */
#include "coverq.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Three at once: a cover is ~100 ms of curl and the server's resize, mostly
 * waiting, so three land in the time one did (Brick, plorpos-ps8.9). */
#define COVERQ_THREADS 3
#define COVERQ_KEEP    48
#define COVERQ_LANDED  32
#define COVERQ_STACK   (256 * 1024)

typedef struct { char src[700], base[700]; } cjob;

struct coverq {
	pthread_mutex_t mu;
	pthread_cond_t cv;
	coverq_fetch_fn fetch;
	cjob q[COVERQ_KEEP];               /* q[n - 1] is fetched next */
	int n;
	char landed[COVERQ_LANDED][700];
	int nlanded;
	bool up;
};

static void *coverq_main(void *arg)
{
	coverq *q = arg;
	cjob j;

	for (;;) {
		pthread_mutex_lock(&q->mu);
		while (!q->n) pthread_cond_wait(&q->cv, &q->mu);
		j = q->q[--q->n];
		pthread_mutex_unlock(&q->mu);
		if (!q->fetch(j.src, j.base)) continue;
		pthread_mutex_lock(&q->mu);
		if (q->nlanded < COVERQ_LANDED)
			snprintf(q->landed[q->nlanded++], sizeof q->landed[0], "%s", j.base);
		pthread_mutex_unlock(&q->mu);
	}
	return NULL;
}

coverq *coverq_new(coverq_fetch_fn fetch)
{
	coverq *q = calloc(1, sizeof *q);

	if (!q) return NULL;
	pthread_mutex_init(&q->mu, NULL);
	pthread_cond_init(&q->cv, NULL);
	q->fetch = fetch;
	return q;
}

/* Its place in the queue, -1 when it is not waiting. Locked. */
static int find(const coverq *q, const char *base)
{
	for (int i = q->n - 1; i >= 0; i--)
		if (!strcmp(q->q[i].base, base)) return i;
	return -1;
}

/* Job i moved to the top. Locked. */
static void raise_job(coverq *q, int i)
{
	cjob j;

	if (i < 0 || i == q->n - 1) return;
	j = q->q[i];
	memmove(&q->q[i], &q->q[i + 1], sizeof q->q[0] * (size_t)(q->n - 1 - i));
	q->q[q->n - 1] = j;
}

/* The threads, on the first wish: none for a source never browsed. */
static void start(coverq *q)
{
	pthread_attr_t at;
	pthread_t th;

	if (q->up) return;
	q->up = true;
	pthread_attr_init(&at);
	pthread_attr_setstacksize(&at, COVERQ_STACK);
	pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
	for (int k = 0; k < COVERQ_THREADS; k++)
		if (pthread_create(&th, &at, coverq_main, q) != 0 && k == 0) q->up = false;
	pthread_attr_destroy(&at);
}

void coverq_want(coverq *q, const char *src, const char *base)
{
	int i;

	if (!q) return;
	pthread_mutex_lock(&q->mu);
	start(q);
	if ((i = find(q, base)) >= 0) {
		raise_job(q, i);
	} else {
		if (q->n == COVERQ_KEEP) {          /* the oldest wish goes */
			memmove(&q->q[0], &q->q[1], sizeof q->q[0] * (COVERQ_KEEP - 1));
			q->n--;
		}
		snprintf(q->q[q->n].src, sizeof q->q[0].src, "%s", src);
		snprintf(q->q[q->n].base, sizeof q->q[0].base, "%s", base);
		q->n++;
		pthread_cond_signal(&q->cv);
	}
	pthread_mutex_unlock(&q->mu);
}

void coverq_bump(coverq *q, const char *base)
{
	if (!q) return;
	pthread_mutex_lock(&q->mu);
	raise_job(q, find(q, base));
	pthread_mutex_unlock(&q->mu);
}

bool coverq_idle(coverq *q)
{
	bool idle;

	if (!q) return true;
	pthread_mutex_lock(&q->mu);
	idle = q->n == 0;
	pthread_mutex_unlock(&q->mu);
	return idle;
}

bool coverq_take(coverq *q, char *base, size_t n)
{
	bool got = false;

	if (!q) return false;
	pthread_mutex_lock(&q->mu);
	if (q->nlanded) {
		snprintf(base, n, "%s", q->landed[--q->nlanded]);
		got = true;
	}
	pthread_mutex_unlock(&q->mu);
	return got;
}
