/*
 * $Id$
 */

#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/time.h>

#include <pthread.h>

#include <event.h>

#include <cli.h>
#include <cli_priv.h>

#include "libvarnish.h"
#include "heritage.h"
#include "shmlog.h"
#include "cache.h"
#include "cli_event.h"

static struct event ev_keepalive;

/*--------------------------------------------------------------------*/

static void
timer_keepalive(int a, short b, void *c)
{

	printf("%s(%d, %d, %p)\n", __func__, a, b, c);
	printf("Heeellloooo ?   Ohh bother...\n");
	exit (1);
}

static void
arm_keepalive(void)
{
	struct timeval tv;

	tv.tv_sec = 30;
	tv.tv_usec = 0;

	evtimer_del(&ev_keepalive);
	evtimer_add(&ev_keepalive, &tv);
}

/*--------------------------------------------------------------------*/

static void
cli_func_url_query(struct cli *cli, char **av, void *priv __unused)
{

	cli_out(cli, "url <%s>", av[2]);
	cli_result(cli, CLIS_UNIMPL);
}

/*--------------------------------------------------------------------*/

static void
cli_func_ping(struct cli *cli, char **av, void *priv __unused)
{
	time_t t;

	VSL(SLT_CLI, 0, av[1]);
	arm_keepalive();
	if (av[2] != NULL) {
		/* XXX: check clock skew is pointless here */
		printf("Got your ping %s\n", av[2]);
	}
	time(&t);
	cli_out(cli, "PONG %ld\n", t);
}

/*--------------------------------------------------------------------*/

static struct cli_proto cli_proto[] = {
	{ CLI_URL_QUERY,	cli_func_url_query },
	{ CLI_PING,		cli_func_ping },
	{ NULL }
};

static pthread_t vca_thread;

void
child_main(void)
{
	struct event_base *eb;
	struct cli *cli;
	int i;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	printf("Child starts\n");

	VSL_Init();

	AZ(pthread_create(&vca_thread, NULL, vca_main, NULL));

	eb = event_init();
	assert(eb != NULL);

	cli = cli_setup(heritage.fds[2], heritage.fds[1], 0, cli_proto);

	evtimer_set(&ev_keepalive, timer_keepalive, NULL);
	event_base_set(eb, &ev_keepalive);
	arm_keepalive();

	i = event_base_loop(eb, 0);
	if (i != 0)
		printf("event_dispatch() = %d\n", i);

	printf("Child dies\n");
}

