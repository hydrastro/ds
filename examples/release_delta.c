/* Reuse an indexed release as the base of streamed and owned update scripts.
 */
#include <dsa/structures/suffix_array.h>
#include <dsaa.h>
#include <stdio.h>

static ds_status_t show_command(const ds_hsadelta_command_t *command,
                                const unsigned char *literal, void *user) {
  (void)literal;
  (void)user;
  printf("  %s %lu bytes from offset %lu\n",
         command->kind == DS_HSA_COPY ? "COPY" : "INSERT",
         (unsigned long)command->length, (unsigned long)command->offset);
  return DS_OK;
}
/* This deliberately weak hash illustrates byte-verified collision handling.
 * Choose a well-distributed hash for real workloads. */
static unsigned long small_hash(const unsigned char *bytes, size_t length,
                                void *user) {
  (void)user;
  return ds_hash_xxh32(bytes, length, 0) & 3UL;
}
static ds_status_t suffix_policy(const size_t *symbols, size_t count,
                                 size_t alphabet, size_t *out, void *user,
                                 ds_context_t *context) {
  (void)user;
  printf("Selected suffix builder: %lu blocks, %lu distinct block symbols\n",
         (unsigned long)count, (unsigned long)alphabet);
  return ds_sa_build_int(symbols, count, alphabet, out, context);
}
static ds_status_t release(const ds_hsadelta_config_t *config,
                           const char *reference, size_t reference_size,
                           const char *target, size_t target_size) {
  ds_hsadelta_index_t index;
  ds_hsadelta_script_t script;
  ds_hsadelta_progress_t progress;
  void *rebuilt;
  size_t rebuilt_size;
  ds_status_t st =
      ds_hsadelta_init(&index, reference, reference_size, config, NULL);
  if (st != DS_OK) {
    return st;
  }
  st = ds_hsadelta_emit(&index, target, target_size, show_command, NULL,
                        &progress);
  if (st != DS_OK) {
    goto index_done;
  }
  printf("Streamed %lu commands for %lu target bytes\n",
         (unsigned long)progress.commands,
         (unsigned long)progress.target_bytes);
  st = ds_hsadelta_build(&index, target, target_size, &script);
  if (st != DS_OK) {
    goto index_done;
  }
  st = ds_hsadelta_apply(&script, reference, reference_size, 1024, &rebuilt,
                         &rebuilt_size, NULL);
  if (st == DS_OK) {
    printf("Reconstructed release (%lu literal bytes):\n",
           (unsigned long)script.literals.size);
    if (rebuilt_size > 0) {
      (void)fwrite(rebuilt, 1, rebuilt_size, stdout);
    }
    ds_context_free(NULL, rebuilt);
  }
  ds_hsadelta_script_destroy(&script);
index_done:
  ds_hsadelta_destroy(&index);
  return st;
}
int main(void) {
  static const char old_release[] =
      "version=one\nalpha=stable\nbeta=stable\ngamma=old\n";
  static const char new_release[] =
      "version=two\nbeta=stable\nalpha=stable\ngamma=new\nadded=yes\n";
  ds_hsadelta_config_t config;
  ds_status_t st;
  ds_hsadelta_config_init(&config);
  config.block_size = 4;
  puts("Default rolling hash and open-addressed lookup:");
  st = release(&config, old_release, sizeof(old_release) - 1, new_release,
               sizeof(new_release) - 1);
  if (st != DS_OK) {
    goto fail;
  }
  config.hash = small_hash;
  config.suffix = suffix_policy;
  config.map_ops = ds_map_swiss_ops();
  puts("Two-bit hash, selected suffix builder and Swiss lookup:");
  st = release(&config, old_release, sizeof(old_release) - 1, new_release,
               sizeof(new_release) - 1);
  if (st != DS_OK) {
    goto fail;
  }
  return 0;
fail:
  fprintf(stderr, "Delta failed: %s\n", ds_status_name(st));
  return 1;
}
