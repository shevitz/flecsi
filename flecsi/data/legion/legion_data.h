/*~--------------------------------------------------------------------------~*
 *  @@@@@@@@  @@           @@@@@@   @@@@@@@@ @@
 * /@@/////  /@@          @@////@@ @@////// /@@
 * /@@       /@@  @@@@@  @@    // /@@       /@@
 * /@@@@@@@  /@@ @@///@@/@@       /@@@@@@@@@/@@
 * /@@////   /@@/@@@@@@@/@@       ////////@@/@@
 * /@@       /@@/@@//// //@@    @@       /@@/@@
 * /@@       @@@//@@@@@@ //@@@@@@  @@@@@@@@ /@@
 * //       ///  //////   //////  ////////  //
 *
 * Copyright (c) 2016 Los Alamos National Laboratory, LLC
 * All rights reserved
 *~--------------------------------------------------------------------------~*/

#ifndef flecsi_legion_legion_data_h
#define flecsi_legion_legion_data_h

#include <cassert>
#include <legion.h>
#include <map>
#include <unordered_map>
#include <vector>

#include "flecsi/execution/context.h"
#include "flecsi/coloring/index_coloring.h"
#include "flecsi/coloring/coloring_types.h"

///
/// \file legion/legion_data.h
/// \date Initial file creation: Jun 7, 2017
///

namespace flecsi{

namespace data{

class legion_data_t{
public:
  using coloring_info_t = coloring::coloring_info_t;

  using coloring_info_map_t = std::unordered_map<size_t, coloring_info_t>;

  using indexed_coloring_info_map_t = 
    std::unordered_map<size_t, std::unordered_map<size_t, coloring_info_t>>;

  struct index_space_info_t{
    size_t index_space_id;
    Legion::IndexSpace index_space;
    Legion::FieldSpace field_space;
    Legion::LogicalRegion logical_region;
    Legion::IndexPartition index_partition;
    size_t total_num_entities;
  };

  struct connectvity_t{
    size_t from_index_space;
    size_t to_index_space;
    Legion::IndexSpace index_space;
    Legion::FieldSpace field_space;
    Legion::LogicalRegion logical_region;
    Legion::IndexPartition index_partition;
    size_t max_conn_size;
  };

  legion_data_t(
    Legion::Context ctx,
    Legion::Runtime* runtime,
    size_t num_colors
  )
  : ctx_(ctx),
    runtime_(runtime),
    num_colors_(num_colors),
    color_bounds_(0, num_colors_ - 1),
    color_domain_(Legion::Domain::from_rect<1>(color_bounds_))
  {
  }

  ~legion_data_t()
  {
    for(auto& itr : index_space_map_){
      index_space_info_t& is = itr.second;

      runtime_->destroy_index_partition(ctx_, is.index_partition);
      runtime_->destroy_index_space(ctx_, is.index_space);
      runtime_->destroy_field_space(ctx_, is.field_space);
      runtime_->destroy_logical_region(ctx_, is.logical_region);
    }
  }

  void
  init_from_coloring_info_map(
    const indexed_coloring_info_map_t& indexed_coloring_info_map
  )
  {
    for(auto& idx_space : indexed_coloring_info_map){
      add_index_space(idx_space.first, idx_space.second);
    }
  }

  void
  add_connectivty()
  {
    using namespace execution;

    context_t & context = context_t::instance();

    for(auto& p : context.adjacencies()){
      connectvity_t c;
      c.from_index_space = p.first;
      c.to_index_space = p.second;

      connectivity_map_.emplace(p, std::move(c));
    }
  }

  void
  add_index_space(
    size_t index_space_id,
    const coloring_info_map_t& coloring_info_map
  )
  {
    using namespace std;
    
    using namespace Legion;
    using namespace LegionRuntime;
    using namespace Arrays;

    using namespace execution;

    context_t & context = context_t::instance();

    index_space_info_t is;
    is.index_space_id = index_space_id;

    // Create expanded IndexSpace
    index_spaces_.insert(index_space_id);

    // Determine max size of a color partition
    is.total_num_entities = 0;
    for(auto color_idx : coloring_info_map){
      clog(trace) << "index: " << index_space_id << " color: " << 
        color_idx.first << " " << color_idx.second << std::endl;
      
      is.total_num_entities = std::max(is.total_num_entities,
        color_idx.second.exclusive + color_idx.second.shared + 
        color_idx.second.ghost);
    } // for color_idx
    
    clog(trace) << "total_num_entities " << is.total_num_entities << std::endl;

    // Create expanded index space
    Rect<2> expanded_bounds = 
      Rect<2>(Point<2>::ZEROES(), make_point(num_colors_, is.total_num_entities));
    
    Domain expanded_dom(Domain::from_rect<2>(expanded_bounds));
    
    is.index_space = runtime_->create_index_space(ctx_, expanded_dom);
    attach_name(is, is.index_space, "expanded index space");

    // Read user + FleCSI registered field spaces
    is.field_space = runtime_->create_field_space(ctx_);

    FieldAllocator allocator = 
      runtime_->create_field_allocator(ctx_, is.field_space);

    auto ghost_owner_pos_fid = FieldID(internal_field::ghost_owner_pos);

    allocator.allocate_field(sizeof(Point<2>), ghost_owner_pos_fid);

    using field_info_t = context_t::field_info_t;

    for(const field_info_t& fi : context.registered_fields()){
      if(fi.index_space == index_space_id){
        allocator.allocate_field(fi.size, fi.fid);
      }
    }

    for(auto& p : context.adjacencies()){
      FieldID adjacency_fid = 
        size_t(internal_field::connectivity_pos_start) + 
        p.first * 10 + p.second;
      
      allocator.allocate_field(sizeof(Point<2>), adjacency_fid);
    }

    attach_name(is, is.field_space, "expanded field space");

    is.logical_region = runtime_->create_logical_region(ctx_, is.index_space, is.field_space);
    attach_name(is, is.logical_region, "expanded logical region");

    // Partition expanded IndexSpace color-wise & create associated PhaseBarriers
    DomainColoring color_partitioning;
    for(int color = 0; color < num_colors_; color++){
      auto citr = coloring_info_map.find(color);
      clog_assert(citr != coloring_info_map.end(), "invalid color info");
      const coloring_info_t& color_info = citr->second;

      Rect<2> subrect(
          make_point(color, 0),
          make_point(color,
            color_info.exclusive + color_info.shared + color_info.ghost - 1));
      
      color_partitioning[color] = Domain::from_rect<2>(subrect);
    }

    is.index_partition = runtime_->create_index_partition(ctx_,
      is.index_space, color_domain_, color_partitioning, true /*disjoint*/);
    attach_name(is, is.index_partition, "color partitioning");

    index_space_map_[index_space_id] = std::move(is);
  }

  const index_space_info_t&
  index_space_info(
    size_t index_space_id
  )
  const
  {
    auto itr = index_space_map_.find(index_space_id);
    clog_assert(itr != index_space_map_.end(), "invalid index space");
    return itr->second;
  }

  const std::set<size_t>&
  index_spaces()
  const
  {
    return index_spaces_;
  }

  const Legion::Domain&
  color_domain()
  const
  {
    return color_domain_;
  }

  const std::map<std::pair<size_t, size_t>, connectvity_t>&
  connectivity_map()
  const
  {
    return connectivity_map_;
  }

  void
  init_connectivity()
  {
    using namespace std;
    
    using namespace Legion;
    using namespace LegionRuntime;
    using namespace Arrays;

    using namespace execution;

    context_t & context = context_t::instance();

    for(auto& itr : connectivity_map_){
      connectvity_t& c = itr.second;

      const index_space_info_t& fi = index_space_map_[c.from_index_space];
      const index_space_info_t& ti = index_space_map_[c.to_index_space];

      c.max_conn_size = fi.total_num_entities * ti.total_num_entities;

      // Create expanded index space
      Rect<2> expanded_bounds = 
        Rect<2>(Point<2>::ZEROES(), make_point(num_colors_, c.max_conn_size));
      
      Domain expanded_dom(Domain::from_rect<2>(expanded_bounds));
      c.index_space = runtime_->create_index_space(ctx_, expanded_dom);
      attach_name(c, c.index_space, "expanded index space");

      // Read user + FleCSI registered field spaces
      c.field_space = runtime_->create_field_space(ctx_);

      FieldAllocator allocator = 
        runtime_->create_field_allocator(ctx_, c.field_space);

      auto connectivity_offset_fid = 
        FieldID(internal_field::connectivity_offset);

      allocator.allocate_field(sizeof(size_t), connectivity_offset_fid);

      attach_name(c, c.field_space, "expanded field space");

      c.logical_region = 
        runtime_->create_logical_region(ctx_, c.index_space, c.field_space);
      attach_name(c, c.logical_region, "expanded logical region");

      // Partition expanded IndexSpace color-wise & create associated PhaseBarriers
      DomainColoring color_partitioning;
      for(int color = 0; color < num_colors_; color++){
        Rect<2> subrect(make_point(color, 0), make_point(color,
          c.max_conn_size - 1));
        
        color_partitioning[color] = Domain::from_rect<2>(subrect);
      }

      c.index_partition = runtime_->create_index_partition(ctx_,
        c.index_space, color_domain_, color_partitioning, true /*disjoint*/);
      attach_name(c, c.index_partition, "color partitioning");
    }
  }

private:
  
  Legion::Context ctx_;

  Legion::HighLevelRuntime* runtime_;

  size_t num_colors_;

  LegionRuntime::Arrays::Rect<1> color_bounds_;

  Legion::Domain color_domain_;

  std::set<size_t> index_spaces_;

  std::unordered_map<size_t, index_space_info_t> index_space_map_;
  
  std::map<std::pair<size_t, size_t>, connectvity_t> connectivity_map_;

  template<
    class T
  >
  void
  attach_name(
    const index_space_info_t& is,
    T& x,
    const char* label
  )
  {
    std::stringstream sstr;
    sstr << label << " " << is.index_space_id;
    runtime_->attach_name(x, sstr.str().c_str());
  }

  template<
    class T
  >
  void
  attach_name(
    const connectvity_t& c,
    T& x,
    const char* label
  )
  {
    std::stringstream sstr;
    sstr << label << " " << c.from_index_space << "->" << c.to_index_space;
    runtime_->attach_name(x, sstr.str().c_str());
  }

}; // struct legion_data_t

} // namespace data

} // namespace flecsi

#endif // flecsi_legion_legion_data_h

/*~-------------------------------------------------------------------------~-*
 * Formatting options
 * vim: set tabstop=2 shiftwidth=2 expandtab :
 *~-------------------------------------------------------------------------~-*/
