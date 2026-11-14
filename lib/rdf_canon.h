#ifndef DS_RDF_CANON_H
#define DS_RDF_CANON_H

/*
 * RDF datasets, N-Quads, and RDF Dataset Canonicalization (RDFC-1.0, the
 * W3C Recommendation of May 2024, formerly URDNA2015).
 *
 * A dataset is a list of quads (subject, predicate, object, graph) of
 * terms: IRIs, blank nodes, literals and the default graph. Terms hold
 * their values unescaped, in UTF-8; a literal's lexical form is counted
 * in bytes, so it may contain U+0000. Adding a quad checks it and copies
 * its strings into the dataset: IRIs must be absolute (a scheme and a
 * colon) and free of the characters N-Quads cannot write in an IRI
 * (controls, space, <>"{}|^`\), blank node labels must follow the
 * N-Quads production, language tags [a-zA-Z]+(-[a-zA-Z0-9]+)*, and all
 * text must be well-formed UTF-8. A literal typed xsd:string is stored as
 * a simple literal and one with a language tag carries no datatype (it is
 * an rdf:langString), so equal literals look equal.
 *
 * ds_rdf_parse_nquads reads an N-Quads document (RDF 1.1 grammar:
 * comments, any EOL, UCHAR and ECHAR escapes; a UCHAR surrogate pair
 * makes one character, a lone surrogate is an error) and adds its quads,
 * or, on a syntax error, adds none and reports the line.
 * ds_rdf_dataset_nquads writes quads in canonical N-Quads form: one per
 * line, single spaces, IRIs and text unescaped except within literals,
 * where only \b \t \n \f \r \" \\ use ECHAR and other controls, U+007F,
 * U+FFFE and U+FFFF use \u with uppercase hex.
 *
 * ds_rdfc_canonicalize relabels the blank nodes of a dataset canonically
 * and returns its serialized canonical form (the distinct quads in
 * canonical N-Quads form, sorted by code point, blank nodes named
 * _:c14n0, _:c14n1, ...) and the issued identifiers map from input
 * labels to canonical labels. Isomorphic datasets get the same form. The
 * algorithm is the Recommendation's: first-degree hashes of every blank
 * node's quads, canonical labels for unique hashes in hash order, and
 * Hash N-Degree Quads, which explores permutations of related blank nodes
 * along gossip paths, for blank nodes that share a hash. The hash is
 * SHA-256 unless the options name another lib/sha2 digest (the test suite
 * also uses SHA-384); any ds_digest_ops_t works.
 *
 * Implementation notes:
 * - Hash N-Degree Quads runs on an explicit stack, so recursion depth
 *   does not grow with the dataset; gossip paths can be as long as the
 *   number of blank nodes.
 * - Temporary issuers are one shared stack of issued blank nodes with a
 *   position per node: copying an issuer, which the algorithm does for
 *   every permutation, costs only the identifiers issued since.
 * - Of the results for blank nodes sharing a first-degree hash, only one
 *   per connected group of blank nodes is kept (the smallest hash, the
 *   only one whose issuer is used), so memory stays linear.
 * - Permutations run in lexicographic order of the input labels. When
 *   blank nodes are automorphic, the issued identifiers map depends on
 *   this order; the canonical form does not.
 *
 * A limit of RDFC-1.0 itself: when two permutations give the same gossip
 * path through issuers that no automorphism relates, the first one wins,
 * and isomorphic datasets with other labels can get different canonical
 * forms. Hash Related Blank Node sees a related node's position and
 * predicate but not which other blank nodes share its quad, so for
 * example three blank nodes in a directed cycle, each naming the third
 * as graph ({_:a <p> _:b _:c}, {_:b <p> _:c _:a}, {_:c <p> _:a _:b}),
 * canonicalize in two ways depending on the labels. Other
 * implementations show the same behaviour, with their own permutation
 * orders; this one follows the Recommendation step by step and agrees
 * with a literal transcription of it on every labelling.
 *
 * Poison datasets: Hash N-Degree Quads can take exponential time on
 * datasets with large symmetric blank-node structures, so, as the
 * Recommendation advises, canonicalization limits it and returns
 * DS_ERR_RANGE past a limit. Each blank node may go through Hash N-Degree
 * Quads at most max_node_calls times. Ordinary data needs at most N per
 * node, for N blank nodes that share their first-degree hash with
 * another: N disjoint copies of a structure, a list of N equal values, a
 * symmetric cycle of N blank nodes. The default is max(N, 64), which
 * bounds a run to O(N^2) calls; the 64 leaves room for small knotted
 * structures, such as the W3C test suite's poison graphs "evil (1)" to
 * "evil (3)" (43 runs per node) or a complete bipartite K(3,3) (25), and
 * still rejects cliques of five or more mutually linked blank nodes (97
 * runs per node for five, about 9! for the suite's 10-node clique).
 * max_calls bounds the runs in all, for a hard limit on time.
 *
 * Memory comes from the ds_context_t given at init (the dataset's) or to
 * the call.
 */

#include "context.h"
#include "sha2.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ds_rdf_kind {
  DS_RDF_IRI = 0,
  DS_RDF_BLANK = 1,
  DS_RDF_LITERAL = 2,
  DS_RDF_DEFAULT_GRAPH = 3
} ds_rdf_kind_t;

typedef struct ds_rdf_term {
  ds_rdf_kind_t kind;
  /* IRI, blank node label without "_:", or lexical form; NUL-terminated
   * in a dataset (a lexical form may also hold NULs: use length) */
  const char *value;
  size_t length;        /* bytes of value */
  const char *datatype; /* literal: datatype IRI, NULL for xsd:string and
                           language-tagged strings */
  const char *language; /* literal: language tag, else NULL */
} ds_rdf_term_t;

typedef struct ds_rdf_quad {
  ds_rdf_term_t subject;   /* IRI or blank node */
  ds_rdf_term_t predicate; /* IRI */
  ds_rdf_term_t object;    /* IRI, blank node or literal */
  ds_rdf_term_t graph;     /* IRI, blank node or the default graph */
} ds_rdf_quad_t;

/* storage for a dataset's strings */
typedef struct ds_rdf_chunk {
  struct ds_rdf_chunk *next;
  size_t used;
  size_t size;
} ds_rdf_chunk_t;

typedef struct ds_rdf_dataset {
  ds_rdf_quad_t *quads; /* in the order added, duplicates included */
  size_t count;
  size_t capacity;
  ds_rdf_chunk_t *chunks;
  ds_context_t *context;
} ds_rdf_dataset_t;

void ds_rdf_dataset_init(ds_rdf_dataset_t *dataset, ds_context_t *context);
void ds_rdf_dataset_destroy(ds_rdf_dataset_t *dataset);
void ds_rdf_dataset_clear(ds_rdf_dataset_t *dataset);
/* copies the quad in; DS_ERR_INVALID if it is not well formed (the
 * dataset is unchanged) */
ds_status_t ds_rdf_dataset_add(ds_rdf_dataset_t *dataset,
                               const ds_rdf_quad_t *quad);
/* term helpers for building quads (value NUL-terminated) */
void ds_rdf_iri(ds_rdf_term_t *term, const char *iri);
void ds_rdf_blank(ds_rdf_term_t *term, const char *label);
void ds_rdf_literal(ds_rdf_term_t *term, const char *lexical,
                    const char *datatype, const char *language);
void ds_rdf_default_graph(ds_rdf_term_t *term);
/* true when the terms are the same RDF term */
bool ds_rdf_term_equal(const ds_rdf_term_t *a, const ds_rdf_term_t *b);

/* adds the quads of an N-Quads document; on DS_ERR_INVALID none are added
 * and *error_line (if not NULL) is the 1-based line at fault */
ds_status_t ds_rdf_parse_nquads(ds_rdf_dataset_t *dataset, const char *text,
                                size_t length, size_t *error_line);
/* the quads in canonical N-Quads form, in order, blank nodes keeping their
 * labels; *out (NUL-terminated) comes from the dataset's context */
ds_status_t ds_rdf_dataset_nquads(const ds_rdf_dataset_t *dataset, char **out,
                                  size_t *out_length);

typedef struct ds_rdfc_options {
  const ds_digest_ops_t *digest; /* NULL: SHA-256 */
  /* Hash N-Degree Quads runs per blank node: 0 means max(N, 64) for the N
   * blank nodes that share their first-degree hash, (size_t)-1 no limit */
  size_t max_node_calls;
  size_t max_calls; /* runs in all; 0 means no limit */
} ds_rdfc_options_t;

typedef struct ds_rdfc_label {
  const char *input;     /* input label, without "_:" */
  const char *canonical; /* "c14n0", "c14n1", ... */
} ds_rdfc_label_t;

typedef struct ds_rdfc_result {
  char *nquads;  /* serialized canonical form, NUL-terminated */
  size_t length; /* bytes */
  size_t quads;  /* distinct quads */
  /* the issued identifiers map, in issue order: labels[k].canonical is
   * "c14n<k>" */
  ds_rdfc_label_t *labels;
  size_t label_count;
  size_t *by_input;      /* label indexes in order of input label */
  size_t calls;          /* Hash N-Degree Quads runs */
  size_t max_node_calls; /* most runs on one blank node */
  char *strings;         /* storage for the labels */
  ds_context_t *context;
} ds_rdfc_result_t;

void ds_rdfc_options_init(ds_rdfc_options_t *options);
/* options NULL: the defaults; on failure the result is empty */
ds_status_t ds_rdfc_canonicalize(const ds_rdf_dataset_t *dataset,
                                 const ds_rdfc_options_t *options,
                                 ds_rdfc_result_t *result);
/* parse, then canonicalize; DS_ERR_INVALID on a syntax error (*error_line
 * as for ds_rdf_parse_nquads) */
ds_status_t ds_rdfc_canonicalize_nquads(const char *text, size_t length,
                                        const ds_rdfc_options_t *options,
                                        ds_rdfc_result_t *result,
                                        size_t *error_line,
                                        ds_context_t *context);
/* the canonical label of an input label (binary search), NULL if it has
 * none */
const char *ds_rdfc_label_of(const ds_rdfc_result_t *result, const char *input);
void ds_rdfc_result_destroy(ds_rdfc_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* DS_RDF_CANON_H */
