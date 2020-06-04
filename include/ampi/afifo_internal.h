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
  // afifo_result_iterator_tmpl
  //
  //----------------------------------------------------------------------------------------------------------------------
  template<typename USER_OBJ_TYPE>
  class afifo_internal_tmpl;
  
  template<typename USER_OBJ_TYPE>
  class afifo_result_iterator_tmpl
    {
  public:
    using user_obj_type = USER_OBJ_TYPE;
    using node_type = lifo_node_t<user_obj_type>;
    using pointer_type = node_type *;
    
  protected:
    node_type * linked_list_;
    
  public:
    afifo_result_iterator_tmpl() noexcept : linked_list_{} {}
    explicit afifo_result_iterator_tmpl( node_type * linked_list ) noexcept : linked_list_{linked_list} {}
    afifo_result_iterator_tmpl( afifo_result_iterator_tmpl && rh ) noexcept ;
    afifo_result_iterator_tmpl & operator=( afifo_result_iterator_tmpl && rh ) noexcept { swap( rh ); return *this; }
    
    bool empty() const noexcept { return linked_list_ == nullptr; }
    
    node_type * pull();
    void swap( afifo_result_iterator_tmpl & rh ) noexcept;
    };
    
  template<typename T>
  afifo_result_iterator_tmpl<T>::afifo_result_iterator_tmpl( afifo_result_iterator_tmpl && rh ) noexcept :
      linked_list_{rh.linked_list_} 
    {
    rh.linked_list_ = nullptr;
    }
  
  template<typename T>
  void afifo_result_iterator_tmpl<T>::swap( afifo_result_iterator_tmpl & rh ) noexcept
    {
    std::swap(linked_list_,rh.linked_list_);
    }
    
  template<typename T>
  typename afifo_result_iterator_tmpl<T>::node_type *
  afifo_result_iterator_tmpl<T>::pull()
    {
    node_type * result {};
    if( linked_list_ != nullptr )
      {
      result = linked_list_;
      linked_list_ = linked_list_->next;
      }
    return result;
    }
    
  //----------------------------------------------------------------------------------------------------------------------
  //
  // afifo_internal_tmpl
  // aggregated pop queue
  //
  //----------------------------------------------------------------------------------------------------------------------

  ///\brief lifo aggregated pop queue used internaly for node managment
  template<typename USER_OBJ_TYPE>
  class afifo_internal_tmpl
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
    afifo_internal_tmpl() : head_{} , size_{}, finish_wating_{} {}
    ~afifo_internal_tmpl(){}
    afifo_internal_tmpl( afifo_internal_tmpl const & ) = delete;
    afifo_internal_tmpl & operator=( afifo_internal_tmpl const & ) = delete;
    
  public:
    ///\brief enqueues supplyied node
    void push( node_type * user_data [[gnu::nonnull]] );
    
    ///\brief single try to dequeue entire linked list
    ///\description @{
    /// when queue is empty returns imediatly
    /// when queue is not empty it retries infinitie number of times until it succeeds or queue becomes empty
    ///@}
    ///\returns linked list of nodes with fifo order
    node_type * pull();
    
    ///\breif waits until pop succeeds with sleeping between retrys
    ///\param sleep_millisec time in miliseconds of sleeping
    node_type * pull_wait( size_t sleep_millisec );
    static node_type * reverse( node_type * node_llist ) noexcept;
    };
    
  template<typename T>
  void afifo_internal_tmpl<T>::push( node_type * next_node [[gnu::nonnull]] )
    {
    if( !finish_waiting() )
      {
      //atomic linked list
      bool node_submitted {};
      do
        {
        pointer_type last_head { head_.load( std::memory_order_relaxed ) };
        next_node->next = last_head;
        node_submitted = head_.compare_exchange_weak(last_head, next_node, std::memory_order_release, std::memory_order_relaxed  );
        }
      while(!node_submitted);
      size_.fetch_add(1,std::memory_order_relaxed );
      }
    }
    
  template<typename T>
  typename afifo_internal_tmpl<T>::node_type * 
  afifo_internal_tmpl<T>::pull()
    {
    pointer_type head_to_dequeue{ head_.load(std::memory_order_relaxed) };

    for (;nullptr != head_to_dequeue; //return when nothing left in queue
            // Keep trying until Dequeue is done
            head_to_dequeue = head_.load(std::memory_order_relaxed) )
      {
      //if swap succeeds new head is estabilished
      bool deque_is_done = head_.compare_exchange_weak( head_to_dequeue, pointer_type{} );
      if( deque_is_done )
        {
        size_type size_to_sub {1};
        for( auto node{ head_to_dequeue->next }; node != nullptr; node = node->next)
           ++size_to_sub; 
        size_.fetch_sub( 1, std::memory_order_release );
        break;
        }
      }
    //reverse order for fifo, do any one needs here lifo order ?
    return reverse(head_to_dequeue);
    }
    
  template<typename T>
  typename afifo_internal_tmpl<T>::node_type * 
  afifo_internal_tmpl<T>::reverse( node_type * llist ) noexcept
    {
    node_type * prev {};
    for( ; nullptr != llist; )
      {
      node_type * next { llist->next };
      llist->next = prev;
      prev = llist;
      llist = next;
      }
    return prev;
    }
    
}
