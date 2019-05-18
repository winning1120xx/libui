// 15 may 2019
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "uipriv.h"

#define nGrow 32

struct handler {
	int id;
	uiEventHandler f;
	void *sender;
	void *data;
	bool blocked;
};

static int handlerCmp(const void *a, const void *b)
{
	const struct handler *ha = (const struct handler *) a;
	const struct handler *hb = (const struct handler *) b;

	// This could be ha->id - hb->id, but let's do it the explicit way to avoid integer overflow/underflow.
	if (ha->id < hb->id)
		return -1;
	if (ha->id > hb->id)
		return 1;
	return 0;
}

struct uiEvent {
	uiEventOptions opts;
	struct handler *handlers;
	size_t len;
	size_t cap;
	int *unusedIDs;
	size_t unusedIDsLen;
	size_t unusedIDsCap;
	bool firing;
};

uiEvent *uiNewEvent(const uiEventOptions *options)
{
	uiEvent *e;

	if (options == NULL) {
		uiprivProgrammerError(uiprivProgrammerErrorNullPointer, "uiEventOptions", __func__);
		return NULL;
	}
	if (options->Size != sizeof (uiEventOptions)) {
		uiprivProgrammerError(uiprivProgrammerErrorWrongStructSize, options->Size, "uiEventOptions");
		return NULL;
	}
	e = (uiEvent *) uiprivAlloc(sizeof (uiEvent), "uiEvent");
	e->opts = *options;
	return e;
}

#define checkEventNonnull(e, ret) if ((e) == NULL) { \
	uiprivProgrammerError(uiprivProgrammerErrorNullPointer, "uiEvent", __func__); \
	return ret; \
}
#define checkEventNotFiring(e, ret) if ((e)->firing) { \
	uiprivProgrammerError(uiprivProgrammerErrorChangingEventDuringFire, __func__); \
	return ret; \
}

static bool checkEventSender(const uiEvent *e, void *sender, const char *func)
{
	if (e->opts.Global && sender != NULL) {
		uiprivProgrammerError(uiprivProgrammerErrorBadSenderForEvent, "non-NULL", "global", func);
		return false;
	}
	if (!e->opts.Global && sender == NULL) {
		uiprivProgrammerError(uiprivProgrammerErrorBadSenderForEvent, "NULL", "non-global", func);
		return false;
	}
	return true;
}

int uiEventAddHandler(uiEvent *e, uiEventHandler handler, void *sender, void *data)
{
	struct handler *h;
	int retID;

	checkEventNonnull(e, 0);
	checkEventNotFiring(e, 0);
	if (handler == NULL) {
		uiprivProgrammerError(uiprivProgrammerErrorNullPointer, "uiEventHandler", __func__);
		return 0;
	}
	if (!checkEventSender(e, sender, __func__))
		return 0;

	if (e->len >= e->cap) {
		e->handlers = uiprivRealloc(e->handlers,
			e->cap * sizeof (struct handler),
			(e->cap + nGrow) * sizeof (struct handler),
			"uiEvent handlers");
		e->cap += nGrow;
	}

	h = e->handlers + e->len;
	h->id = 0;
	if (e->unusedIDsLen > 0) {
		h->id = e->unusedIDs[e->unusedIDsLen - 1];
		e->unusedIDsLen--;
	} else if (e->len == 0)
		h->id = e->handlers[e->len - 1].id + 1;
	h->f = handler;
	h->sender = sender;
	h->data = data;

	// after the qsort(), h may no longer be correct, so save the return ID now
	retID = h->id;
	e->len++;
	qsort(e->handlers, e->len, sizeof (struct handler), handlerCmp);
	return retID;
}

static struct handler *findHandler(const uiEvent *e, int id, const char *func)
{
	struct handler key;
	struct handler *ret;

	if (e->len == 0)
		goto notFound;
	memset(&key, 0, sizeof (struct handler));
	key.id = id;
	ret = (struct handler *) bsearch(&key, e->handlers, e->len, sizeof (struct handler), handlerCmp);
	if (ret != NULL)
		return ret;
	// otherwise fall through
notFound:
	uiprivProgrammerError(uiprivProgrammerErrorIntIDNotFound, "uiEvent handler", id, func);
	return NULL;
}

void uiEventDeleteHandler(uiEvent *e, int id)
{
	struct handler *h;

	checkEventNonnull(e, /* nothing */);
	checkEventNotFiring(e, /* nothing */);
	h = findHandler(e, id, __func__);
	if (h == NULL)
		return;

	e->len--;
	// TODO write this in a way that doesn't mix ptrdiff_t and size_t
	memmove(h, h + 1, (e->len - (h - e->handlers)) * sizeof (struct handler));

	if (e->unusedIDsLen >= e->unusedIDsCap) {
		e->unusedIDs = (int *) uiprivRealloc(e->unusedIDs,
			e->unusedIDsCap * sizeof (int),
			(e->unusedIDsCap + nGrow) * sizeof (int),
			"uiEvent handler unused IDs");
		e->unusedIDsCap += nGrow;
	}
	e->unusedIDs[e->unusedIDsLen] = id;
	e->unusedIDsLen++;
}

void uiEventFire(uiEvent *e, void *sender, void *args)
{
	struct handler *h;
	size_t i;

	checkEventNonnull(e, /* nothing */);
	if (e->firing) {
		uiprivProgrammerError(uiprivProgrammerErrorRecursiveEventFire);
		return;
	}
	if (!checkEventSender(e, sender, __func__))
		return;

	e->firing = true;
	h = e->handlers;
	for (i = 0; i < e->len; i++) {
		if (h->sender == sender && !h->blocked)
			(*(h->f))(sender, args, h->data);
		h++;
	}
	e->firing = false;
}

bool uiEventHandlerBlocked(const uiEvent *e, int id)
{
	struct handler *h;

	checkEventNonnull(e, false);
	h = findHandler(e, id, __func__);
	if (h == NULL)
		return false;
	return h->blocked;
}

void uiEventSetHandlerBlocked(uiEvent *e, int id, bool blocked)
{
	struct handler *h;

	checkEventNonnull(e, /* nothing */);
	checkEventNotFiring(e, /* nothing */);
	h = findHandler(e, id, __func__);
	if (h == NULL)
		return;
	h->blocked = blocked;
}