/*
 * QUIC v1 (RFC 9000 / RFC 9001) as a suspenders hose transport.
 *
 * quic://host:port dials, quic://0.0.0.0:port listens. One connection is one
 * hose; stream 0 is the byte stream (read/write). The handshake is TLS 1.3
 * with X25519, TLS_AES_128_GCM_SHA256 and an ephemeral Ed25519 certificate.
 *
 * Trust: CertificateVerify is checked against the certificate's public key,
 * so the peer proved it holds that key. There is no PKI, name check, or
 * pinning — same shape as an unauthenticated TLS session. Both ends here
 * are this stack (ALPN "susp"); this is not an HTTP/3 endpoint.
 *
 * Included from suspenders.h inside the implementation, after the blocking
 * I/O helpers. POSIX + OpenSSL 3 only.
 */
#ifndef SUSPENDERS_QUIC_H
#define SUSPENDERS_QUIC_H

#if defined(SUSPENDERS_HAVE_OPENSSL) && !defined(SUSPENDERS_PLATFORM_WINDOWS)

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpedantic"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/core_names.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <openssl/hmac.h>
#include <openssl/err.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <pthread.h>

#define S_QUIC_CID_LEN 8
#define S_QUIC_MAX_DGRAM 1400
#define S_QUIC_INITIAL_PAD 1200
#define S_QUIC_CRYPTO_MAX 8192
#define S_QUIC_STREAM_MAX (256 * 1024)
#define S_QUIC_FLOW (8u * 1024u * 1024u)

enum {
    S_QUIC_INITIAL = 0,
    S_QUIC_HANDSHAKE = 1,
    S_QUIC_APP = 2
};

typedef struct {
    uint8_t secret[32];
    uint8_t key[16];
    uint8_t iv[12];
    uint8_t hp[16];
    int have;
} s_quic_keys_t;

typedef struct {
    int live;
    uint64_t pn;
    uint64_t sent_ns;
    uint8_t frames[1200];
    uint16_t flen;
} s_quic_flight_t;

typedef struct s_quic_conn {
    int is_server;
    int owns_fd;
    int app_ready;
    int closed;
    int ch_done;          /* server has accepted ClientHello */
    int client_fin_sent;
    suspenders_sock_t fd;
    struct sockaddr_in peer;
    int peer_set;

    uint8_t local_cid[S_QUIC_CID_LEN];
    uint8_t peer_cid[S_QUIC_CID_LEN];
    uint8_t orig_dcid[S_QUIC_CID_LEN];

    EVP_PKEY *x25519;
    uint8_t my_pub[32];
    uint8_t peer_pub[32];
    int have_peer_pub;
    uint8_t shared[32];
    uint8_t hs_secret[32];
    int have_shared;

    s_quic_keys_t keys[2][3]; /* [0]=client->server, [1]=server->client */
    uint64_t tx_pn[3];
    int rx_have[3];
    uint64_t rx_max[3];
    uint64_t rx_mask[3];

    EVP_MD_CTX *transcript;

    uint8_t rx_crypto[2][S_QUIC_CRYPTO_MAX];
    size_t rx_crypto_len[2];
    size_t rx_crypto_parsed[2];

    uint8_t *rx;
    size_t rx_len;
    size_t rx_pos;
    size_t rx_cap;
    uint64_t tx_off;
    uint64_t peer_bidi_local;
    uint64_t peer_bidi_remote;
    int rx_fin;

    s_quic_flight_t flight[3];
    uint8_t cert_der[1024];
    size_t cert_der_len;
    int saw_params;
} s_quic_conn_t;

static EVP_PKEY *s_quic_cert_key = NULL;
static uint8_t s_quic_cert_der[768];
static int s_quic_cert_der_len = 0;
static pthread_once_t s_quic_cert_once = PTHREAD_ONCE_INIT;

static int s_quic_trace(void) {
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("SUSPENDERS_QUIC_TRACE");
        v = (e && e[0]) ? 1 : 0;
    }
    return v;
}

static void s_quic_log(const char *msg) {
    if (s_quic_trace()) fprintf(stderr, "quic: %s\n", msg);
}

static const uint8_t s_quic_v1_salt[20] = {
    0x38, 0x76, 0x2c, 0xf7, 0xf5, 0x59, 0x34, 0xb3, 0x4d, 0x17,
    0x9a, 0xe6, 0xa4, 0xc8, 0x0c, 0xad, 0xcc, 0xbb, 0x7f, 0x0a
};

static const uint8_t s_quic_empty_hash[32] = {
    0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8,
    0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
    0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
};

/* RFC 9001 appendix A.1 — fail closed if HKDF doesn't match. */
static const uint8_t s_quic_vec_cid[8] = {
    0x83, 0x94, 0xc8, 0xf0, 0x3e, 0x51, 0x57, 0x08
};
static const uint8_t s_quic_vec_client_key[16] = {
    0x1f, 0x36, 0x96, 0x13, 0xdd, 0x76, 0xd5, 0x46,
    0x77, 0x30, 0xef, 0xcb, 0xe3, 0xb1, 0xa2, 0x2d
};

static size_t s_quic_varint_size(uint64_t v) {
    if (v <= 63ull) return 1;
    if (v <= 16383ull) return 2;
    if (v <= 1073741823ull) return 4;
    return 8;
}

static int s_quic_put_varint(uint8_t *p, size_t cap, uint64_t v) {
    size_t n = s_quic_varint_size(v);
    size_t i;
    if (n > cap) return -1;
    if (n == 1) { p[0] = (uint8_t)v; return 1; }
    if (n == 2) { p[0] = (uint8_t)((v >> 8) | 0x40); p[1] = (uint8_t)v; return 2; }
    if (n == 4) {
        p[0] = (uint8_t)((v >> 24) | 0x80);
        p[1] = (uint8_t)(v >> 16);
        p[2] = (uint8_t)(v >> 8);
        p[3] = (uint8_t)v;
        return 4;
    }
    p[0] = (uint8_t)((v >> 56) | 0xc0);
    for (i = 1; i < 8; i++) p[i] = (uint8_t)(v >> (56 - 8 * i));
    return 8;
}

static int s_quic_get_varint(const uint8_t **pp, const uint8_t *end, uint64_t *out) {
    const uint8_t *p = *pp;
    uint64_t v;
    size_t n, i;
    if (p >= end) return -1;
    n = 1u << (p[0] >> 6);
    if ((size_t)(end - p) < n) return -1;
    v = p[0] & 0x3fu;
    for (i = 1; i < n; i++) v = (v << 8) | p[i];
    *pp = p + n;
    *out = v;
    return 0;
}

static int s_quic_hkdf(int mode_expand_only, const uint8_t *key, size_t key_len,
                       const uint8_t *salt, size_t salt_len,
                       const uint8_t *info, size_t info_len,
                       uint8_t *out, size_t out_len) {
    EVP_KDF *kdf;
    EVP_KDF_CTX *ctx;
    int ok;
    char *mode = mode_expand_only ? (char *)"EXPAND_ONLY" : (char *)"EXTRACT_ONLY";
    OSSL_PARAM params[5];
    int np = 0;
    params[np++] = OSSL_PARAM_construct_utf8_string("digest", (char *)"SHA256", 0);
    params[np++] = OSSL_PARAM_construct_utf8_string("mode", mode, 0);
    params[np++] = OSSL_PARAM_construct_octet_string("key", (void *)key, key_len);
    if (!mode_expand_only) {
        params[np++] = OSSL_PARAM_construct_octet_string("salt",
            (void *)(salt ? salt : (const uint8_t *)""), salt_len);
    } else if (info && info_len) {
        params[np++] = OSSL_PARAM_construct_octet_string("info", (void *)info, info_len);
    }
    params[np] = OSSL_PARAM_construct_end();
    kdf = EVP_KDF_fetch(NULL, "HKDF", NULL);
    if (!kdf) return -1;
    ctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!ctx) return -1;
    ok = EVP_KDF_derive(ctx, out, out_len, params) > 0;
    EVP_KDF_CTX_free(ctx);
    return ok ? 0 : -1;
}

static int s_quic_expand_label(const uint8_t *secret, const char *label,
                               const uint8_t *ctx, size_t ctx_len,
                               uint8_t *out, size_t out_len) {
    uint8_t info[128];
    size_t lab_len = strlen(label);
    uint8_t *p = info;
    if (lab_len > 64 || ctx_len > 32 || out_len > 255) return -1;
    *p++ = 0;
    *p++ = (uint8_t)out_len;
    *p++ = (uint8_t)(6 + lab_len);
    memcpy(p, "tls13 ", 6);
    p += 6;
    memcpy(p, label, lab_len);
    p += lab_len;
    *p++ = (uint8_t)ctx_len;
    if (ctx_len) {
        memcpy(p, ctx, ctx_len);
        p += ctx_len;
    }
    return s_quic_hkdf(1, secret, 32, NULL, 0, info, (size_t)(p - info), out, out_len);
}

static int s_quic_install_from_secret(s_quic_keys_t *k) {
    if (s_quic_expand_label(k->secret, "quic key", NULL, 0, k->key, 16) != 0) return -1;
    if (s_quic_expand_label(k->secret, "quic iv", NULL, 0, k->iv, 12) != 0) return -1;
    if (s_quic_expand_label(k->secret, "quic hp", NULL, 0, k->hp, 16) != 0) return -1;
    k->have = 1;
    return 0;
}

static int s_quic_install_initial(s_quic_conn_t *c) {
    uint8_t initial[32];
    uint8_t zeros[32];
    memset(zeros, 0, sizeof(zeros));
    if (s_quic_hkdf(0, c->orig_dcid, S_QUIC_CID_LEN, s_quic_v1_salt, sizeof(s_quic_v1_salt),
                    NULL, 0, initial, 32) != 0) return -1;
    if (s_quic_expand_label(initial, "client in", NULL, 0, c->keys[0][S_QUIC_INITIAL].secret, 32) != 0)
        return -1;
    if (s_quic_expand_label(initial, "server in", NULL, 0, c->keys[1][S_QUIC_INITIAL].secret, 32) != 0)
        return -1;
    if (s_quic_install_from_secret(&c->keys[0][S_QUIC_INITIAL]) != 0) return -1;
    if (s_quic_install_from_secret(&c->keys[1][S_QUIC_INITIAL]) != 0) return -1;
    (void)zeros;
    return 0;
}

static int s_quic_selftest_keys(void) {
    s_quic_conn_t c;
    static int done = 0;
    if (done) return 0;
    memset(&c, 0, sizeof(c));
    memcpy(c.orig_dcid, s_quic_vec_cid, 8);
    if (s_quic_install_initial(&c) != 0) return -1;
    if (memcmp(c.keys[0][S_QUIC_INITIAL].key, s_quic_vec_client_key, 16) != 0) {
        s_quic_log("RFC 9001 initial-key vector mismatch");
        return -1;
    }
    done = 1;
    return 0;
}

static int s_quic_hp_mask(const uint8_t hp[16], const uint8_t sample[16], uint8_t mask[16]) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int outl = 0, fin = 0;
    int ok;
    if (!ctx) return -1;
    ok = EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), NULL, hp, NULL) == 1
      && EVP_CIPHER_CTX_set_padding(ctx, 0) == 1
      && EVP_EncryptUpdate(ctx, mask, &outl, sample, 16) == 1
      && EVP_EncryptFinal_ex(ctx, mask + outl, &fin) == 1
      && outl + fin == 16;
    EVP_CIPHER_CTX_free(ctx);
    return ok ? 0 : -1;
}

static int s_quic_aead(int enc, const uint8_t key[16], const uint8_t iv[12], uint64_t pn,
                       const uint8_t *aad, size_t aad_len,
                       const uint8_t *in, size_t in_len,
                       uint8_t *out, uint8_t tag[16]) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    uint8_t nonce[12];
    int outl = 0, fin = 0, i;
    int ok;
    if (!ctx) return -1;
    memcpy(nonce, iv, 12);
    for (i = 0; i < 8; i++) nonce[4 + i] ^= (uint8_t)(pn >> (56 - 8 * i));
    if (enc) {
        ok = EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL) == 1
          && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, 12, NULL) == 1
          && EVP_EncryptInit_ex(ctx, NULL, NULL, key, nonce) == 1
          && EVP_EncryptUpdate(ctx, NULL, &outl, aad, (int)aad_len) == 1
          && EVP_EncryptUpdate(ctx, out, &outl, in, (int)in_len) == 1
          && EVP_EncryptFinal_ex(ctx, out + outl, &fin) == 1
          && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, 16, tag) == 1;
    } else {
        ok = EVP_DecryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL) == 1
          && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, 12, NULL) == 1
          && EVP_DecryptInit_ex(ctx, NULL, NULL, key, nonce) == 1
          && EVP_DecryptUpdate(ctx, NULL, &outl, aad, (int)aad_len) == 1
          && EVP_DecryptUpdate(ctx, out, &outl, in, (int)in_len) == 1
          && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, 16, (void *)tag) == 1
          && EVP_DecryptFinal_ex(ctx, out + outl, &fin) == 1;
    }
    EVP_CIPHER_CTX_free(ctx);
    return ok ? 0 : -1;
}

static int s_quic_hash_init(s_quic_conn_t *c) {
    c->transcript = EVP_MD_CTX_new();
    if (!c->transcript) return -1;
    if (EVP_DigestInit_ex(c->transcript, EVP_sha256(), NULL) != 1) return -1;
    return 0;
}

static int s_quic_hash_update(s_quic_conn_t *c, const uint8_t *p, size_t n) {
    return EVP_DigestUpdate(c->transcript, p, n) == 1 ? 0 : -1;
}

static int s_quic_hash_peek(s_quic_conn_t *c, uint8_t out[32]) {
    EVP_MD_CTX *tmp = EVP_MD_CTX_new();
    unsigned int l = 0;
    int ok;
    if (!tmp) return -1;
    ok = EVP_MD_CTX_copy_ex(tmp, c->transcript) == 1
      && EVP_DigestFinal_ex(tmp, out, &l) == 1
      && l == 32;
    EVP_MD_CTX_free(tmp);
    return ok ? 0 : -1;
}

static int s_quic_hmac(const uint8_t *key, size_t key_len,
                       const uint8_t *data, size_t data_len, uint8_t out[32]) {
    unsigned int l = 0;
    if (!HMAC(EVP_sha256(), key, (int)key_len, data, data_len, out, &l) || l != 32) return -1;
    return 0;
}

static void s_quic_make_cert(void) {
    EVP_PKEY_CTX *kctx;
    EVP_PKEY *key = NULL;
    X509 *x = NULL;
    X509_NAME *name;
    unsigned char *der = NULL;
    int len;
    kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
    if (!kctx) return;
    if (EVP_PKEY_keygen_init(kctx) != 1 || EVP_PKEY_keygen(kctx, &key) != 1) {
        EVP_PKEY_CTX_free(kctx);
        return;
    }
    EVP_PKEY_CTX_free(kctx);
    x = X509_new();
    if (!x) { EVP_PKEY_free(key); return; }
    X509_set_version(x, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(x), 1);
    X509_gmtime_adj(X509_getm_notBefore(x), 0);
    X509_gmtime_adj(X509_getm_notAfter(x), 60 * 60 * 24 * 365);
    name = X509_get_subject_name(x);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               (const unsigned char *)"suspenders", -1, -1, 0);
    X509_set_issuer_name(x, name);
    X509_set_pubkey(x, key);
    if (X509_sign(x, key, NULL) == 0) {
        X509_free(x);
        EVP_PKEY_free(key);
        return;
    }
    len = i2d_X509(x, &der);
    if (len > 0 && len <= (int)sizeof(s_quic_cert_der)) {
        memcpy(s_quic_cert_der, der, (size_t)len);
        s_quic_cert_der_len = len;
        s_quic_cert_key = key;
        key = NULL;
    }
    OPENSSL_free(der);
    X509_free(x);
    EVP_PKEY_free(key);
}

static int s_quic_x25519(s_quic_conn_t *c) {
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, NULL);
    size_t len = 32;
    if (!ctx) return -1;
    if (EVP_PKEY_keygen_init(ctx) != 1 || EVP_PKEY_keygen(ctx, &c->x25519) != 1) {
        EVP_PKEY_CTX_free(ctx);
        return -1;
    }
    EVP_PKEY_CTX_free(ctx);
    if (EVP_PKEY_get_raw_public_key(c->x25519, c->my_pub, &len) != 1 || len != 32) return -1;
    return 0;
}

static int s_quic_derive_shared(s_quic_conn_t *c) {
    EVP_PKEY *peer;
    EVP_PKEY_CTX *ctx;
    size_t len = 32;
    if (c->have_shared) return 0;
    peer = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, NULL, c->peer_pub, 32);
    if (!peer) return -1;
    ctx = EVP_PKEY_CTX_new(c->x25519, NULL);
    if (!ctx) { EVP_PKEY_free(peer); return -1; }
    if (EVP_PKEY_derive_init(ctx) != 1 || EVP_PKEY_derive_set_peer(ctx, peer) != 1 ||
        EVP_PKEY_derive(ctx, c->shared, &len) != 1 || len != 32) {
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(peer);
        return -1;
    }
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(peer);
    c->have_shared = 1;
    return 0;
}

static int s_quic_derive_handshake(s_quic_conn_t *c) {
    uint8_t early[32], derived[32], th[32], zeros[32];
    memset(zeros, 0, 32);
    if (s_quic_derive_shared(c) != 0) return -1;
    if (s_quic_hkdf(0, zeros, 32, zeros, 32, NULL, 0, early, 32) != 0) return -1;
    if (s_quic_expand_label(early, "derived", s_quic_empty_hash, 32, derived, 32) != 0) return -1;
    if (s_quic_hkdf(0, c->shared, 32, derived, 32, NULL, 0, c->hs_secret, 32) != 0) return -1;
    if (s_quic_hash_peek(c, th) != 0) return -1;
    if (s_quic_expand_label(c->hs_secret, "c hs traffic", th, 32,
                            c->keys[0][S_QUIC_HANDSHAKE].secret, 32) != 0) return -1;
    if (s_quic_expand_label(c->hs_secret, "s hs traffic", th, 32,
                            c->keys[1][S_QUIC_HANDSHAKE].secret, 32) != 0) return -1;
    if (s_quic_install_from_secret(&c->keys[0][S_QUIC_HANDSHAKE]) != 0) return -1;
    if (s_quic_install_from_secret(&c->keys[1][S_QUIC_HANDSHAKE]) != 0) return -1;
    return 0;
}

static int s_quic_derive_app(s_quic_conn_t *c) {
    uint8_t derived[32], master[32], th[32], zeros[32];
    memset(zeros, 0, 32);
    if (s_quic_expand_label(c->hs_secret, "derived", s_quic_empty_hash, 32, derived, 32) != 0)
        return -1;
    if (s_quic_hkdf(0, zeros, 32, derived, 32, NULL, 0, master, 32) != 0) return -1;
    if (s_quic_hash_peek(c, th) != 0) return -1;
    if (s_quic_expand_label(master, "c ap traffic", th, 32,
                            c->keys[0][S_QUIC_APP].secret, 32) != 0) return -1;
    if (s_quic_expand_label(master, "s ap traffic", th, 32,
                            c->keys[1][S_QUIC_APP].secret, 32) != 0) return -1;
    if (s_quic_install_from_secret(&c->keys[0][S_QUIC_APP]) != 0) return -1;
    if (s_quic_install_from_secret(&c->keys[1][S_QUIC_APP]) != 0) return -1;
    OPENSSL_cleanse(master, sizeof(master));
    return 0;
}

static int s_quic_put_params(uint8_t *p, size_t cap, const uint8_t cid[8], int is_server,
                             const uint8_t *odcid) {
    size_t n = 0;
    int k;
    struct { uint64_t id; uint64_t val; int integer; } items[8];
    int ni = 0;
    items[ni].id = 0x04; items[ni].val = S_QUIC_FLOW; items[ni].integer = 1; ni++;
    items[ni].id = 0x05; items[ni].val = S_QUIC_FLOW; items[ni].integer = 1; ni++;
    items[ni].id = 0x06; items[ni].val = S_QUIC_FLOW; items[ni].integer = 1; ni++;
    items[ni].id = 0x08; items[ni].val = 4; items[ni].integer = 1; ni++;
    items[ni].id = 0x01; items[ni].val = 30000; items[ni].integer = 1; ni++;
    for (k = 0; k < ni; k++) {
        int a = s_quic_put_varint(p + n, cap - n, items[k].id);
        int b, c;
        if (a < 0) return -1;
        n += (size_t)a;
        b = s_quic_put_varint(p + n, cap - n, s_quic_varint_size(items[k].val));
        if (b < 0) return -1;
        n += (size_t)b;
        c = s_quic_put_varint(p + n, cap - n, items[k].val);
        if (c < 0) return -1;
        n += (size_t)c;
    }
    /* initial_source_connection_id */
    {
        int a = s_quic_put_varint(p + n, cap - n, 0x0f);
        int b;
        if (a < 0) return -1;
        n += (size_t)a;
        b = s_quic_put_varint(p + n, cap - n, S_QUIC_CID_LEN);
        if (b < 0 || n + (size_t)b + S_QUIC_CID_LEN > cap) return -1;
        n += (size_t)b;
        memcpy(p + n, cid, S_QUIC_CID_LEN);
        n += S_QUIC_CID_LEN;
    }
    if (is_server && odcid) {
        int a = s_quic_put_varint(p + n, cap - n, 0x00);
        int b;
        if (a < 0) return -1;
        n += (size_t)a;
        b = s_quic_put_varint(p + n, cap - n, S_QUIC_CID_LEN);
        if (b < 0 || n + (size_t)b + S_QUIC_CID_LEN > cap) return -1;
        n += (size_t)b;
        memcpy(p + n, odcid, S_QUIC_CID_LEN);
        n += S_QUIC_CID_LEN;
    }
    return (int)n;
}

static int s_quic_take_params(s_quic_conn_t *c, const uint8_t *p, size_t n, int from_server) {
    const uint8_t *end = p + n;
    while (p < end) {
        uint64_t id, len;
        const uint8_t *val;
        if (s_quic_get_varint(&p, end, &id) != 0) return -1;
        if (s_quic_get_varint(&p, end, &len) != 0) return -1;
        if ((size_t)(end - p) < len) return -1;
        val = p;
        p += len;
        if (id == 0x05 || id == 0x06 || id == 0x04 || id == 0x01) {
            const uint8_t *vp = val;
            uint64_t v = 0;
            if (s_quic_get_varint(&vp, val + len, &v) != 0) return -1;
            if (id == 0x05) c->peer_bidi_local = v;
            if (id == 0x06) c->peer_bidi_remote = v;
        } else if (id == 0x0f) {
            if (len != S_QUIC_CID_LEN || memcmp(val, c->peer_cid, S_QUIC_CID_LEN) != 0)
                return -1;
        } else if (id == 0x00 && from_server) {
            if (len != S_QUIC_CID_LEN || memcmp(val, c->orig_dcid, S_QUIC_CID_LEN) != 0)
                return -1;
        }
    }
    c->saw_params = 1;
    return 0;
}

static int s_quic_ext(uint8_t *p, size_t cap, uint16_t type, const uint8_t *data, size_t n) {
    if (cap < 4 + n) return -1;
    p[0] = (uint8_t)(type >> 8);
    p[1] = (uint8_t)type;
    p[2] = (uint8_t)(n >> 8);
    p[3] = (uint8_t)n;
    if (n) memcpy(p + 4, data, n);
    return (int)(4 + n);
}

static int s_quic_build_client_hello(s_quic_conn_t *c, uint8_t *out, size_t cap) {
    uint8_t body[1024];
    uint8_t exts[800];
    size_t bl = 0, el = 0;
    int n;
    uint8_t random[32];
    uint8_t ks[2 + 2 + 2 + 32];
    uint8_t params[256];
    int plen;
    if (RAND_bytes(random, 32) != 1) return -1;
    /* legacy_version, random, empty session id, one cipher, null compression */
    body[bl++] = 0x03; body[bl++] = 0x03;
    memcpy(body + bl, random, 32); bl += 32;
    body[bl++] = 0; /* session id len */
    body[bl++] = 0x00; body[bl++] = 0x02; /* cipher list len */
    body[bl++] = 0x13; body[bl++] = 0x01;
    body[bl++] = 1; body[bl++] = 0; /* compression */
    /* supported_versions */
    {
        uint8_t d[3] = { 0x02, 0x03, 0x04 };
        n = s_quic_ext(exts + el, sizeof(exts) - el, 0x002b, d, 3);
        if (n < 0) return -1;
        el += (size_t)n;
    }
    /* signature_algorithms: ed25519 */
    {
        uint8_t d[4] = { 0x00, 0x02, 0x08, 0x07 };
        n = s_quic_ext(exts + el, sizeof(exts) - el, 0x000d, d, 4);
        if (n < 0) return -1;
        el += (size_t)n;
    }
    /* supported_groups: x25519 */
    {
        uint8_t d[4] = { 0x00, 0x02, 0x00, 0x1d };
        n = s_quic_ext(exts + el, sizeof(exts) - el, 0x000a, d, 4);
        if (n < 0) return -1;
        el += (size_t)n;
    }
    /* key_share */
    ks[0] = 0; ks[1] = 36; /* shares length */
    ks[2] = 0x00; ks[3] = 0x1d;
    ks[4] = 0; ks[5] = 32;
    memcpy(ks + 6, c->my_pub, 32);
    n = s_quic_ext(exts + el, sizeof(exts) - el, 0x0033, ks, sizeof(ks));
    if (n < 0) return -1;
    el += (size_t)n;
    /* ALPN "susp" */
    {
        uint8_t d[1 + 1 + 4 + 1] = { 0x00, 0x05, 0x04, 's', 'u', 's', 'p' };
        /* list length is 2 bytes: 0x0005, then name len 4 + name. Total 7.
         * I packed 7 bytes starting with 0x00, 0x05. sizeof above is wrong. */
        uint8_t alpn[7];
        alpn[0] = 0x00; alpn[1] = 0x05; alpn[2] = 4;
        memcpy(alpn + 3, "susp", 4);
        n = s_quic_ext(exts + el, sizeof(exts) - el, 0x0010, alpn, 7);
        if (n < 0) return -1;
        el += (size_t)n;
        (void)d;
    }
    plen = s_quic_put_params(params, sizeof(params), c->local_cid, 0, NULL);
    if (plen < 0) return -1;
    n = s_quic_ext(exts + el, sizeof(exts) - el, 0x0039, params, (size_t)plen);
    if (n < 0) return -1;
    el += (size_t)n;
    if (bl + 2 + el > sizeof(body)) return -1;
    body[bl++] = (uint8_t)(el >> 8);
    body[bl++] = (uint8_t)el;
    memcpy(body + bl, exts, el);
    bl += el;
    if (cap < 4 + bl) return -1;
    out[0] = 1; /* ClientHello */
    out[1] = (uint8_t)(bl >> 16);
    out[2] = (uint8_t)(bl >> 8);
    out[3] = (uint8_t)bl;
    memcpy(out + 4, body, bl);
    return (int)(4 + bl);
}

static int s_quic_find_ext(const uint8_t *exts, size_t elen, uint16_t type,
                           const uint8_t **data, size_t *dlen) {
    const uint8_t *p = exts;
    const uint8_t *end = exts + elen;
    while (p + 4 <= end) {
        uint16_t t = (uint16_t)((p[0] << 8) | p[1]);
        uint16_t n = (uint16_t)((p[2] << 8) | p[3]);
        p += 4;
        if ((size_t)(end - p) < n) return -1;
        if (t == type) {
            *data = p;
            *dlen = n;
            return 0;
        }
        p += n;
    }
    return -1;
}

static int s_quic_parse_client_hello(s_quic_conn_t *c, const uint8_t *msg, size_t len) {
    const uint8_t *p, *end, *exts, *ks, *alpn, *params;
    size_t elen, klen, alen, plen;
    uint16_t ext_len;
    if (len < 4 || msg[0] != 1) {
        if (s_quic_trace()) fprintf(stderr, "quic: CH header %zu %u\n", len, len ? msg[0] : 0);
        return -1;
    }
    p = msg + 4;
    end = msg + len;
    if ((size_t)(end - p) < 35) return -1;
    p += 2 + 32; /* version + random */
    if (p >= end) return -1;
    p += 1u + p[0]; /* session id */
    if ((size_t)(end - p) < 2) return -1;
    {
        uint16_t cl = (uint16_t)((p[0] << 8) | p[1]);
        p += 2;
        if ((size_t)(end - p) < cl + 1) return -1;
        p += cl;
    }
    p += 1u + p[0]; /* compression */
    if ((size_t)(end - p) < 2) return -1;
    ext_len = (uint16_t)((p[0] << 8) | p[1]);
    p += 2;
    if ((size_t)(end - p) < ext_len) return -1;
    exts = p;
    elen = ext_len;
    if (s_quic_find_ext(exts, elen, 0x0033, &ks, &klen) != 0) return -1;
    /* client shares: u16 len, group, u16 key len, key */
    if (klen < 2 + 2 + 2 + 32) return -1;
    if (ks[2] != 0x00 || ks[3] != 0x1d) return -1;
    if (ks[4] != 0 || ks[5] != 32) return -1;
    memcpy(c->peer_pub, ks + 6, 32);
    c->have_peer_pub = 1;
    if (s_quic_find_ext(exts, elen, 0x0010, &alpn, &alen) != 0) return -1;
    if (alen < 7 || alpn[2] != 4 || memcmp(alpn + 3, "susp", 4) != 0) return -1;
    if (s_quic_find_ext(exts, elen, 0x0039, &params, &plen) != 0) return -1;
    /* peer_cid is the client's SCID, already copied from the packet before parse.
     * initial_source_connection_id must match it. */
    if (s_quic_take_params(c, params, plen, 0) != 0) return -1;
    return 0;
}

static int s_quic_build_server_hello(s_quic_conn_t *c, uint8_t *out, size_t cap) {
    uint8_t body[128];
    uint8_t exts[64];
    size_t bl = 0, el = 0;
    int n;
    uint8_t random[32];
    uint8_t ks[2 + 2 + 32];
    if (RAND_bytes(random, 32) != 1) return -1;
    body[bl++] = 0x03; body[bl++] = 0x03;
    memcpy(body + bl, random, 32); bl += 32;
    body[bl++] = 0; /* echo empty session id */
    body[bl++] = 0x13; body[bl++] = 0x01;
    body[bl++] = 0;
    {
        uint8_t d[2] = { 0x03, 0x04 };
        n = s_quic_ext(exts + el, sizeof(exts) - el, 0x002b, d, 2);
        if (n < 0) return -1;
        el += (size_t)n;
    }
    ks[0] = 0x00; ks[1] = 0x1d;
    ks[2] = 0; ks[3] = 32;
    memcpy(ks + 4, c->my_pub, 32);
    n = s_quic_ext(exts + el, sizeof(exts) - el, 0x0033, ks, sizeof(ks));
    if (n < 0) return -1;
    el += (size_t)n;
    if (bl + 2 + el > sizeof(body)) return -1;
    body[bl++] = (uint8_t)(el >> 8);
    body[bl++] = (uint8_t)el;
    memcpy(body + bl, exts, el);
    bl += el;
    if (cap < 4 + bl) return -1;
    out[0] = 2;
    out[1] = (uint8_t)(bl >> 16);
    out[2] = (uint8_t)(bl >> 8);
    out[3] = (uint8_t)bl;
    memcpy(out + 4, body, bl);
    return (int)(4 + bl);
}

static int s_quic_parse_server_hello(s_quic_conn_t *c, const uint8_t *msg, size_t len) {
    const uint8_t *p, *end, *exts, *ks;
    size_t elen, klen;
    uint16_t ext_len;
    if (len < 4 || msg[0] != 2) return -1;
    p = msg + 4;
    end = msg + len;
    if ((size_t)(end - p) < 35) return -1;
    p += 2 + 32;
    if (p >= end) return -1;
    p += 1u + p[0];
    if ((size_t)(end - p) < 3) return -1;
    if (p[0] != 0x13 || p[1] != 0x01) return -1;
    p += 3; /* suite + compression */
    if ((size_t)(end - p) < 2) return -1;
    ext_len = (uint16_t)((p[0] << 8) | p[1]);
    p += 2;
    if ((size_t)(end - p) < ext_len) return -1;
    exts = p;
    elen = ext_len;
    if (s_quic_find_ext(exts, elen, 0x0033, &ks, &klen) != 0) return -1;
    if (klen < 2 + 2 + 32 || ks[0] != 0x00 || ks[1] != 0x1d || ks[2] != 0 || ks[3] != 32)
        return -1;
    memcpy(c->peer_pub, ks + 4, 32);
    c->have_peer_pub = 1;
    return 0;
}

static int s_quic_build_encrypted_extensions(s_quic_conn_t *c, uint8_t *out, size_t cap) {
    uint8_t exts[400];
    uint8_t params[256];
    uint8_t alpn[6];
    size_t el = 0;
    int n, plen;
    alpn[0] = 4;
    memcpy(alpn + 1, "susp", 4);
    n = s_quic_ext(exts + el, sizeof(exts) - el, 0x0010, alpn, 5);
    if (n < 0) return -1;
    el += (size_t)n;
    plen = s_quic_put_params(params, sizeof(params), c->local_cid, 1, c->orig_dcid);
    if (plen < 0) return -1;
    n = s_quic_ext(exts + el, sizeof(exts) - el, 0x0039, params, (size_t)plen);
    if (n < 0) return -1;
    el += (size_t)n;
    if (cap < 4 + 2 + el) return -1;
    out[0] = 8;
    out[1] = (uint8_t)((el + 2) >> 16);
    out[2] = (uint8_t)((el + 2) >> 8);
    out[3] = (uint8_t)(el + 2);
    out[4] = (uint8_t)(el >> 8);
    out[5] = (uint8_t)el;
    memcpy(out + 6, exts, el);
    return (int)(6 + el);
}

static int s_quic_parse_ee(s_quic_conn_t *c, const uint8_t *msg, size_t len) {
    const uint8_t *p, *end, *params, *alpn;
    size_t plen, alen;
    uint16_t elen;
    if (len < 6 || msg[0] != 8) return -1;
    p = msg + 4;
    end = msg + len;
    elen = (uint16_t)((p[0] << 8) | p[1]);
    p += 2;
    if ((size_t)(end - p) < elen) return -1;
    if (s_quic_find_ext(p, elen, 0x0010, &alpn, &alen) != 0) return -1;
    if (alen < 5 || alpn[0] != 4 || memcmp(alpn + 1, "susp", 4) != 0) return -1;
    if (s_quic_find_ext(p, elen, 0x0039, &params, &plen) != 0) return -1;
    return s_quic_take_params(c, params, plen, 1);
}

static int s_quic_build_certificate(uint8_t *out, size_t cap) {
    size_t der_len = (size_t)s_quic_cert_der_len;
    size_t list;
    size_t body;
    if (s_quic_cert_der_len <= 0) return -1;
    /* context(1)=0, list len(3), cert len(3), der, exts(2)=0 */
    list = 3 + der_len + 2;
    body = 1 + 3 + list;
    if (cap < 4 + body) return -1;
    out[0] = 11;
    out[1] = (uint8_t)(body >> 16);
    out[2] = (uint8_t)(body >> 8);
    out[3] = (uint8_t)body;
    out[4] = 0; /* context */
    out[5] = (uint8_t)(list >> 16);
    out[6] = (uint8_t)(list >> 8);
    out[7] = (uint8_t)list;
    out[8] = (uint8_t)(der_len >> 16);
    out[9] = (uint8_t)(der_len >> 8);
    out[10] = (uint8_t)der_len;
    memcpy(out + 11, s_quic_cert_der, der_len);
    out[11 + der_len] = 0;
    out[12 + der_len] = 0;
    return (int)(13 + der_len);
}

static int s_quic_parse_certificate(s_quic_conn_t *c, const uint8_t *msg, size_t len) {
    const uint8_t *p;
    uint32_t list, clen;
    if (len < 13 || msg[0] != 11) return -1;
    p = msg + 4;
    if (p[0] != 0) return -1;
    list = ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    clen = ((uint32_t)p[4] << 16) | ((uint32_t)p[5] << 8) | p[6];
    if (11u + clen + 2u > len || clen > sizeof(c->cert_der)) return -1;
    (void)list;
    memcpy(c->cert_der, p + 7, clen);
    c->cert_der_len = clen;
    return 0;
}

static int s_quic_cert_verify_tbs(s_quic_conn_t *c, uint8_t *tbs, size_t tbs_cap, size_t *tbs_len) {
    uint8_t th[32];
    const char *ctx = "TLS 1.3, server CertificateVerify";
    size_t ctx_len = 33; /* includes trailing NUL */
    if (tbs_cap < 64 + ctx_len + 32) return -1;
    if (s_quic_hash_peek(c, th) != 0) return -1;
    memset(tbs, 0x20, 64);
    memcpy(tbs + 64, ctx, ctx_len);
    memcpy(tbs + 64 + ctx_len, th, 32);
    *tbs_len = 64 + ctx_len + 32;
    return 0;
}

static int s_quic_build_cert_verify(s_quic_conn_t *c, uint8_t *out, size_t cap) {
    uint8_t tbs[160];
    size_t tbs_len = 0;
    EVP_MD_CTX *md = EVP_MD_CTX_new();
    uint8_t sig[128];
    size_t sig_len = sizeof(sig);
    size_t body;
    if (!md || !s_quic_cert_key) { EVP_MD_CTX_free(md); return -1; }
    if (s_quic_cert_verify_tbs(c, tbs, sizeof(tbs), &tbs_len) != 0) {
        EVP_MD_CTX_free(md);
        return -1;
    }
    /* Ed25519: one-shot DigestSign with a NULL digest. */
    if (EVP_DigestSignInit(md, NULL, NULL, NULL, s_quic_cert_key) != 1 ||
        EVP_DigestSign(md, sig, &sig_len, tbs, tbs_len) != 1) {
        if (s_quic_trace())
            fprintf(stderr, "quic: sign %s\n", ERR_error_string(ERR_get_error(), NULL));
        EVP_MD_CTX_free(md);
        return -1;
    }
    EVP_MD_CTX_free(md);
    body = 2 + 2 + sig_len;
    if (cap < 4 + body || sig_len > 65535) return -1;
    out[0] = 15;
    out[1] = (uint8_t)(body >> 16);
    out[2] = (uint8_t)(body >> 8);
    out[3] = (uint8_t)body;
    out[4] = 0x08; out[5] = 0x07; /* ed25519 */
    out[6] = (uint8_t)(sig_len >> 8);
    out[7] = (uint8_t)sig_len;
    memcpy(out + 8, sig, sig_len);
    return (int)(8 + sig_len);
}

static int s_quic_parse_cert_verify(s_quic_conn_t *c, const uint8_t *msg, size_t len) {
    const unsigned char *der;
    X509 *x;
    EVP_PKEY *pk;
    EVP_MD_CTX *md;
    uint8_t tbs[160];
    size_t tbs_len = 0;
    uint16_t sig_len;
    int ok;
    if (len < 8 || msg[0] != 15) return -1;
    if (msg[4] != 0x08 || msg[5] != 0x07) return -1;
    sig_len = (uint16_t)((msg[6] << 8) | msg[7]);
    if ((size_t)sig_len + 8 > len || c->cert_der_len <= 0) return -1;
    der = c->cert_der;
    x = d2i_X509(NULL, &der, (long)c->cert_der_len);
    if (!x) return -1;
    pk = X509_get_pubkey(x);
    X509_free(x);
    if (!pk) return -1;
    if (s_quic_cert_verify_tbs(c, tbs, sizeof(tbs), &tbs_len) != 0) {
        EVP_PKEY_free(pk);
        return -1;
    }
    md = EVP_MD_CTX_new();
    if (!md) { EVP_PKEY_free(pk); return -1; }
    ok = EVP_DigestVerifyInit(md, NULL, NULL, NULL, pk) == 1
      && EVP_DigestVerify(md, msg + 8, sig_len, tbs, tbs_len) == 1;
    EVP_MD_CTX_free(md);
    EVP_PKEY_free(pk);
    if (!ok && s_quic_trace())
        fprintf(stderr, "quic: verify %s\n", ERR_error_string(ERR_get_error(), NULL));
    return ok ? 0 : -1;
}

static int s_quic_build_finished(s_quic_conn_t *c, int sender_is_server, uint8_t *out, size_t cap) {
    uint8_t secret_label_src[32];
    uint8_t fin_key[32], th[32], verify[32];
    const s_quic_keys_t *k = sender_is_server ? &c->keys[1][S_QUIC_HANDSHAKE]
                                              : &c->keys[0][S_QUIC_HANDSHAKE];
    if (!k->have) return -1;
    memcpy(secret_label_src, k->secret, 32);
    if (s_quic_expand_label(secret_label_src, "finished", NULL, 0, fin_key, 32) != 0) return -1;
    if (s_quic_hash_peek(c, th) != 0) return -1;
    if (s_quic_hmac(fin_key, 32, th, 32, verify) != 0) return -1;
    if (cap < 4 + 32) return -1;
    out[0] = 20;
    out[1] = 0; out[2] = 0; out[3] = 32;
    memcpy(out + 4, verify, 32);
    OPENSSL_cleanse(fin_key, sizeof(fin_key));
    return 36;
}

static int s_quic_check_finished(s_quic_conn_t *c, int sender_is_server,
                                 const uint8_t *msg, size_t len) {
    uint8_t fin_key[32], th[32], verify[32];
    const s_quic_keys_t *k = sender_is_server ? &c->keys[1][S_QUIC_HANDSHAKE]
                                              : &c->keys[0][S_QUIC_HANDSHAKE];
    if (len != 36 || msg[0] != 20) return -1;
    if (s_quic_expand_label(k->secret, "finished", NULL, 0, fin_key, 32) != 0) return -1;
    if (s_quic_hash_peek(c, th) != 0) return -1;
    if (s_quic_hmac(fin_key, 32, th, 32, verify) != 0) return -1;
    if (memcmp(verify, msg + 4, 32) != 0) return -1;
    return 0;
}

/* Forward: packet send and handshake progression call each other. */
static int s_quic_send_frames(s_quic_conn_t *c, int level, const uint8_t *frames,
                              size_t flen, int retransmit, int pad_to);
static int s_quic_client_finish(s_quic_conn_t *c);
static int s_quic_server_flight(s_quic_conn_t *c);

static int s_quic_on_msg(s_quic_conn_t *c, int level, const uint8_t *msg, size_t len) {
    if (len < 4) return -1;
    if (!c->is_server && level == S_QUIC_INITIAL && msg[0] == 2) {
        if (s_quic_parse_server_hello(c, msg, len) != 0) { s_quic_log("bad SH"); return -1; }
        if (s_quic_hash_update(c, msg, len) != 0) return -1;
        return s_quic_derive_handshake(c);
    }
    if (!c->is_server && level == S_QUIC_HANDSHAKE && msg[0] == 8) {
        if (s_quic_parse_ee(c, msg, len) != 0) return -1;
        return s_quic_hash_update(c, msg, len);
    }
    if (!c->is_server && level == S_QUIC_HANDSHAKE && msg[0] == 11) {
        if (s_quic_parse_certificate(c, msg, len) != 0) return -1;
        return s_quic_hash_update(c, msg, len);
    }
    if (!c->is_server && level == S_QUIC_HANDSHAKE && msg[0] == 15) {
        if (s_quic_parse_cert_verify(c, msg, len) != 0) return -1;
        return s_quic_hash_update(c, msg, len);
    }
    if (!c->is_server && level == S_QUIC_HANDSHAKE && msg[0] == 20) {
        if (s_quic_check_finished(c, 1, msg, len) != 0) return -1;
        if (s_quic_hash_update(c, msg, len) != 0) return -1;
        if (s_quic_derive_app(c) != 0) return -1;
        return s_quic_client_finish(c);
    }
    if (c->is_server && level == S_QUIC_INITIAL && msg[0] == 1) {
        if (c->ch_done) return 0;
        if (s_quic_parse_client_hello(c, msg, len) != 0) return -1;
        if (s_quic_hash_update(c, msg, len) != 0) return -1;
        c->ch_done = 1;
        return s_quic_server_flight(c);
    }
    if (c->is_server && level == S_QUIC_HANDSHAKE && msg[0] == 20) {
        if (s_quic_check_finished(c, 0, msg, len) != 0) return -1;
        if (s_quic_hash_update(c, msg, len) != 0) return -1;
        c->app_ready = 1;
        return 0;
    }
    if (s_quic_trace())
        fprintf(stderr, "quic: unexpected msg type %u level %d server %d\n",
                msg[0], level, c->is_server);
    return -1;
}

static int s_quic_crypto_input(s_quic_conn_t *c, int level, uint64_t offset,
                               const uint8_t *data, size_t len) {
    if (level > S_QUIC_HANDSHAKE) return 0;
    if (offset > S_QUIC_CRYPTO_MAX || len > S_QUIC_CRYPTO_MAX - offset) return -1;
    if (offset == c->rx_crypto_len[level]) {
        memcpy(c->rx_crypto[level] + offset, data, len);
        c->rx_crypto_len[level] += len;
    } else if (offset + len <= c->rx_crypto_len[level]) {
        return 0; /* duplicate */
    } else {
        return -1; /* gap */
    }
    while (c->rx_crypto_parsed[level] + 4 <= c->rx_crypto_len[level]) {
        const uint8_t *m = c->rx_crypto[level] + c->rx_crypto_parsed[level];
        uint32_t bl = ((uint32_t)m[1] << 16) | ((uint32_t)m[2] << 8) | m[3];
        size_t total = 4u + bl;
        if (c->rx_crypto_parsed[level] + total > c->rx_crypto_len[level]) break;
        if (s_quic_on_msg(c, level, m, total) != 0) return -1;
        c->rx_crypto_parsed[level] += total;
    }
    return 0;
}

static int s_quic_stream_input(s_quic_conn_t *c, uint64_t offset, const uint8_t *data,
                               size_t len, int fin) {
    if (offset > S_QUIC_STREAM_MAX || len > S_QUIC_STREAM_MAX - offset) return -1;
    if (!c->rx) {
        c->rx_cap = 8192;
        c->rx = (uint8_t *)memento_thread_heap_alloc(memento_thread_heap_get(), c->rx_cap);
        if (!c->rx) return -1;
    }
    if (offset + len > c->rx_cap) {
        size_t ncap = c->rx_cap;
        uint8_t *nbuf;
        while (ncap < offset + len) {
            if (ncap > S_QUIC_STREAM_MAX / 2) return -1;
            ncap *= 2;
        }
        nbuf = (uint8_t *)memento_thread_heap_alloc(memento_thread_heap_get(), ncap);
        if (!nbuf) return -1;
        if (c->rx_len) memcpy(nbuf, c->rx, c->rx_len);
        memento_thread_heap_free(memento_thread_heap_get(), c->rx, c->rx_cap);
        c->rx = nbuf;
        c->rx_cap = ncap;
    }
    if (offset == c->rx_len) {
        memcpy(c->rx + offset, data, len);
        c->rx_len += len;
    } else if (offset + len <= c->rx_len) {
        /* duplicate */
    } else {
        return -1;
    }
    if (fin) c->rx_fin = 1;
    return 0;
}

static int s_quic_note_rx(s_quic_conn_t *c, int level, uint64_t pn) {
    if (!c->rx_have[level]) {
        c->rx_have[level] = 1;
        c->rx_max[level] = pn;
        c->rx_mask[level] = 1;
        return 0;
    }
    if (pn > c->rx_max[level]) {
        uint64_t shift = pn - c->rx_max[level];
        if (shift >= 64) c->rx_mask[level] = 1;
        else c->rx_mask[level] = (c->rx_mask[level] << shift) | 1ull;
        c->rx_max[level] = pn;
        return 0;
    }
    {
        uint64_t d = c->rx_max[level] - pn;
        if (d >= 64) return 0;
        if (c->rx_mask[level] & (1ull << d)) return 1; /* dup */
        c->rx_mask[level] |= 1ull << d;
    }
    return 0;
}

static int s_quic_ack_covers(s_quic_conn_t *c, int level, uint64_t pn) {
    (void)c; (void)level; (void)pn;
    return 1;
}

static int s_quic_encode_ack(s_quic_conn_t *c, int level, uint8_t *p, size_t cap) {
    uint64_t first = 0;
    int n = 0, k;
    if (!c->rx_have[level] || cap < 8) return 0;
    while (first < 63 && (c->rx_mask[level] & (1ull << (first + 1)))) first++;
    p[n++] = 0x02;
    k = s_quic_put_varint(p + n, cap - (size_t)n, c->rx_max[level]);
    if (k < 0) return -1;
    n += k;
    k = s_quic_put_varint(p + n, cap - (size_t)n, 0); /* delay */
    if (k < 0) return -1;
    n += k;
    k = s_quic_put_varint(p + n, cap - (size_t)n, 0); /* range count */
    if (k < 0) return -1;
    n += k;
    k = s_quic_put_varint(p + n, cap - (size_t)n, first);
    if (k < 0) return -1;
    n += k;
    return n;
}

static void s_quic_apply_ack(s_quic_conn_t *c, int level, uint64_t largest, uint64_t first) {
    uint64_t pn;
    uint64_t low = largest >= first ? largest - first : 0;
    if (!c->flight[level].live) return;
    pn = c->flight[level].pn;
    if (pn <= largest && pn >= low) c->flight[level].live = 0;
}

static int s_quic_parse_frames(s_quic_conn_t *c, int level, uint8_t *plain, size_t len) {
    size_t off = 0;
    while (off < len) {
        uint8_t t = plain[off++];
        if (t == 0x00 || t == 0x01) continue;
        if (t == 0x1e) continue; /* HANDSHAKE_DONE */
        if (t == 0x02 || t == 0x03) {
            const uint8_t *p = plain + off;
            const uint8_t *end = plain + len;
            uint64_t largest, delay, range_count, first;
            uint64_t i;
            if (s_quic_get_varint(&p, end, &largest) ||
                s_quic_get_varint(&p, end, &delay) ||
                s_quic_get_varint(&p, end, &range_count) ||
                s_quic_get_varint(&p, end, &first)) return -1;
            s_quic_apply_ack(c, level, largest, first);
            for (i = 0; i < range_count; i++) {
                uint64_t gap, ar;
                if (s_quic_get_varint(&p, end, &gap) || s_quic_get_varint(&p, end, &ar))
                    return -1;
            }
            if (t == 0x03) {
                /* ECN counts: three varints. Ignore contents. */
                uint64_t a, b, d;
                if (s_quic_get_varint(&p, end, &a) || s_quic_get_varint(&p, end, &b) ||
                    s_quic_get_varint(&p, end, &d)) return -1;
            }
            off = (size_t)(p - plain);
            continue;
        }
        if (t == 0x06) {
            const uint8_t *p = plain + off;
            const uint8_t *end = plain + len;
            uint64_t offset, clen;
            if (s_quic_get_varint(&p, end, &offset) || s_quic_get_varint(&p, end, &clen))
                return -1;
            if ((size_t)(end - p) < clen) return -1;
            if (s_quic_crypto_input(c, level, offset, p, (size_t)clen) != 0) return -1;
            off = (size_t)(p + clen - plain);
            continue;
        }
        if ((t & 0xf8) == 0x08) {
            const uint8_t *p = plain + off;
            const uint8_t *end = plain + len;
            uint64_t sid, offset = 0, slen;
            if (s_quic_get_varint(&p, end, &sid)) return -1;
            if (t & 0x04) {
                if (s_quic_get_varint(&p, end, &offset)) return -1;
            }
            if (t & 0x02) {
                if (s_quic_get_varint(&p, end, &slen)) return -1;
            } else {
                slen = (uint64_t)(end - p);
            }
            if ((uint64_t)(end - p) < slen) return -1;
            if (sid == 0) {
                if (s_quic_stream_input(c, offset, p, (size_t)slen, t & 0x01) != 0) return -1;
            }
            off = (size_t)(p + slen - plain);
            continue;
        }
        if (t == 0x1c || t == 0x1d) {
            c->closed = 1;
            return 0;
        }
        s_quic_log("unknown frame");
        return -1;
    }
    (void)s_quic_ack_covers;
    return 0;
}

static int s_quic_open_packet(s_quic_conn_t *c, uint8_t *pkt, size_t pkt_len, size_t *consumed) {
    int long_hdr = (pkt[0] & 0x80) != 0;
    size_t off = 1;
    int level = S_QUIC_APP;
    size_t pn_offset, pn_len, payload_len, sample_at;
    uint8_t sample[16], mask[16];
    s_quic_keys_t *k;
    uint64_t pn = 0;
    size_t i;
    uint8_t plain[1500];
    uint8_t tag[16];
    /* -2 = discard (undecryptable / not for us). -1 = decrypted, then illegal. */
    if (pkt_len < 20) return -2;
    if (long_hdr) {
        uint64_t token_len = 0, length = 0;
        int type;
        if (pkt_len < 7) return -2;
        if (((uint32_t)pkt[1] << 24 | (uint32_t)pkt[2] << 16 |
             (uint32_t)pkt[3] << 8 | pkt[4]) != 0x00000001u) {
            return -2;
        }
        off = 5;
        if (off >= pkt_len) return -2;
        if (pkt[off] != S_QUIC_CID_LEN) return -2;
        off++;
        if (off + S_QUIC_CID_LEN > pkt_len) return -2;
        /* DCID must be ours */
        if (memcmp(pkt + off, c->local_cid, S_QUIC_CID_LEN) != 0) return -2;
        off += S_QUIC_CID_LEN;
        if (off >= pkt_len || pkt[off] != S_QUIC_CID_LEN) return -2;
        off++;
        if (off + S_QUIC_CID_LEN > pkt_len) return -2;
        if (!c->peer_set || c->peer_cid[0] == 0) {
            /* peer cid learned from their SCID; dialer already knows it */
        }
        if (memcmp(c->peer_cid, pkt + off, S_QUIC_CID_LEN) != 0 && c->is_server && !c->ch_done) {
            memcpy(c->peer_cid, pkt + off, S_QUIC_CID_LEN);
        }
        off += S_QUIC_CID_LEN;
        type = (pkt[0] & 0x30) >> 4;
        if (type == 0) {
            const uint8_t *tp = pkt + off;
            if (s_quic_get_varint(&tp, pkt + pkt_len, &token_len) != 0) return -2;
            off = (size_t)(tp - pkt);
            if (off + token_len > pkt_len) return -2;
            off += (size_t)token_len;
            level = S_QUIC_INITIAL;
        } else if (type == 2) {
            level = S_QUIC_HANDSHAKE;
        } else {
            return -2;
        }
        {
            const uint8_t *lp = pkt + off;
            if (s_quic_get_varint(&lp, pkt + pkt_len, &length) != 0) return -2;
            off = (size_t)(lp - pkt);
        }
        if (off + length > pkt_len) return -2;
        pn_offset = off;
        *consumed = off + (size_t)length;
    } else {
        if (pkt_len < 1 + S_QUIC_CID_LEN + 4 + 16) return -2;
        if (memcmp(pkt + 1, c->local_cid, S_QUIC_CID_LEN) != 0) return -2;
        pn_offset = 1 + S_QUIC_CID_LEN;
        *consumed = pkt_len;
        level = S_QUIC_APP;
    }
    k = &c->keys[c->is_server ? 0 : 1][level];
    if (!k->have) return -2;
    sample_at = pn_offset + 4;
    if (sample_at + 16 > *consumed) return -2;
    memcpy(sample, pkt + sample_at, 16);
    if (s_quic_hp_mask(k->hp, sample, mask) != 0) return -2;
    if (long_hdr) pkt[0] ^= (uint8_t)(mask[0] & 0x0f);
    else pkt[0] ^= (uint8_t)(mask[0] & 0x1f);
    pn_len = (size_t)((pkt[0] & 0x03) + 1);
    if (pn_offset + pn_len + 16 > *consumed) return -2;
    for (i = 0; i < pn_len; i++) pkt[pn_offset + i] ^= mask[1 + i];
    pn = 0;
    for (i = 0; i < pn_len; i++) pn = (pn << 8) | pkt[pn_offset + i];
    payload_len = *consumed - (pn_offset + pn_len);
    if (payload_len < 16 || payload_len - 16 > sizeof(plain)) return -2;
    memcpy(tag, pkt + *consumed - 16, 16);
    if (s_quic_aead(0, k->key, k->iv, pn, pkt, pn_offset + pn_len,
                    pkt + pn_offset + pn_len, payload_len - 16, plain, tag) != 0) {
        if (s_quic_trace()) fprintf(stderr, "quic: aead fail level %d pn %llu pay %zu\n",
                                    level, (unsigned long long)pn, payload_len);
        return -2;
    }
    if (s_quic_trace()) fprintf(stderr, "quic: opened level %d pn %llu plain0=%u plen=%zu\n",
                                level, (unsigned long long)pn, plain[0], payload_len - 16);
    if (s_quic_note_rx(c, level, pn) == 1) return 0; /* duplicate */
    return s_quic_parse_frames(c, level, plain, payload_len - 16);
}

static int s_quic_ingest(s_quic_conn_t *c, uint8_t *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        size_t consumed = 0;
        int rc = s_quic_open_packet(c, buf + off, len - off, &consumed);
        if (rc == -2) {
            /* Undecryptable: drop it. A 1-RTT failure still ends the conn. */
            if (c->app_ready && !(buf[off] & 0x80)) return -1;
            return 0;
        }
        if (rc != 0) return -1;
        if (consumed == 0) return -1;
        off += consumed;
        if (!(buf[off - consumed] & 0x80)) break;
    }
    return 0;
}

static int s_quic_send_frames(s_quic_conn_t *c, int level, const uint8_t *frames,
                              size_t flen, int retransmit, int pad_to) {
    uint8_t pkt[1500];
    uint8_t plain[1400];
    uint8_t tag[16];
    int from_server = c->is_server;
    s_quic_keys_t *k = &c->keys[from_server][level];
    uint64_t pn;
    size_t hdr, plain_len, length_size, lp, i;
    size_t fixed;
    int long_hdr = level != S_QUIC_APP;
    if (!k->have) return -1;
    pn = c->tx_pn[level]++;
    plain_len = flen;
    if (flen > sizeof(plain)) return -1;
    memcpy(plain, frames, flen);
    if (long_hdr) {
        length_size = 2;
        fixed = 1 + 4 + 1 + S_QUIC_CID_LEN + 1 + S_QUIC_CID_LEN;
        if (level == S_QUIC_INITIAL) fixed += 1; /* token length 0 */
        for (i = 0; i < 2; i++) {
            hdr = fixed + length_size + 4;
            plain_len = flen;
            if (pad_to && (size_t)pad_to > hdr + 16 && (size_t)pad_to - hdr - 16 > plain_len)
                plain_len = (size_t)pad_to - hdr - 16;
            lp = 4 + plain_len + 16;
            if (s_quic_varint_size(lp) == length_size) break;
            length_size = s_quic_varint_size(lp);
        }
        if (plain_len > sizeof(plain)) return -1;
        if (plain_len > flen) memset(plain + flen, 0, plain_len - flen);
        hdr = fixed + length_size + 4;
        if (hdr + plain_len + 16 > sizeof(pkt)) return -1;
        pkt[0] = (uint8_t)(0xc0 | ((level == S_QUIC_INITIAL ? 0 : 2) << 4) | 0x03);
        pkt[1] = 0; pkt[2] = 0; pkt[3] = 0; pkt[4] = 1;
        pkt[5] = S_QUIC_CID_LEN;
        memcpy(pkt + 6, c->peer_cid, S_QUIC_CID_LEN);
        pkt[6 + S_QUIC_CID_LEN] = S_QUIC_CID_LEN;
        memcpy(pkt + 7 + S_QUIC_CID_LEN, c->local_cid, S_QUIC_CID_LEN);
        {
            size_t o = 7 + 2 * S_QUIC_CID_LEN;
            if (level == S_QUIC_INITIAL) pkt[o++] = 0;
            lp = 4 + plain_len + 16;
            if (s_quic_put_varint(pkt + o, length_size, lp) != (int)length_size) return -1;
            o += length_size;
            pkt[o] = (uint8_t)(pn >> 24);
            pkt[o + 1] = (uint8_t)(pn >> 16);
            pkt[o + 2] = (uint8_t)(pn >> 8);
            pkt[o + 3] = (uint8_t)pn;
            if (s_quic_aead(1, k->key, k->iv, pn, pkt, o + 4, plain, plain_len,
                            pkt + o + 4, tag) != 0) return -1;
            memcpy(pkt + o + 4 + plain_len, tag, 16);
            {
                uint8_t sample[16], mask[16];
                memcpy(sample, pkt + o + 4, 16);
                if (s_quic_hp_mask(k->hp, sample, mask) != 0) return -1;
                pkt[0] ^= (uint8_t)(mask[0] & 0x0f);
                for (i = 0; i < 4; i++) pkt[o + i] ^= mask[1 + i];
            }
            if (s_io_sendto(c->fd, pkt, o + 4 + plain_len + 16,
                            (struct sockaddr *)&c->peer, sizeof(c->peer)) < 0) return -1;
        }
    } else {
        size_t o;
        hdr = 1 + S_QUIC_CID_LEN + 4;
        if (hdr + plain_len + 16 > sizeof(pkt)) return -1;
        pkt[0] = 0x43;
        memcpy(pkt + 1, c->peer_cid, S_QUIC_CID_LEN);
        o = 1 + S_QUIC_CID_LEN;
        pkt[o] = (uint8_t)(pn >> 24);
        pkt[o + 1] = (uint8_t)(pn >> 16);
        pkt[o + 2] = (uint8_t)(pn >> 8);
        pkt[o + 3] = (uint8_t)pn;
        if (s_quic_aead(1, k->key, k->iv, pn, pkt, o + 4, plain, plain_len,
                        pkt + o + 4, tag) != 0) return -1;
        memcpy(pkt + o + 4 + plain_len, tag, 16);
        {
            uint8_t sample[16], mask[16];
            memcpy(sample, pkt + o + 4, 16);
            if (s_quic_hp_mask(k->hp, sample, mask) != 0) return -1;
            pkt[0] ^= (uint8_t)(mask[0] & 0x1f);
            for (i = 0; i < 4; i++) pkt[o + i] ^= mask[1 + i];
        }
        if (s_io_sendto(c->fd, pkt, o + 4 + plain_len + 16,
                        (struct sockaddr *)&c->peer, sizeof(c->peer)) < 0) return -1;
    }
    if (retransmit && flen <= sizeof(c->flight[level].frames)) {
        memcpy(c->flight[level].frames, frames, flen);
        c->flight[level].flen = (uint16_t)flen;
        c->flight[level].pn = pn;
        c->flight[level].sent_ns = suspenders_now_ns();
        c->flight[level].live = 1;
    }
    return 0;
}

static int s_quic_send_crypto(s_quic_conn_t *c, int level, const uint8_t *msg, size_t len,
                              uint64_t crypto_off, int pad_to) {
    uint8_t fr[1400];
    size_t n = 0;
    int a, k;
    a = s_quic_encode_ack(c, level, fr, sizeof(fr));
    if (a < 0) return -1;
    n = (size_t)a;
    if (n + 1 + 16 + len > sizeof(fr)) return -1;
    fr[n++] = 0x06;
    k = s_quic_put_varint(fr + n, sizeof(fr) - n, crypto_off);
    if (k < 0) return -1;
    n += (size_t)k;
    k = s_quic_put_varint(fr + n, sizeof(fr) - n, len);
    if (k < 0) return -1;
    n += (size_t)k;
    memcpy(fr + n, msg, len);
    n += len;
    return s_quic_send_frames(c, level, fr, n, 1, pad_to);
}

static int s_quic_server_flight(s_quic_conn_t *c) {
    uint8_t sh[256], ee[512], cert[1200], cv[200], fin[40];
    uint8_t hs[4096];
    int nsh, nee, nc, nv, nf;
    size_t hsl = 0;
    pthread_once(&s_quic_cert_once, s_quic_make_cert);
    if (!s_quic_cert_key) { s_quic_log("no cert"); return -1; }
    if (s_quic_x25519(c) != 0) { s_quic_log("x25519"); return -1; }
    nsh = s_quic_build_server_hello(c, sh, sizeof(sh));
    if (nsh < 0) { s_quic_log("sh"); return -1; }
    if (s_quic_hash_update(c, sh, (size_t)nsh) != 0) return -1;
    if (s_quic_derive_handshake(c) != 0) { s_quic_log("hs keys"); return -1; }
    if (s_quic_send_crypto(c, S_QUIC_INITIAL, sh, (size_t)nsh, 0, 0) != 0) {
        s_quic_log("send sh"); return -1;
    }
    nee = s_quic_build_encrypted_extensions(c, ee, sizeof(ee));
    nc = s_quic_build_certificate(cert, sizeof(cert));
    if (nee < 0 || nc < 0) {
        if (s_quic_trace()) fprintf(stderr, "quic: ee %d cert %d\n", nee, nc);
        return -1;
    }
    if (s_quic_hash_update(c, ee, (size_t)nee) != 0) return -1;
    if (s_quic_hash_update(c, cert, (size_t)nc) != 0) return -1;
    nv = s_quic_build_cert_verify(c, cv, sizeof(cv));
    if (nv < 0) { s_quic_log("cv"); return -1; }
    if (s_quic_hash_update(c, cv, (size_t)nv) != 0) return -1;
    nf = s_quic_build_finished(c, 1, fin, sizeof(fin));
    if (nf < 0) { s_quic_log("fin"); return -1; }
    if (s_quic_hash_update(c, fin, (size_t)nf) != 0) return -1;
    if (s_quic_derive_app(c) != 0) { s_quic_log("app keys"); return -1; }
    if ((size_t)nee + (size_t)nc + (size_t)nv + (size_t)nf > sizeof(hs)) {
        s_quic_log("hs overflow"); return -1;
    }
    memcpy(hs, ee, (size_t)nee); hsl = (size_t)nee;
    memcpy(hs + hsl, cert, (size_t)nc); hsl += (size_t)nc;
    memcpy(hs + hsl, cv, (size_t)nv); hsl += (size_t)nv;
    memcpy(hs + hsl, fin, (size_t)nf); hsl += (size_t)nf;
    if (s_quic_trace()) fprintf(stderr, "quic: sending handshake flight %zu\n", hsl);
    if (s_quic_send_crypto(c, S_QUIC_HANDSHAKE, hs, hsl, 0, 0) != 0) {
        s_quic_log("send hs"); return -1;
    }
    return 0;
}

static int s_quic_client_finish(s_quic_conn_t *c) {
    uint8_t fin[40];
    int n;
    if (c->client_fin_sent) return 0;
    n = s_quic_build_finished(c, 0, fin, sizeof(fin));
    if (n < 0) return -1;
    if (s_quic_hash_update(c, fin, (size_t)n) != 0) return -1;
    if (s_quic_send_crypto(c, S_QUIC_HANDSHAKE, fin, (size_t)n, 0, 0) != 0) return -1;
    c->client_fin_sent = 1;
    c->app_ready = 1;
    return 0;
}

static int s_quic_retransmit(s_quic_conn_t *c) {
    int level;
    uint64_t now = suspenders_now_ns();
    for (level = 0; level < 3; level++) {
        s_quic_flight_t *f = &c->flight[level];
        if (!f->live) continue;
        if (c->app_ready && level != S_QUIC_APP) {
            f->live = 0;
            continue;
        }
        if (now < f->sent_ns + 40000000ull) continue;
        if (s_quic_send_frames(c, level, f->frames, f->flen, 1, level == S_QUIC_INITIAL && !c->is_server ? S_QUIC_INITIAL_PAD : 0) != 0)
            return -1;
    }
    return 0;
}

static int s_quic_drive(s_quic_conn_t *c, int want_app) {
    uint64_t deadline = suspenders_now_ns() + 5000000000ull;
    while (suspenders_now_ns() < deadline) {
        uint8_t buf[1500];
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        ssize_t n;
        uint64_t saved;
        if (c->closed) return -1;
        if (want_app && c->app_ready) return 0;
        if (s_quic_retransmit(c) != 0) return -1;
        saved = s_io_deadline_ns;
        s_io_deadline_ns = suspenders_now_ns() + 50000000ull;
        n = s_io_recvfrom(c->fd, buf, sizeof(buf), (struct sockaddr *)&from, &fl);
        s_io_deadline_ns = saved;
        if (n < 0) {
            if (suspenders_errno == SUSPENDERS_TIMEDOUT) continue;
            return -1;
        }
        if (!c->peer_set) {
            c->peer = from;
            c->peer_set = 1;
        }
        if (s_quic_ingest(c, buf, (size_t)n) != 0) return -1;
        if (want_app && c->app_ready) return 0;
    }
    suspenders_errno = SUSPENDERS_TIMEDOUT;
    return -1;
}

static void s_quic_conn_free(s_quic_conn_t *c) {
    if (!c) return;
    if (c->rx) {
        memento_thread_heap_free(memento_thread_heap_get(), c->rx, c->rx_cap);
    }
    EVP_PKEY_free(c->x25519);
    EVP_MD_CTX_free(c->transcript);
    OPENSSL_cleanse(c->shared, sizeof(c->shared));
    OPENSSL_cleanse(c->hs_secret, sizeof(c->hs_secret));
    memento_thread_heap_free(memento_thread_heap_get(), c, sizeof(*c));
}

static s_quic_conn_t *s_quic_conn_new(void) {
    memento_thread_heap_t *h = memento_thread_heap_get();
    s_quic_conn_t *c = (s_quic_conn_t *)memento_thread_heap_alloc(h, sizeof(*c));
    if (!c) return NULL;
    memset(c, 0, sizeof(*c));
    c->fd = SUSPENDERS_INVALID_SOCK;
    c->peer_bidi_local = S_QUIC_FLOW;
    c->peer_bidi_remote = S_QUIC_FLOW;
    return c;
}

static int s_quic_udp(suspenders_hose_t *h, const char *host, int port, int listen_mode) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (listen_mode) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        return -1;
    }
    h->fd = suspenders_socket(AF_INET, SOCK_DGRAM, 0);
    if (h->fd == SUSPENDERS_INVALID_SOCK) return -1;
    if (!suspenders_set_nonblocking(h->fd)) {
        suspenders_close_socket(h->fd);
        h->fd = SUSPENDERS_INVALID_SOCK;
        return -1;
    }
    if (listen_mode) {
        int opt = 1;
        setsockopt(h->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        if (bind(h->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            suspenders_close_socket(h->fd);
            h->fd = SUSPENDERS_INVALID_SOCK;
            return -1;
        }
    }
    return 0;
}

static bool quic_dial(suspenders_hose_t *h, const char *host, int port) {
    s_quic_conn_t *c;
    uint8_t ch[1024];
    int n;
    if (s_quic_selftest_keys() != 0) return false;
    if (s_quic_udp(h, host, port, 0) != 0) return false;
    c = s_quic_conn_new();
    if (!c) {
        suspenders_close_socket(h->fd);
        h->fd = SUSPENDERS_INVALID_SOCK;
        return false;
    }
    c->fd = h->fd;
    c->owns_fd = 1;
    c->is_server = 0;
    c->peer_set = 1;
    c->peer.sin_family = AF_INET;
    c->peer.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &c->peer.sin_addr) != 1) goto fail;
    if (RAND_bytes(c->local_cid, S_QUIC_CID_LEN) != 1) goto fail;
    if (RAND_bytes(c->peer_cid, S_QUIC_CID_LEN) != 1) goto fail;
    memcpy(c->orig_dcid, c->peer_cid, S_QUIC_CID_LEN);
    if (s_quic_install_initial(c) != 0) goto fail;
    if (s_quic_x25519(c) != 0) goto fail;
    if (s_quic_hash_init(c) != 0) goto fail;
    n = s_quic_build_client_hello(c, ch, sizeof(ch));
    if (n < 0) goto fail;
    if (s_quic_hash_update(c, ch, (size_t)n) != 0) goto fail;
    if (s_quic_send_crypto(c, S_QUIC_INITIAL, ch, (size_t)n, 0, S_QUIC_INITIAL_PAD) != 0)
        goto fail;
    if (s_quic_drive(c, 1) != 0) goto fail;
    h->priv = c;
    h->roles |= SUSPENDERS_HOSE_ROLE_DIALER | SUSPENDERS_HOSE_ROLE_READER | SUSPENDERS_HOSE_ROLE_WRITER;
    h->protocol = SUSPENDERS_HOSE_PROTO_QUIC;
    return true;
fail:
    if (c->owns_fd && c->fd != SUSPENDERS_INVALID_SOCK) suspenders_close_socket(c->fd);
    h->fd = SUSPENDERS_INVALID_SOCK;
    s_quic_conn_free(c);
    return false;
}

static bool quic_listen(suspenders_hose_t *h, const char *host, int port) {
    (void)host;
    if (s_quic_selftest_keys() != 0) return false;
    pthread_once(&s_quic_cert_once, s_quic_make_cert);
    if (!s_quic_cert_key) return false;
    if (s_quic_udp(h, host, port, 1) != 0) return false;
    h->roles |= SUSPENDERS_HOSE_ROLE_LISTENER;
    h->protocol = SUSPENDERS_HOSE_PROTO_QUIC;
    return true;
}

static bool quic_accept(suspenders_hose_t *listener, suspenders_hose_t *client) {
    s_quic_conn_t *c;
    uint8_t buf[1500];
    struct sockaddr_in from;
    socklen_t fl;
    ssize_t n;
    uint8_t done[8];
    int dn;
    if (!(listener->roles & SUSPENDERS_HOSE_ROLE_LISTENER)) return false;
    c = s_quic_conn_new();
    if (!c) return false;
    c->is_server = 1;
    c->owns_fd = 0;
    c->fd = listener->fd;
    if (s_quic_hash_init(c) != 0) { s_quic_conn_free(c); return false; }
    fl = sizeof(from);
    n = s_io_recvfrom(listener->fd, buf, sizeof(buf), (struct sockaddr *)&from, &fl);
    if (n < 0) { s_quic_conn_free(c); return false; }
    c->peer = from;
    c->peer_set = 1;
    /* First packet is the client Initial: DCID is what we adopt as local cid. */
    if (n < 6 + S_QUIC_CID_LEN || !(buf[0] & 0x80) || buf[5] != S_QUIC_CID_LEN) {
        s_quic_conn_free(c);
        return false;
    }
    memcpy(c->local_cid, buf + 6, S_QUIC_CID_LEN);
    memcpy(c->orig_dcid, c->local_cid, S_QUIC_CID_LEN);
    if (s_quic_install_initial(c) != 0) { s_quic_conn_free(c); return false; }
    if (s_quic_ingest(c, buf, (size_t)n) != 0 || s_quic_drive(c, 1) != 0) {
        s_quic_conn_free(c);
        return false;
    }
    /* HANDSHAKE_DONE so the client knows the handshake finished. */
    done[0] = 0x1e;
    dn = s_quic_encode_ack(c, S_QUIC_APP, done + 1, sizeof(done) - 1);
    if (dn < 0) dn = 0;
    (void)s_quic_send_frames(c, S_QUIC_APP, done, 1u + (size_t)dn, 0, 0);
    suspenders_hose_init(client, NULL);
    client->fd = listener->fd;
    client->protocol = SUSPENDERS_HOSE_PROTO_QUIC;
    client->transport = listener->transport;
    client->roles = SUSPENDERS_HOSE_ROLE_READER | SUSPENDERS_HOSE_ROLE_WRITER;
    client->priv = c;
    return true;
}

static ssize_t quic_read(suspenders_hose_t *h, void *dest, size_t len) {
    s_quic_conn_t *c = (s_quic_conn_t *)h->priv;
    uint64_t deadline;
    if (!c || !(h->roles & SUSPENDERS_HOSE_ROLE_READER)) return -1;
    deadline = suspenders_now_ns() + 5000000000ull;
    while (c->rx_pos >= c->rx_len) {
        uint8_t buf[1500];
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        ssize_t n;
        uint64_t saved;
        if (c->closed || c->rx_fin) return 0;
        if (suspenders_now_ns() > deadline) {
            suspenders_errno = SUSPENDERS_TIMEDOUT;
            return -1;
        }
        if (s_quic_retransmit(c) != 0) return -1;
        saved = s_io_deadline_ns;
        if (!saved) s_io_deadline_ns = suspenders_now_ns() + 50000000ull;
        n = s_io_recvfrom(c->fd, buf, sizeof(buf), (struct sockaddr *)&from, &fl);
        s_io_deadline_ns = saved;
        if (n < 0) {
            if (suspenders_errno == SUSPENDERS_TIMEDOUT) continue;
            return -1;
        }
        if (s_quic_ingest(c, buf, (size_t)n) != 0) return -1;
    }
    {
        size_t have = c->rx_len - c->rx_pos;
        if (have > len) have = len;
        memcpy(dest, c->rx + c->rx_pos, have);
        c->rx_pos += have;
        return (ssize_t)have;
    }
}

static ssize_t quic_write(suspenders_hose_t *h, const void *src, size_t len) {
    s_quic_conn_t *c = (s_quic_conn_t *)h->priv;
    const uint8_t *p = (const uint8_t *)src;
    size_t off = 0;
    uint64_t limit;
    if (!c || !c->app_ready || !(h->roles & SUSPENDERS_HOSE_ROLE_WRITER)) return -1;
    limit = c->is_server ? c->peer_bidi_local : c->peer_bidi_remote;
    while (off < len) {
        uint8_t fr[1200];
        size_t n = 0;
        size_t chunk = len - off;
        int a, k;
        if (chunk > 1000) chunk = 1000;
        if (c->tx_off + chunk > limit) return -1;
        a = s_quic_encode_ack(c, S_QUIC_APP, fr, sizeof(fr));
        if (a < 0) return -1;
        n = (size_t)a;
        fr[n++] = 0x0e; /* OFF | LEN */
        k = s_quic_put_varint(fr + n, sizeof(fr) - n, 0);
        if (k < 0) return -1;
        n += (size_t)k;
        k = s_quic_put_varint(fr + n, sizeof(fr) - n, c->tx_off);
        if (k < 0) return -1;
        n += (size_t)k;
        k = s_quic_put_varint(fr + n, sizeof(fr) - n, chunk);
        if (k < 0) return -1;
        n += (size_t)k;
        memcpy(fr + n, p + off, chunk);
        n += chunk;
        if (s_quic_send_frames(c, S_QUIC_APP, fr, n, 1, 0) != 0) return -1;
        c->tx_off += chunk;
        off += chunk;
    }
    return (ssize_t)len;
}

static ssize_t quic_readv(suspenders_hose_t *h, const struct iovec *iov, int iovcnt) {
    ssize_t total = 0;
    int i;
    for (i = 0; i < iovcnt; i++) {
        ssize_t n;
        if (!iov[i].iov_len) continue;
        n = quic_read(h, iov[i].iov_base, iov[i].iov_len);
        if (n < 0) return total > 0 ? total : -1;
        total += n;
        if ((size_t)n < iov[i].iov_len) break;
    }
    return total;
}

static ssize_t quic_writev(suspenders_hose_t *h, const struct iovec *iov, int iovcnt) {
    ssize_t total = 0;
    int i;
    for (i = 0; i < iovcnt; i++) {
        ssize_t n;
        if (!iov[i].iov_len) continue;
        n = quic_write(h, iov[i].iov_base, iov[i].iov_len);
        if (n < 0) return total > 0 ? total : -1;
        total += n;
    }
    return total;
}

static void quic_close(suspenders_hose_t *h) {
    s_quic_conn_t *c = (s_quic_conn_t *)h->priv;
    if (c) {
        if (!c->closed && c->app_ready) {
            uint8_t fr[4] = { 0x1c, 0x00, 0x00, 0x00 };
            (void)s_quic_send_frames(c, S_QUIC_APP, fr, 4, 0, 0);
        }
        if (c->owns_fd && c->fd != SUSPENDERS_INVALID_SOCK)
            suspenders_close_socket(c->fd);
        s_quic_conn_free(c);
        h->priv = NULL;
    } else if ((h->roles & SUSPENDERS_HOSE_ROLE_LISTENER) && h->fd != SUSPENDERS_INVALID_SOCK) {
        suspenders_close_socket(h->fd);
    }
    h->fd = SUSPENDERS_INVALID_SOCK;
    h->roles = 0;
}

static const suspenders_transport_ops_t quic_transport_ops = {
    .scheme = "quic://",
    .dial = quic_dial,
    .listen = quic_listen,
    .accept = quic_accept,
    .read = quic_read,
    .write = quic_write,
    .readv = quic_readv,
    .writev = quic_writev,
    .recvfrom = NULL,
    .sendto = NULL,
    .close = quic_close,
};

#endif /* OPENSSL && !Windows */
#endif /* SUSPENDERS_QUIC_H */
