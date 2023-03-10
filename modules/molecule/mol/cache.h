#pragma once

#include <string_view>
#include <span>
#include <md_types.h>

struct md_molecule_t;
struct md_trajectory_i;

// @NOTE(Robin) If you want to go full blown cplusplus bananas mode, you can use smart pointers (shared external + weak internal) to keep some type of reference counting going
// and free resources when they are not held by any external pointers anymore. I'm not going to care about that right now.

namespace mol_manager {
    const md_molecule_t*   load_molecule(std::string_view path_to_file, bool coarse_grained = false);
    const md_trajectory_i* load_trajectory(std::string_view path_to_file, const md_molecule_t* mol = nullptr, bool deperiodize_on_load = false);
    
    // This will kick of work into worker threads and thus not stall the calling thread
    void prefetch_frame_range(const md_trajectory_i* traj, int64_t beg, int64_t end);

    std::span<const md_secondary_structure_t> get_secondary_structure_frame_data(const md_trajectory_i* traj, int64_t frame);
}