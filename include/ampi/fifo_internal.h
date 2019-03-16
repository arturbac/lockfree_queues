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
    
  private:
    struct pimpl_t 
      {
      std::atomic<pointer_type>  delayed_reclamtion_;
      std::atomic<pointer_type>  head_;
      std::atomic<pointer_type>  tail_;
      std::atomic<size_type>     size_;
      
      pimpl_t() : size_() {}
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
    void delay_reclamation( pointer_type ptr );
  };
  

  template<typename USER_OBJ_TYPE>
  fifo_queue_internal_tmpl<USER_OBJ_TYPE>::fifo_queue_internal_tmpl()
    : data_{ std::make_unique<typename fifo_queue_internal_tmpl<USER_OBJ_TYPE>::pimpl_t>() }
  {
    node_type * node = new node_type(); // Allocate a free node
                      // Make it the only node in the linked list
    data_->head_.store( pointer_type( node ) );
    data_->tail_.store( pointer_type( node ) );        // Both Head and Tail point to it
    static_assert( sizeof(pointer_type) == 8, "64bit only supported TODO 32bit" );
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
      delay_reclamation( pointer_type{} );
      }
    catch(...)
      {}
    }

  template<typename T>
  void fifo_queue_internal_tmpl<T>::push( user_obj_type * user_data )
    {
    pointer_type tail_local {};
    // Allocate a new node from the free list
    
    std::unique_ptr<node_type> node{ std::make_unique<node_type>( user_obj_ptr_type{user_data} ) };
                                                                    // Set next pointer of node to NULL
    for(;;)                                                     // Keep trying until Enqueue is done
      {
      // Read Tail.ptr and Tail.count together
      tail_local = data_->tail_.load( std::memory_order_relaxed );        
      node_type * tail_local_ptr = tail_local.get();
    
      // Read next ptr and count fields together
      pointer_type next { tail_local_ptr->next };      

      // Are tail_local and next consistent?
      if ( tail_local ==  data_->tail_.load( std::memory_order_acquire ) )
        {
        // Was Tail pointing to the last node?
        if( next.get() == nullptr)
          {
          // Try to link node at the end of the linked list
          if( tail_local->next.compare_exchange_strong( next, pointer_type{ node.get(), next.count() + 1 } ) )
            // Enqueue is done.  Exit loop
            break;      
          }
        // Tail was not pointing to the last node
        else
          // Try to swing Tail to the next node
          data_->tail_.compare_exchange_strong( tail_local, pointer_type{ next.get(), tail_local.count() + 1 } );
        }
      }
    // Enqueue is done.  Try to swing Tail to the inserted node
    data_->tail_.compare_exchange_strong( tail_local, {node.get(), tail_local.count() + 1} );
    node.release();
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
          data_->tail_.compare_exchange_strong(tail, pointer_type{next.get(), tail.count() + 1} );
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
            if ( data_->head_.compare_exchange_strong( head, pointer_type{next.get(), head.count() + 1}))
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

  template<typename T>
  void fifo_queue_internal_tmpl<T>::delay_reclamation( pointer_type next_todel )
    {
    pointer_type old { data_->delayed_reclamtion_.load( std::memory_order_relaxed ) };
    while( !data_->delayed_reclamtion_.compare_exchange_weak( old, next_todel, std::memory_order_release, std::memory_order_relaxed ) )
      {
      old = data_->delayed_reclamtion_.load( std::memory_order_relaxed );
      }
    if( old.get() != nullptr )
      {
      delete old.get();
//       printf("reclaim %X", (uintptr_t)old.get() );
      }
    }
}
