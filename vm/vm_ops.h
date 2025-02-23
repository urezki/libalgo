#ifndef __OPS_H__
#define __OPS_H__

void fixup_metadata(struct bpn *);
ulong bpn_max_avail(struct bpn *);

bool try_merge_va(struct bpt_root *, struct bpn *,
	struct vmap_area *, int);

void try_merge_with_adjacent_leaf(struct bpt_root *,
	struct bpn *, int, struct vmap_area **);

int vm_init_free_space(struct bpt_root *, ulong, ulong);

int free_vmap_area(struct bpt_root *, struct vmap_area *);
struct vmap_area *alloc_vmap_area(struct bpt_root *,
	ulong, ulong, ulong, ulong);
struct vmap_area *lookup_smallest_va(struct bpt_root *,
	ulong, ulong, ulong, struct bpn **);
struct bpn *
bpt_lookup_lowest_leaf(struct bpt_root *, ulong, ulong);

#endif
