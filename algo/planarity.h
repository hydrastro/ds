#ifndef DS_ALGO_PLANARITY_H
#define DS_ALGO_PLANARITY_H

/*
 * Planarity: testing, embeddings, faces and Kuratowski certificates.
 *
 * ds_planarity_test() decides whether an undirected graph can be drawn in
 * the plane without crossings. It is the left-right planarity test of de
 * Fraysseix and Rosenstiehl in Brandes' formulation: a depth-first pass
 * orients the graph and computes lowpoints and nesting depths; a second
 * pass partitions the back edges into a left and a right class under
 * conflict constraints (a stack of conflict pairs of intervals); a third
 * pass turns the partition into a combinatorial embedding. The three passes
 * are iterative and the adjacency orders come from bucket sorts, so a test
 * costs O(n + m) time and space.
 *
 * The graph is read through a ds_gview_t (see graph_view.h). Each edge may
 * be listed in one direction or in both; self-loops and parallel edges are
 * dropped, since they never change planarity, so the embedding describes
 * the underlying simple graph. A graph with n >= 3 vertices and more than
 * 3n - 6 distinct edges is answered from the count alone.
 *
 * An embedding is a rotation system: around each vertex, its neighbours in
 * the cyclic order of some crossing-free drawing (clockwise in it, and
 * counterclockwise in its mirror image). Half-edges are stored in CSR form:
 * the half-edges of v are offsets[v] .. offsets[v + 1] - 1 in rotation
 * order, half-edge h leads to target[h], and twin[h] is the same edge seen
 * from the other end. The faces are the orbits of ds_planar_next(), which
 * steps from h to the half-edge just before twin[h] in the rotation of
 * target[h]. ds_planar_embedding_check() verifies a rotation system on its
 * own: twins must pair up and every component must satisfy Euler's formula
 * V - E + F = 2.
 *
 * Certificates for the other answer: ds_planarity_kuratowski() returns a
 * subdivision of K5 or K3,3 inside the graph, which every non-planar graph
 * contains (Kuratowski's theorem). The graph is shrunk while it stays
 * non-planar. It is kept reduced: vertices of degree one go, vertices of
 * degree two are suppressed into paths, and loops and the longer of
 * parallel paths are dropped, none of which changes planarity. Random
 * subsets of the reduced edges are deleted when the rest stays non-planar,
 * the deletion probability halving after a failure and doubling after a
 * success; then single edges are deleted until none can be. A reduced
 * minimal non-planar graph is K5 or K3,3 itself, and its edges expand into
 * the paths of the subdivision. Each attempt is one planarity test on the
 * current reduced graph. Non-planar graphs usually hold many Kuratowski
 * subgraphs, so deletions mostly succeed and the graph shrinks
 * geometrically: a few full tests in all (0.3 s for a perturbed 100,000
 * vertex triangulation). The worst case is O(m^2 log m), when the reduced
 * graph keeps many edges that resist deletion. The random generator has a
 * fixed seed, so results are reproducible. ds_kuratowski_classify() checks
 * a claimed certificate independently by tracing the paths between its
 * branch vertices.
 *
 * ds_planar_draw() turns an embedding into a straight-line drawing on the
 * integer grid [0, 2n - 4] x [0, n - 2] that realises its rotations, in
 * O(n) (canonical ordering and the shift method).
 *
 * Built on the test: ds_outerplanarity_test() (the graph plus one vertex
 * joined to every vertex must be planar; the embedding then has every
 * vertex on one face, recorded in `outer`), and
 * ds_planarity_maximal_subgraph(), a planarization by edge deletion that
 * keeps edges greedily in the given order. Each run of accepted edges is
 * found by galloping and bisection over the remaining ones, so a run of d
 * edges costs O(log d) planarity tests (tests on more than 3n - 6 edges are
 * free): O((r + 1) log m) tests for r rejected edges. A maximum planar
 * subgraph is NP-hard to find; this one is maximal: adding back any
 * rejected edge breaks planarity.
 *
 * All functions are iterative; working memory comes from the ds_context_t
 * (NULL for the default).
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include "graph_view.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ds_planar_embedding {
  size_t vertices;
  size_t edges;    /* distinct undirected edges */
  size_t *offsets; /* vertices + 1 entries */
  size_t *target;  /* 2 * edges half-edges, clockwise around each vertex */
  size_t *twin;    /* 2 * edges: the reverse half-edge */
  /* NULL, or for an outerplanar embedding: per vertex, the half-edge
   * leaving it that starts its corner of the outer face (DS_GVIEW_NONE for
   * an isolated vertex); every vertex of a component lies on that face */
  size_t *outer;
  ds_context_t *context;
} ds_planar_embedding_t;

typedef enum ds_kuratowski_kind {
  DS_KURATOWSKI_NONE = 0, /* not a subdivision of K5 or K3,3 */
  DS_KURATOWSKI_K5 = 1,
  DS_KURATOWSKI_K33 = 2
} ds_kuratowski_kind_t;

/* *out_planar tells whether the graph is planar; when it is and
 * out_embedding is not NULL, the embedding is built there (release it with
 * ds_planar_embedding_destroy()). When the graph is not planar,
 * *out_embedding is left empty, which is safe to destroy. DS_ERR_INVALID
 * for an edge target outside the graph. */
ds_status_t ds_planarity_test(const ds_gview_t *g, bool *out_planar,
                              ds_planar_embedding_t *out_embedding,
                              ds_context_t *context);
/* outerplanarity: a drawing with every vertex on the outer face; on
 * success the embedding has `outer` set */
ds_status_t ds_outerplanarity_test(const ds_gview_t *g, bool *out_outerplanar,
                                   ds_planar_embedding_t *out_embedding,
                                   ds_context_t *context);
void ds_planar_embedding_destroy(ds_planar_embedding_t *e);

/* the embedding as a graph view over its own arrays (no copy): every edge
 * in both directions, neighbours in rotation order; the view reads
 * *out_csr, which must outlive it, as must e */
void ds_planar_embedding_view(const ds_planar_embedding_t *e,
                              ds_gview_csr_t *out_csr, ds_gview_t *out_view);

/* the half-edge that follows h on its face */
size_t ds_planar_next(const ds_planar_embedding_t *e, size_t h);
/* the vertex a half-edge leaves (binary search over offsets) */
size_t ds_planar_source(const ds_planar_embedding_t *e, size_t h);
/* face number of every half-edge (2 * edges entries) and the number of
 * faces; a component with E edges and F faces has V - E + F = 2 */
void ds_planar_faces(const ds_planar_embedding_t *e, size_t *out_face,
                     size_t *out_count);
/* *out_valid: the arrays form a rotation system of a simple graph (twins
 * pair up, no repeated neighbour) and every component has genus 0 */
ds_status_t ds_planar_embedding_check(const ds_planar_embedding_t *e,
                                      bool *out_valid);

/* a subdivision of K5 or K3,3 contained in the graph, as an edge list
 * (out_u[i], out_v[i]) of *out_count edges; DS_NOT_FOUND when the graph is
 * planar, DS_ERR_RANGE when capacity is smaller than the certificate (the
 * required size is still stored in *out_count). out_kind may be NULL. */
ds_status_t ds_planarity_kuratowski(const ds_gview_t *g, size_t *out_u,
                                    size_t *out_v, size_t capacity,
                                    size_t *out_count,
                                    ds_kuratowski_kind_t *out_kind,
                                    ds_context_t *context);
/* whether the edges form exactly a subdivision of K5 or K3,3 (isolated
 * vertices aside); edges must be distinct and not loops */
ds_status_t ds_kuratowski_classify(size_t vertices, const size_t *u,
                                   const size_t *v, size_t count,
                                   ds_kuratowski_kind_t *out_kind,
                                   ds_context_t *context);

/* a straight-line drawing of the embedding on the integer grid
 * [0, 2n - 4] x [0, n - 2] (n >= 3): no two edges cross and no vertex lies
 * on an edge it does not end. The embedding is completed to a connected,
 * internally triangulated one (the largest face stays outer), and the
 * vertices are placed in a canonical order by the shift method of de
 * Fraysseix, Pach and Pollack with Chrobak and Payne's relative offsets:
 * O(n) time. Around each vertex the neighbours appear in rotation order.
 * DS_ERR_INVALID if ds_planar_embedding_check() rejects the embedding. */
ds_status_t ds_planar_draw(const ds_planar_embedding_t *e, size_t *out_x,
                           size_t *out_y);

/* a maximal planar subgraph of an undirected edge list: keep[i] is set for
 * the edges kept, scanning them in order; *out_kept counts them (may be
 * NULL). Loops and repeated edges are never kept. */
ds_status_t ds_planarity_maximal_subgraph(size_t vertices, const size_t *u,
                                          const size_t *v, size_t count,
                                          bool *keep, size_t *out_kept,
                                          ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_PLANARITY_H */
