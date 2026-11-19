/*
 * Two parties agree on a key and sign a message, with nothing but ds.
 *
 * Alice and Bob use the secrets of RFC 7748's Diffie-Hellman example:
 * each sends the other an X25519 public key, both compute the same shared
 * secret (the RFC's), and both hash it into a session key that
 * authenticates their messages with HMAC-SHA256. Alice also signs a
 * statement with Ed25519, which anyone holding her public key can check:
 * the signature verifies, and fails once one byte of the statement
 * changes. Key material here is fixed for reproducibility; real keys come
 * from a cryptographic random source.
 */
#include <dsa.h>
#include <stdio.h>
#include <string.h>

static void from_hex(const char *h, unsigned char *out, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    unsigned v;
    sscanf(h + 2 * i, "%2x", &v);
    out[i] = (unsigned char)v;
  }
}

static void show(const char *what, const unsigned char *p, size_t n) {
  size_t i;
  printf("%-22s", what);
  for (i = 0; i < n; i++) {
    printf("%02x", p[i]);
  }
  printf("\n");
}

int main(void) {
  unsigned char alice[32], bob[32], alice_pub[32], bob_pub[32];
  unsigned char s1[32], s2[32], expect[32], session[DS_SHA256_SIZE];
  unsigned char mac[DS_SHA256_SIZE], seed[32], sign_pub[32], sig[64];
  static const char statement[] = "Alice owes Bob 10 apples.";
  char tampered[sizeof(statement)];
  int ok = 1;
  from_hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a",
           alice, 32);
  from_hex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb",
           bob, 32);
  from_hex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742",
           expect, 32);
  /* the public keys travel; the secrets stay */
  if (ds_x25519_public_key(alice, alice_pub) != DS_OK ||
      ds_x25519_public_key(bob, bob_pub) != DS_OK) {
    return 1;
  }
  show("Alice's public key", alice_pub, 32);
  show("Bob's public key", bob_pub, 32);
  /* each side combines its secret with the other's public key */
  if (ds_x25519(alice, bob_pub, s1) != DS_OK ||
      ds_x25519(bob, alice_pub, s2) != DS_OK) {
    return 1;
  }
  show("shared secret", s1, 32);
  printf("both sides agree: %s; it is RFC 7748's: %s\n",
         memcmp(s1, s2, 32) == 0 ? "yes" : "NO",
         memcmp(s1, expect, 32) == 0 ? "yes" : "NO");
  ok &= memcmp(s1, s2, 32) == 0 && memcmp(s1, expect, 32) == 0;
  /* a session key from the secret, authenticating a message */
  ds_sha256(s1, 32, session);
  ds_hmac_sha256(session, sizeof(session), statement, strlen(statement), mac);
  show("message tag", mac, sizeof(mac));
  /* Alice's signing key (a separate key pair) and her signature */
  ds_sha256("Alice's signing seed", 20, seed);
  if (ds_ed25519_public_key(seed, sign_pub) != DS_OK ||
      ds_ed25519_sign(seed, sign_pub, statement, strlen(statement), sig) !=
          DS_OK) {
    return 1;
  }
  show("signature", sig, 64);
  printf("the signature verifies: %s\n",
         ds_ed25519_verify(sign_pub, statement, strlen(statement), sig) ==
                 DS_OK
             ? "yes"
             : "NO");
  ok &=
      ds_ed25519_verify(sign_pub, statement, strlen(statement), sig) == DS_OK;
  memcpy(tampered, statement, sizeof(statement));
  tampered[15] = '9'; /* "owes Bob 90 apples" */
  printf("after \"%s\": %s\n", tampered,
         ds_ed25519_verify(sign_pub, tampered, strlen(tampered), sig) == DS_OK
             ? "VERIFIES"
             : "refused");
  ok &= ds_ed25519_verify(sign_pub, tampered, strlen(tampered), sig) != DS_OK;
  return ok ? 0 : 1;
}
