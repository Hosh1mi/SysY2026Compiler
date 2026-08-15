# Speculative vector performance cases

These sources depend on SysY2026 rules that have not been published. They are
kept below the suite root so `arm_v_performance.sh` does not treat expected
front-end rejection as a performance failure.

- `inferred_extent_pipeline.sy`: omitted-width `VecType` inference.
- `vector_compare_mask.sy`: lane-wise relational results and mask type.
