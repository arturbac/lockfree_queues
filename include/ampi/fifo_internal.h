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
#include <array>
#include <algorithm>

namespace ampi
{
  
  template<typename USER_OBJ_TYPE>
  struct fifo_node_t
    {
    typedef USER_OBJ_TYPE               user_obj_type;
    typedef fifo_node_t<user_obj_type>  class_type;
    using pointer_type = pointer_t<fifo_node_t<user_obj_type>>;

    user_obj_type     value;
    std::atomic<pointer_type> next;
      
    fifo_node_t() : value(),  next() {}
    fifo_node_t( user_obj_type && data ) : value( std::forward<user_obj_type>(data)), next() {}
    };

    
  template<typename USER_OBJ_TYPE>
  struct queue_envelope_t
    {
    using user_obj_type = USER_OBJ_TYPE ;
    user_obj_type value;
    
    queue_envelope_t( user_obj_type && v ) : value( std::forward<user_obj_type>(v)){}
    };

    
  //----------------------------------------------------------------------------------------------------------------------
  //
  // fifo_queue_internal_tmpl
  //
  //----------------------------------------------------------------------------------------------------------------------

    
  template<typename USER_OBJ_TYPE>
  class fifo_queue_internal_tmpl
    {
  public:
    using user_obj_type =  USER_OBJ_TYPE;
    using node_type = fifo_node_t<user_obj_type *>;
    using pointer = node_type *;
    using pointer_type = pointer_t<node_type>;
    using user_obj_ptr_type = user_obj_type *;
    using size_type = long;
    using reclaim_counter_type = uint32_t;
  private:
    struct lock_counter_t
      {
      reclaim_counter_type 
          counter : 31,
          lock : 1;
      };
      
    struct reclaimed_t
      {
      pointer_type pointer;
      std::atomic<lock_counter_t> lock_counter;
      };
    using reaclaim_array_t = std::array<reclaimed_t,512>;
    
    struct pimpl_t 
      {
      reaclaim_array_t                  delayed_reclamtion_;
      std::atomic<reclaim_counter_type> reclaim_counter_;
      std::atomic<pointer_type>  head_;
      std::atomic<pointer_type>  tail_;
      std::atomic<size_type>     size_;
      
      pimpl_t() : 
          delayed_reclamtion_{},
          reclaim_counter_{},
          head_{},
          tail_{},
          size_{} 
        {}
      };
    std::unique_ptr<pimpl_t>  data_;
      
  public:
    bool        empty() const noexcept           { return data_->size_.load(std::memory_order_acquire) == 0; }
    size_type   size() const  noexcept           { return data_->size_.load(std::memory_order_acquire); }

  public:
    fifo_queue_internal_tmpl();
    ~fifo_queue_internal_tmpl();
    fifo_queue_internal_tmpl( fifo_queue_internal_tmpl const & ) = delete;
    fifo_queue_internal_tmpl & operator=( fifo_queue_internal_tmpl const & ) = delete;
  
  public:
    void push( user_obj_type * user_data );
    user_obj_type * pull();
    
  private:
    typename reaclaim_array_t::iterator oldest_store() noexcept;
    void delay_reclamation( pointer_type ptr );
    pointer_type alloc();
  };
  
  template<typename T>
  typename fifo_queue_internal_tmpl<T>::reaclaim_array_t::iterator
  fifo_queue_internal_tmpl<T>::oldest_store() noexcept
    {
    return std::min_element( std::begin(data_->delayed_reclamtion_), std::end(data_->delayed_reclamtion_),
                          [](reclaimed_t const & l, reclaimed_t const & r)
                          {
                          lock_counter_t ll { l.lock_counter.load(std::memory_order_relaxed) };
                          lock_counter_t rl { r.lock_counter.load(std::memory_order_relaxed) };
                          if( ll.lock == rl.lock )
                            return  ll.counter < rl.counter;
                          return ll.lock < rl.lock;
                          } );
    }
    
  template<typename T>
  fifo_queue_internal_tmpl<T>::fifo_queue_internal_tmpl() :
      data_{ std::make_unique<pimpl_t>() }
    {
    node_type * node = new node_type(); // Allocate a free node
                      // Make it the only node in the linked list
    data_->head_.store( pointer_type( node ) );
    data_->tail_.store( pointer_type( node ) );        // Both Head and Tail point to it
    data_->reclaim_counter_ = 1;
    static_assert( sizeof(pointer_type) == 8, "64bit only supported TODO 32bit" );
    for( reclaimed_t & el : data_->delayed_reclamtion_ )
      el.lock_counter.store(lock_counter_t{0,0});
    }
  
  template<typename T>
  typename fifo_queue_internal_tmpl<T>::pointer_type
  fifo_queue_internal_tmpl<T>::alloc()
    {
    auto to_reuse { std::find_if(std::begin(data_->delayed_reclamtion_), std::end(data_->delayed_reclamtion_), 
      []( reclaimed_t const & l )
      {
      lock_counter_t ll { l.lock_counter.load(std::memory_order_relaxed) };
      return ll.lock == 0 && l.pointer.get() != nullptr;
      }) };
      
    if( to_reuse != std::end(data_->delayed_reclamtion_) )
      {
      reclaimed_t & el { *to_reuse };
      lock_counter_t lcexpected = el.lock_counter.load(std::memory_order_relaxed);
      lock_counter_t lc_locked {lcexpected.counter, true };
      if( el.lock_counter.compare_exchange_weak( lcexpected, lc_locked, std::memory_order_release, std::memory_order_relaxed ))
        {
        pointer_type reclaim{};
        std::swap( el.pointer, reclaim );
        lock_counter_t lc_unlocked { 0, false };
        el.lock_counter.store( lc_unlocked, std::memory_order_release );
        if( reclaim.get() != nullptr )
          return reclaim;
        }
      }
    return pointer_type{ new node_type() };
    }
  

  template<typename T>
  void fifo_queue_internal_tmpl<T>::delay_reclamation( pointer_type reclaim )
    {
    bool reclaimed {};
    do
      {
      //find oldest reclaiming node with lowest counter and unlocked status;
      auto oldest_to_reclaim { oldest_store() };
      //try to swap it with own
      reclaimed_t & el { *oldest_to_reclaim };
      lock_counter_t lcexpected = el.lock_counter.load(std::memory_order_relaxed);
//       printf("reclaim %ld ->%u\n", std::distance( std::begin(data_->delayed_reclamtion_), oldest_to_reclaim), lcexpected.counter );
      if( !lcexpected.lock )
        {
        lock_counter_t lc_locked {lcexpected.counter, true };
        if( el.lock_counter.compare_exchange_weak( lcexpected, lc_locked, std::memory_order_release, std::memory_order_relaxed ))
          {
          std::swap( el.pointer, reclaim );
          lock_counter_t lc_unlocked {
                      data_->reclaim_counter_.fetch_add( std::memory_order_acquire ),
                      false };
          el.lock_counter.store( lc_unlocked, std::memory_order_release );
        
          node_type * other_to_del { reclaim.get() };
          if( other_to_del != nullptr )
            delete other_to_del;
          
          reclaimed = true;
          }
        }
      }
    while(!reclaimed);
// //       printf("reclaim %X", (uintptr_t)old.get() );
    }
    
  template<typename T>
  fifo_queue_internal_tmpl<T>::~fifo_queue_internal_tmpl()
    {
    try 
      {
//       if( !m_finish_wating)
//         m_finish_wating = true;

      user_obj_type * any_data;
      while ((any_data = pull()) != nullptr);
      delete data_->head_.load().get();
//       delay_reclamation( pointer_type{} );
      for( reclaimed_t & el : data_->delayed_reclamtion_ )
        {
        if( el.pointer.get () != nullptr )
          delete el.pointer.get();
        }
      }
    catch(...)
      {}
    }

  template<typename T>
  void fifo_queue_internal_tmpl<T>::push( user_obj_type * user_data )
    {
    pointer_type tail_local {};
    // Allocate a new node from the free list
    node_type * node{ alloc().get() };
    node->value = user_data; 
    // Set next pointer of node to NULL
    node->next = pointer_type{};
    // Keep trying until Enqueue is done
    for(;;)
      {
      // Read Tail.ptr and Tail.count together
      tail_local = data_->tail_.load( std::memory_order_relaxed );        
    
      // Read next ptr and count fields together
      pointer_type next { tail_local.get()->next };      

      // Are tail_local and next consistent?
      if ( tail_local ==  data_->tail_.load( std::memory_order_acquire ) )
        {
        // Was Tail pointing to the last node?
        if( next.get() == nullptr)
          {
          // Try to link node at the end of the linked list
          if( tail_local->next.compare_exchange_strong( next, pointer_type{ node, next.count() + 1 }, std::memory_order_release, std::memory_order_relaxed ) )
            // Enqueue is done.  Exit loop
            break;      
          }
        // Tail was not pointing to the last node
        else
          // Try to swing Tail to the next node
          data_->tail_.compare_exchange_strong( tail_local, pointer_type{ next.get(), tail_local.count() + 1 }, std::memory_order_release, std::memory_order_relaxed );
        }
      }
    // Enqueue is done.  Try to swing Tail to the inserted node
    data_->tail_.compare_exchange_strong( tail_local, {node, tail_local.count() + 1} );
    data_->size_.fetch_add( size_type{1}, std::memory_order_relaxed );
    }

  template<typename T>
  typename fifo_queue_internal_tmpl<T>::user_obj_type *
  fifo_queue_internal_tmpl<T>::pull()
    {
    user_obj_type * pvalue{};
    pointer_type head;

    // Keep trying until Dequeue is done
    for (;;)
      {
      // Read Head
      head = data_->head_.load( std::memory_order_acquire );
      // Read Tail
      pointer_type tail = data_->tail_.load( std::memory_order_acquire );
    
      // Read Head.ptr->next //heap-use-after-free
      pointer_type next = head.get()->next.load( std::memory_order_acquire );
      // Are head, tail, and next consistent?
      if( head == data_->head_.load( std::memory_order_acquire ) )
        {
        // Is queue empty or Tail falling behind?
        if( head.get() == tail.get() )
          {
          // Is queue empty?
          if ( next.get() == nullptr)
            return nullptr;  
          // Tail is falling behind.  Try to advance it
          data_->tail_.compare_exchange_strong(tail, pointer_type{next.get(), tail.count() + 1}, std::memory_order_release, std::memory_order_relaxed );
          }
        else
          {
          // Read value before CAS
          //D12 Otherwise, another dequeue might free the next node
          node_type * node_d = next.get();
          if ( node_d != nullptr) 
            {
            pvalue = node_d->value;
            // Try to swing Head to the next node
            if ( data_->head_.compare_exchange_strong( head, pointer_type{next.get(), head.count() + 1}, std::memory_order_release, std::memory_order_relaxed))
              break;
            pvalue = nullptr;
            }
          }
        }
      }
    head->value = nullptr; //value is returned to user, dont leave here pointer
    
    // It is safe now to free the old node //heap-use-after-free
    delay_reclamation( head );

    data_->size_.fetch_sub(size_type{1}, std::memory_order_relaxed );
    return pvalue;   // Queue was not empty, dequeue succeeded
    }

}
