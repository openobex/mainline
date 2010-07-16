
void custom_get_ops(struct obex_transport_ops* ops);
int custom_register(obex_t *self, const obex_ctrans_t *in);
void custom_set_data(obex_t *self, void *data);
void* custom_get_data(obex_t *self);
