
#ifndef FDOBEX_H
#define FDOBEX_H

void fdobex_get_ops(struct obex_transport_ops* ops);

struct fdobex_data {
	socket_t writefd; /* write descriptor */
};
#endif
