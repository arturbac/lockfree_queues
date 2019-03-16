// MIT License
// 
// Copyright (c) 2019 Artur Bac
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// Non-Blocking Concurrent Queue Algorithms, lock free
#pragma once

#include "common_utils.h"

namespace ampi
{
  //----------------------------------------------------------------------------------------------------------------------
  //
  // stack_internal_tmpl
  //
  //----------------------------------------------------------------------------------------------------------------------
  ///\brief lifo queue used internaly for node managment
  template<typename USER_OBJ_TYPE>
  class stack_internal_tmpl
    {
  public:
    using user_obj_type = USER_OBJ_TYPE;
    using node_type = lifo_node_t<user_obj_type>;
    using pointer_type = node_type *;
    using size_type = long;
    
  private:
    std::atomic<pointer_type> head_;
    std::atomic<size_type> size_;
    bool           finish_wating_;
    
  public:
    inline bool        empty() const noexcept                  { return head_.load( std::memory_order_acquire) == nullptr; }
    inline size_type   size() const noexcept                   { return size_.load( std::memory_order_acquire); }
    inline bool        finish_waiting() const noexcept         { return finish_wating_; }
    inline void        finish_waiting( bool value ) noexcept   { finish_wating_ = value ; }
    
  public:
    stack_internal_tmpl() noexcept : head_{} , size_{}, finish_wating_{} {}
    stack_internal_tmpl( stack_internal_tmpl const & ) = delete;
    stack_internal_tmpl & operator=( stack_internal_tmpl const & ) = delete;
    
  public:
    ///\brief enqueues supplyied node
    void push( node_type * user_data [[gnu::nonnull]] ) noexcept;
    
    ///\brief single try to dequeue element
    ///\description @{
    /// when queue is empty returns imediatly
    /// when queue is not empty it retries infinitie number of times until it succeeds or queue becomes empty
    ///@}
    node_type * pull() noexcept;
    
    ///\breif waits until pop succeeds with sleeping between retrys
    ///\param sleep_millisec time in miliseconds of sleeping
    node_type * pull_wait( size_t sleep_millisec ) noexcept;
    };

  
  template<typename T>
  void stack_internal_tmpl<T>::push( node_type * next_node [[gnu::nonnull]] ) noexcept
    {
    if( !finish_waiting() )
      {
      //atomic linked list
      bool node_submitted {};
      do
        {
        next_node->next = head_.load( std::memory_order_relaxed );
        node_submitted = head_.compare_exchange_weak( next_node->next, next_node, std::memory_order_release, std::memory_order_relaxed );
        }
      while(!node_submitted);
      size_.fetch_add(1,std::memory_order_relaxed);
      }
    }

  template<typename T>
  typename stack_internal_tmpl<T>::node_type * 
  stack_internal_tmpl<T>::pull() noexcept
    {
    pointer_type head_to_dequeue{ head_.load( std::memory_order_relaxed ) };

    for (;nullptr != head_to_dequeue; //return when nothing left in queue
            head_to_dequeue = head_.load( std::memory_order_relaxed ) )                                                 // Keep trying until Dequeue is done
      {
      bool deque_is_done = head_.compare_exchange_weak( head_to_dequeue, head_to_dequeue->next, std::memory_order_release, std::memory_order_relaxed );
      if( deque_is_done && head_to_dequeue != nullptr )
        {
        size_.fetch_sub(1, std::memory_order_relaxed);
        head_to_dequeue->next = pointer_type{};
        break;
        }
      }
    return head_to_dequeue;
    }
  
  template<typename T>
  typename stack_internal_tmpl<T>::node_type * 
  stack_internal_tmpl<T>::pull_wait( size_t sleep_millisec ) noexcept
    {
    for(;;)
      {
      node_type * res = pull();
      if ( res != nullptr || finish_wating_ )
        return res;

      ampi::sleep( static_cast< uint32_t>( sleep_millisec ));
      }
    }
}
