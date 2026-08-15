# Vector performance coverage

Cases `00`-`20` cover arithmetic streams, reductions, matrix/stencil kernels,
register pressure, lane reconstruction, shuffles, selection, dynamic extracts,
and values live across calls.

The added cases avoid repeating those primary targets:

| Case | New primary coverage |
|---|---|
| `21_int4_divmod_recurrence` | Vector-by-vector integer divide/remainder |
| `22_float4_division_chain` | Dependent vector floating-point division |
| `23_dynamic_lane_update` | Runtime lane insertion in a hot loop |
| `24_many_vector_args_throughput` | Sustained calls with ten vector arguments |
| `25_vector_2d_transpose_params` | Multidimensional vector array parameters |
| `26_vectype_spelling_chain` | Candidate `VecType` spelling equivalence |
| `27_int4_iir_recurrence` | Genuine loop-carried vector recurrence |
| `28_float4_horner_chain` | Long vector Horner dependency chains |
| `29_vector_runtime_alias` | Exact-alias and disjoint vector array calls |
| `30_float4_lane_normalize` | Horizontal lane reduction and rebroadcast |
| `31_vector_ring_buffer` | Non-power-of-two circular vector storage |
| `32_vector_indirect_gather` | Changing whole-vector indirect loads |

Unpublished language behavior is represented under `speculative/` and is not
picked up by the performance runner.
