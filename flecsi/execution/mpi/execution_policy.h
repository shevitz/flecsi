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

#ifndef flecsi_execution_mpi_execution_policy_h
#define flecsi_execution_mpi_execution_policy_h

//----------------------------------------------------------------------------//
//! @file
//! @date Initial file creation: Nov 15, 2015
//----------------------------------------------------------------------------//

#include <functional>
#include <memory>
#include <type_traits>
#include <future>
#include <cinchlog.h>

#include "flecsi/execution/common/processor.h"
#include "flecsi/execution/context.h"
#include "flecsi/execution/mpi/task_wrapper.h"
#include "flecsi/execution/mpi/task_prolog.h"
#include "flecsi/execution/mpi/task_epilog.h"
#include "flecsi/execution/mpi/finalize_handles.h"
#include "flecsi/execution/mpi/future.h"

namespace flecsi {
namespace execution {

///
/// Executor interface.
///
template<
  typename RETURN,
  typename ARG_TUPLE>
struct executor__
{
  ///
  ///
  ///
  template<
    typename T,
    typename A
  >
  static
  decltype(auto)
  execute(
    T fun,
    A && targs
  )
  {
    auto user_fun = (reinterpret_cast<RETURN(*)(ARG_TUPLE)>(fun));
    mpi_future__<RETURN> fut;
    fut.set(user_fun(targs));
    return fut;
  } // execute_task
}; // struct executor__

template<
  typename ARG_TUPLE
>
struct executor__<void, ARG_TUPLE>
{
  ///
  ///
  ///
  template<
    typename T,
    typename A
  >
  static
  decltype(auto)
  execute(
    T fun,
    A && targs
  )
  {
    auto user_fun = (reinterpret_cast<void(*)(ARG_TUPLE)>(fun));

    mpi_future__<void> fut;
    user_fun(targs);

    return fut;
  } // execute_task
}; // struct executor__

//----------------------------------------------------------------------------//
// Execution policy.
//----------------------------------------------------------------------------//

//----------------------------------------------------------------------------//
//! The mpi_execution_policy_t is the backend runtime execution policy
//! for MPI.
//!
//! @ingroup mpi-execution
//----------------------------------------------------------------------------//

struct mpi_execution_policy_t
{
  //--------------------------------------------------------------------------//
  //! The future__ type may be used for explicit synchronization of tasks.
  //!
  //! @tparam RETURN The return type of the task.
  //--------------------------------------------------------------------------//

  template<typename RETURN>
  using future__ = mpi_future__<RETURN>;

  template<
    typename FUNCTOR_TYPE
  >
  using functor_task_wrapper__ =
    typename flecsi::execution::functor_task_wrapper__<FUNCTOR_TYPE>;

  //--------------------------------------------------------------------------//
  //! The runtime_state_t type identifies a public type for the high-level
  //! runtime interface to pass state required by the backend.
  //--------------------------------------------------------------------------//
  struct runtime_state_t {};
  //using runtime_state_t = mpi_runtime_state_t;

  //--------------------------------------------------------------------------//
  //! Return the runtime state of the calling FleCSI task.
  //!
  //! @param task The calling task.
  //--------------------------------------------------------------------------//

  static
  runtime_state_t &
  runtime_state(
    void * task
  );

  //--------------------------------------------------------------------------//
  // Task interface.
  //--------------------------------------------------------------------------//

  //--------------------------------------------------------------------------//
  //! Legion backend task registration. For documentation on this
  //! method please see task__::register_task.
  //--------------------------------------------------------------------------//

  template<
    size_t KEY,
    typename RETURN,
    typename ARG_TUPLE,
    RETURN (*DELEGATE)(ARG_TUPLE)
  >
  static
  bool
  register_task(
     processor_type_t processor,
     launch_t launch,
     std::string name
  )
  {
    return context_t::instance().template register_function<
      RETURN, ARG_TUPLE, DELEGATE, KEY>();
  } // register_task

  //--------------------------------------------------------------------------//
  //! MPI backend task execution. For documentation on this method,
  //! please see task__::execute_task.
  //--------------------------------------------------------------------------//

  template<
    size_t KEY,
    typename RETURN,
    typename ARG_TUPLE,
    typename ... ARGS
  >
  static
  decltype(auto)
  execute_task(
    launch_type_t launch,
    size_t parent,
    ARGS && ... args
  )
  {
    auto fun = context_t::instance().function(KEY);
    // Make a tuple from the task arguments.
    ARG_TUPLE task_args = std::make_tuple(args ...);

    auto begin = std::chrono::high_resolution_clock::now();
    // run task_prolog to copy ghost cells.
    task_prolog_t task_prolog;
    task_prolog.walk(task_args);
    auto end = std::chrono::high_resolution_clock::now();
//    clog_rank(warn, 0) << "task_prolog:  "
//              << std::chrono::duration_cast<std::chrono::microseconds>(end-begin).count()
//              << "us" << std::endl;

    begin = std::chrono::high_resolution_clock::now();
    auto fut = executor__<RETURN, ARG_TUPLE>::execute(fun, task_args);
    end = std::chrono::high_resolution_clock::now();
//    clog_rank(warn, 0) << "task_execute: "
//              << std::chrono::duration_cast<std::chrono::microseconds>(end-begin).count()
//              << "us" << std::endl;

    begin = std::chrono::high_resolution_clock::now();
    task_epilog_t task_epilog;
    task_epilog.walk(task_args);
    end = std::chrono::high_resolution_clock::now();
//    clog_rank(warn, 0)<< "task_epilog:  "
//              << std::chrono::duration_cast<std::chrono::microseconds>(end-begin).count()
//              << "us" << std::endl;

    finalize_handles_t finalize_handles;
    finalize_handles.walk(task_args);

    return fut;
  } // execute_task

  //--------------------------------------------------------------------------//
  // Function interface.
  //--------------------------------------------------------------------------//

  //--------------------------------------------------------------------------//
  //! MPI backend function registration. For documentation on this
  //! method, please see function__::register_function.
  //--------------------------------------------------------------------------//

  template<
    typename RETURN,
    typename ARG_TUPLE,
    RETURN (*FUNCTION)(ARG_TUPLE),
    size_t KEY
  >
  static
  bool
  register_function()
  {
    return context_t::instance().template register_function<
      RETURN, ARG_TUPLE, FUNCTION, KEY>();
  } // register_function

  //--------------------------------------------------------------------------//
  //! MPI backend function execution. For documentation on this
  //! method, please see function__::execute_function.
  //--------------------------------------------------------------------------//

  template<
    typename FUNCTION_HANDLE,
    typename ... ARGS
  >
  static
  decltype(auto)
  execute_function(
    FUNCTION_HANDLE & handle,
    ARGS && ... args
  )
  {
    return handle(context_t::instance().function(handle.key()),
      std::forward_as_tuple(args ...));
  } // execute_function

}; // struct mpi_execution_policy_t

} // namespace execution 
} // namespace flecsi

#endif // flecsi_execution_mpi_execution_policy_h

/*~-------------------------------------------------------------------------~-*
 * Formatting options
 * vim: set tabstop=2 shiftwidth=2 expandtab :
 *~-------------------------------------------------------------------------~-*/
