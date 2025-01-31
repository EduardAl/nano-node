/*
	a custom hash must have a 512bit digest and implement:

	struct ed25519_hash_context;

	void ed25519_hash_init(ed25519_hash_context *ctx);
	void ed25519_hash_update(ed25519_hash_context *ctx, const uint8_t *in, size_t inlen);
	void ed25519_hash_final(ed25519_hash_context *ctx, uint8_t *hash);
	void ed25519_hash(uint8_t *hash, const uint8_t *in, size_t inlen);
*/

#include <vban/crypto/blake2/blake2.h>

typedef blake2b_state ed25519_hash_context;

void ed25519_hash_init (ed25519_hash_context * ctx);

void ed25519_hash_update (ed25519_hash_context * ctx, uint8_t const * in, size_t inlen);

void ed25519_hash_final (ed25519_hash_context * ctx, uint8_t * out);

void ed25519_hash (uint8_t * out, uint8_t const * in, size_t inlen);
