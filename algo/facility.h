#ifndef DS_ALGO_FACILITY_H
#define DS_ALGO_FACILITY_H

/*
 * Covering, packing, location and clustering problems; exact searches for
 * small inputs (with node budgets) next to the classic approximations.
 *
 * Set systems are given in CSR form: set i holds elems[off[i] .. off[i+1])
 * (elements in [0, universe)), with optional weights (NULL: all 1).
 *
 *   ds_set_cover        greedy (cost per newly covered element, an H(n)
 *                       approximation) or exact branch and bound
 *   ds_set_packing      maximum-weight family of pairwise disjoint sets:
 *                       greedy (weight per element) or exact branch and
 *                       bound
 *   ds_facility_location  uncapacitated facility location (opening costs
 *                       f, service costs c[facility][client]): greedy,
 *                       local search (open, close and swap moves), or
 *                       exact over all subsets (up to 20 facilities)
 *   ds_capacitated_facility_location  facilities with capacities, clients
 *                       with demands, splittable service; exact over all
 *                       subsets (up to 16 facilities) with each subset's
 *                       transportation problem solved by min-cost flow
 *   ds_p_center         discrete p-center (choose p of the n vertices to
 *                       minimise the largest distance to a centre):
 *                       Gonzalez's farthest-first 2-approximation, or exact
 *                       (threshold search with an exact dominating-set
 *                       test)
 *   ds_k_clustering     k-clustering: k-center by farthest-first, or
 *                       single linkage (maximum spacing, Kruskal stopped at
 *                       k components)
 */

#include "../lib/context.h"
#include "../lib/status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ds_cover_method {
  DS_COVER_GREEDY = 0,
  DS_COVER_EXACT
} ds_cover_method_t;

/* chosen: count flags; *out_cost the total weight. DS_NOT_FOUND when
 * some element is in no set; DS_STOP when the budget ran out (the best
 * cover found so far is returned). */
ds_status_t ds_set_cover(size_t universe, size_t count, const size_t *off,
                         const size_t *elems, const double *weight,
                         ds_cover_method_t method, size_t budget,
                         unsigned char *chosen, double *out_cost,
                         ds_context_t *context);
ds_status_t ds_set_packing(size_t universe, size_t count, const size_t *off,
                           const size_t *elems, const double *weight,
                           ds_cover_method_t method, size_t budget,
                           unsigned char *chosen, double *out_weight,
                           ds_context_t *context);

typedef enum ds_facility_method {
  DS_FACILITY_GREEDY = 0,
  DS_FACILITY_LOCAL_SEARCH,
  DS_FACILITY_EXACT
} ds_facility_method_t;

/* f: m opening costs; c: m x n service costs. open: m flags; assign: n
 * facility indices. */
ds_status_t ds_facility_location(const double *f, const double *c, size_t m,
                                 size_t n, ds_facility_method_t method,
                                 unsigned char *open, size_t *assign,
                                 double *out_cost, ds_context_t *context);
/* capacity: m; demand: n; c: m x n cost per unit. flow (m x n, may be
 * NULL) receives the amounts served. DS_NOT_FOUND when the total capacity
 * cannot meet the demand. */
ds_status_t ds_capacitated_facility_location(
    const double *f, const double *capacity, const double *demand,
    const double *c, size_t m, size_t n, unsigned char *open, double *flow,
    double *out_cost, ds_context_t *context);

typedef enum ds_center_method {
  DS_CENTER_GONZALEZ = 0,
  DS_CENTER_EXACT
} ds_center_method_t;

/* dist: n x n. centers: p vertices; *out_radius the objective. */
ds_status_t ds_p_center(const double *dist, size_t n, size_t p,
                        ds_center_method_t method, size_t budget,
                        size_t *centers, double *out_radius,
                        ds_context_t *context);

typedef enum ds_cluster_method {
  DS_CLUSTER_K_CENTER = 0,
  DS_CLUSTER_SINGLE_LINKAGE
} ds_cluster_method_t;

/* label: n cluster ids in [0, k); *out_value the radius (k-center) or
 * the spacing, the smallest distance between clusters (single linkage). */
ds_status_t ds_k_clustering(const double *dist, size_t n, size_t k,
                            ds_cluster_method_t method, size_t *label,
                            double *out_value, ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_ALGO_FACILITY_H */
