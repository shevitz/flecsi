/*~--------------------------------------------------------------------------~*
 * Copyright (c) 2015 Los Alamos National Security, LLC
 * All rights reserved.
 *~--------------------------------------------------------------------------~*/

#pragma once

//----------------------------------------------------------------------------//
//! @file
//! @date Initial file creation: Jun 21, 2017
//----------------------------------------------------------------------------//

#include <flecsi/data/common/registration_wrapper.h>
#include <flecsi/data/hpx/dense.h>
#include <flecsi/data/hpx/sparse.h>
#include <flecsi/data/storage.h>
#include <flecsi/data/hpx/global.h>
#include <flecsi/data/hpx/color.h>

namespace flecsi {
namespace data {

//----------------------------------------------------------------------------//
//! FIXME: Description of class
//----------------------------------------------------------------------------//

struct hpx_data_policy_t
{
  //--------------------------------------------------------------------------//
  //! The storage_class__ type determines the underlying storage mechanism
  //! for the backend runtime.
  //--------------------------------------------------------------------------//

  template<
    size_t STORAGE_CLASS
  >
  using storage_class__ = hpx::storage_class__<STORAGE_CLASS>;

}; // class hpx_data_policy_t

} // namespace data
} // namespace flecsi

/*~-------------------------------------------------------------------------~-*
*~-------------------------------------------------------------------------~-*/