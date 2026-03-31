
# Proof of Concept

This is a proof of concept to `pgr_rcShortestPaths`, a proposed SQL function to pgRouting.

The proof of concept intentionally skips the postgreSQL part and focuses on how the algorithm works.

#### What the PoC showed:
- Edge Indices:
   Boost's `r_c_shortest_paths` needs edge indices to be sequential 0-based integers. PostgreSQL edge IDs are arbitrary (10045, 99821, etc). If we pass those IDs as Boost's edge indices, Boost treats them as array indices and either crashes or gives wrong results (which is honestly worse than crashing).
   During graph construction, each edge gets a `bglIdx` counter starting from 0, separate from the PostgreSQL ID. A custom `EdgeIndexMap` struct returns `bgl_idx` when Boost asks for the index. The original PostgreSQL is preserved for output rows and it's never touched by BGL.
- Vertex ID Mapping:
  Since PostgreSQL uses arbitrary vertex IDs and Boost expects them to be 0-based sequential, we keep two maps:
	  - `pgToBgl`: used when building the graph and looking up source/target.
	  - `bglToPg`: used when reconstructing paths for output.
- Path reconstruction:
  Boost returns paths as `vector<edge_descriptor>` in reverse order (target to source). In the PoC we iterate backwards and walk each edge descriptor to extract source vertex and edge original IDs (PostgreSQL IDs) using the remapping layer, accumulate cost and time, apply time window at each vertex and then stores everything into an output struct `OutputRow` with the correct fields.

## Building and running
```
g++ -std=c++17 poc.cpp -I"path/to/boost" -o poc
./poc
```

## Tested Graphs
### Test 1
#### Vertices
<img width="205" height="246" alt="image" src="https://github.com/user-attachments/assets/78170b3a-e64a-4c47-8de6-f9cced840b7f" />

#### Edges
<img width="432" height="398" alt="image" src="https://github.com/user-attachments/assets/d2c203ad-fafe-4189-9359-c197692a91bc" />

#### Output
<img width="651" height="205" alt="image" src="https://github.com/user-attachments/assets/852be139-c188-467f-8473-791adda4fbe0" />

#### Why this is correct
A->C: arrive t=5, wait → t=6

C->B: travel 3. Arrive at B at t=9. (Within time window)​

B->D: travel 2. Arrive at D at t=11. (Within time window)

D->E: travel 3. Arrive at E at t=14. (Within time window)

Total: cost=10, time=14

#### Why A->C->D->E fails
A->C: arrive t=5, wait -> t=6

C->D: travel 8. Arrive at D at t=14. D's window is [3, 12] -> 14 > 12

REF returns false therefore label is discarded.


### Test 2
#### Vertices
<img width="209" height="216" alt="image" src="https://github.com/user-attachments/assets/76228496-737c-44bd-9bf7-ba01597c94ba" />

#### Edges
<img width="411" height="217" alt="image" src="https://github.com/user-attachments/assets/a114c39c-04b1-48fc-9e68-5c1aba999b4c" />

#### Output
<img width="551" height="295" alt="image" src="https://github.com/user-attachments/assets/61a941d2-4d2d-4483-a66f-1caf9898f7b7" />

- Path 1 (1 -> 2 -> 4): cheaper (cost = 2).
- Path 2 (1 -> 3 -> 4): faster (time = 7). 
- Neither dominates the other, both returned.
