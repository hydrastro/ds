/* Small classical amplitude simulations, with explicit storage/shot limits. */
#include <dsaa.h>
#include <stdio.h>

static ds_status_t parity(size_t x, size_t *out, void *user) {
  size_t bit = 0;
  (void)user;
  while (x != 0) {
    bit ^= 1;
    x &= x - 1;
  }
  *out = bit;
  return DS_OK;
}
static ds_status_t marked(size_t x, size_t *out, void *user) {
  *out = x == *(const size_t *)user;
  return DS_OK;
}
static ds_status_t paired(size_t x, size_t *out, void *user) {
  size_t other = x ^ *(const size_t *)user;
  *out = x < other ? x : other;
  return DS_OK;
}
static ds_status_t identity(size_t x, size_t *out, void *user) {
  (void)user;
  *out = x;
  return DS_OK;
}
int main(void) {
  ds_qref_config_t config;
  ds_rng_t rng;
  ds_status_t st;
  bool constant, hit;
  size_t target = 5, secret = 6, found;
  double probability;
  unsigned long order, factor;
  ds_qref_config_init(&config);
  ds_rng_seed(&rng, 23);
  config.rng = &rng;
  config.max_states = 4096;
  config.max_shots = 64;
  st =
      ds_qref_deutsch_jozsa(4, parity, NULL, &config, &constant, &probability);
  if (st != DS_OK) {
    goto done;
  }
  printf("Deutsch-Jozsa: %s; zero-state probability %.6f\n",
         constant ? "constant" : "balanced", probability);
  st = ds_qref_grover(3, marked, &target, DS_QREF_AUTO, &config, &found, &hit,
                      &probability);
  if (st != DS_OK) {
    goto done;
  }
  printf("Grover: measured %lu (%s); success probability %.6f\n",
         (unsigned long)found, hit ? "marked" : "unmarked", probability);
  st = ds_qref_simon(4, paired, &secret, &config, &found);
  if (st != DS_OK) {
    goto done;
  }
  printf("Simon: recovered XOR mask %lu\n", (unsigned long)found);
  st = ds_qref_simon(4, identity, NULL, &config, &found);
  if (st != DS_OK) {
    goto done;
  }
  printf("Simon, injective oracle: mask %lu\n", (unsigned long)found);
  st = ds_qref_shor_order(5, 33, &config, &order);
  if (st != DS_OK) {
    goto done;
  }
  printf("Shor Fourier samples: order of 5 modulo 33 is %lu\n", order);
  st = ds_qref_shor_factor(33, &config, &factor);
  if (st == DS_OK) {
    printf("Factorization: 33 = %lu * %lu\n", factor, 33 / factor);
  }
done:
  if (st != DS_OK) {
    fprintf(stderr, "interference: %s\n", ds_status_name(st));
  }
  return st == DS_OK ? 0 : 1;
}
