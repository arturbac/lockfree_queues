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

// fifo algorithm author Michael L. Scott mlscott at acm.org http://www.cs.rochester.edu/research/synchronization/pseudocode/queues.html
// lifo, apop and fifo implementation Artur Bac artur_at_ebasoft.com.pl
// published under MIT Licence
// ver 1.1 01.10.2009
// ver 2.0 09 03.2019
// - fixed data race under pressure when node is freed and still other thread tries to dequeue
//   push readed next when po just deleted node
// - fixed nullptr derefence at D12 (differs from oroginal algo)
// Note from my disscusion with author of algorithm
// > On Mar 9, 2019, at 6:49 AM, Artur Bać <artur@ebasoft.com.pl> wrote:
// > 
// > Hi.
// > Just wanted You to know that presented on You webpage algo has a "Data
// > Race" causing memory use after free.
// > 
// > Thread 1 - Between D2 and D5 Q->Head ..
// > dequeue
// >   D2:      head = Q->Head         // Read Head
// >   D3:      tail = Q->Tail         // Read Tail
// >   D4:      next = head.ptr->next    // Read Head.ptr->next
// >   D5:      if head == Q->Head         // Are head, tail, and next consistent?
// > 
// > Thread 2 - D20 and returned is being freed which causes read form
// > already freed pointer
// > D19:   free(head.ptr)
// > D20:   return TRUE
// > 
// > I have found this with sanitizer from clang during high pressure of
// > enqueue/dequeue
// > Possible solution to this is I thnik to use reusable node's with
// > lifo lock free queue which is simple in implementation.
// > 
// > It will be sufficient that node is not freed and its contents doesn't
// > matter because of D5 validation of Q->head.
//
//
// W dniu 09.03.2019 o 14:59, Michael L. Scott pisze:
// Thanks, Artur.  The code is actually correct, but the web page (which I hadn't looked at in years)
// is unclear: the code requires an allocator that is type-preserving and that never gives memory back to the OS.
// This requirement is clearer in the actual PODC paper.
// With the type-preserving allocator, a freed and reused node never becomes an object of a different type.
// Thus the counted pointer fields are still counted pointers and the consistency checks discover all races safely,
// despite the fact that a thread may sometimes look at a node that has been reused.
// If this strategy is unacceptable in a given real-world context (e.g., if the queue must use a shared, general purpose allocator),
// the code can be augmented easily to use hazard pointers or epoch- or interval-based reclamation.
// 
// - mls

#pragma once

#include "common_utils.h"
#include "stack_internal.h"
#include "afifo_internal.h"
#include "fifo_internal.h"
#include <memory>

namespace ampi
{

  //----------------------------------------------------------------------------------------------------------------------
  //
  // stack_t
  //
  //----------------------------------------------------------------------------------------------------------------------
  
  template<typename USER_OBJ_TYPE>
  class stack_t 
      : public stack_internal_tmpl<USER_OBJ_TYPE>
    {
  public:
    using user_obj_type =  USER_OBJ_TYPE;
    using base_type = stack_internal_tmpl<user_obj_type>;
    using node_type = typename base_type::node_type;
    
  public:
    stack_t() : base_type()/*, free_node_to_reuse_()*/ {}
    stack_t( stack_t const & ) = delete;
    stack_t & operator=( stack_t const & ) = delete;
    
    void push( user_obj_type && user_data );
    std::pair<user_obj_type, bool> pull();
    };
  
  template<typename T>
  void stack_t<T>::push( user_obj_type && user_data )
    {
    std::unique_ptr<node_type> next_node { std::make_unique<node_type>(std::forward<user_obj_type>(user_data)) };
    base_type::push( next_node.get() );
    next_node.release();
    }
    
  template<typename T>
  std::pair<typename stack_t<T>::user_obj_type, bool>  
  stack_t<T>::pull()
    {
    std::unique_ptr<node_type> detached_node { base_type::pull() };
    
    if( nullptr != detached_node )
      {
      std::pair<user_obj_type, bool> result { std::move( detached_node->value ),  true };
      return result;
      }
    return {};
    }

  //----------------------------------------------------------------------------------------------------------------------
  //
  // afifo_t
  // aggregated pop queue
  //
  //----------------------------------------------------------------------------------------------------------------------
  template<typename USER_OBJ_TYPE>
  class afifo_t ;
  
  template<typename USER_OBJ_TYPE>
  class afifo_result_iterator_t :
      protected afifo_result_iterator_tmpl<USER_OBJ_TYPE>
    {
  public:
    using user_obj_type = USER_OBJ_TYPE;
    using node_type = lifo_node_t<user_obj_type>;
    using pointer_type = node_type *;
    using parent_type = afifo_t<user_obj_type>;
    using base_type = afifo_result_iterator_tmpl<user_obj_type>;
    
  public:
    afifo_result_iterator_t() noexcept : base_type{} {}
    afifo_result_iterator_t(std::unique_ptr<node_type> && llist) noexcept : 
      base_type{llist.get()}
      { llist.release(); }
      
    afifo_result_iterator_t( afifo_result_iterator_t && rh ) noexcept ;
    afifo_result_iterator_t & operator=( afifo_result_iterator_t && rh ) noexcept { swap( rh ); return *this; }
    
    using base_type::empty;
    std::pair<user_obj_type, bool> pull();
    void swap( afifo_result_iterator_t & rh ) noexcept;
    };
    
    
  template<typename T>
  afifo_result_iterator_t<T>::afifo_result_iterator_t( afifo_result_iterator_t && rh ) noexcept :
      base_type{ std::move(rh)}
    {}
  
  template<typename T>
  void afifo_result_iterator_t<T>::swap( afifo_result_iterator_t & rh ) noexcept
    {
    base_type::swap(rh);
    }
    
  template<typename T>
  std::pair<typename afifo_result_iterator_t<T>::user_obj_type, bool>
  afifo_result_iterator_t<T>::pull()
    {
    std::unique_ptr<node_type> detached_node { base_type::pull() };
    
    if( nullptr != detached_node )
      {
      std::pair<user_obj_type, bool> result { std::move( detached_node->value ),  true };
      return result;
      }
    return {};
    }
    
  ///\brief lifo aggregated pop queue used internaly for node managment
  template<typename USER_OBJ_TYPE>
  class afifo_t 
      : public afifo_internal_tmpl<USER_OBJ_TYPE>
    {
  public:
    using user_obj_type =  USER_OBJ_TYPE;
    using base_type = afifo_internal_tmpl<user_obj_type>;
    using node_type = typename base_type::node_type;
    using pop_iterator_type = afifo_result_iterator_t<user_obj_type>;
    
  public:
    afifo_t() : base_type()/*, free_node_to_reuse_()*/ {}
    afifo_t( afifo_t const & ) = delete;
    afifo_t & operator=( afifo_t const & ) = delete;
    
    void push( user_obj_type && user_data );
    std::pair<pop_iterator_type, bool> pull();
    };
    
  template<typename T>
  void afifo_t<T>::push( user_obj_type && user_data )
    {
    std::unique_ptr<node_type> next_node { std::make_unique<node_type>(std::forward<user_obj_type>(user_data)) };
    base_type::push( next_node.get() );
    next_node.release();
    }
  
  template<typename T>
  std::pair<typename afifo_t<T>::pop_iterator_type, bool>
  afifo_t<T>::pull()
    {
    std::unique_ptr<node_type> node_list { base_type::pull() };
    bool success = static_cast<bool>(node_list);
    return {pop_iterator_type{ std::move(node_list) }, success };
    }

  //----------------------------------------------------------------------------------------------------------------------
  //
  // fifo_queue_t
  //
  //----------------------------------------------------------------------------------------------------------------------
  
  template<typename USER_OBJ_TYPE>
  class fifo_queue_t
    : public fifo_queue_internal_tmpl<queue_envelope_t<USER_OBJ_TYPE>>
  {
  public:
    typedef USER_OBJ_TYPE user_obj_type;
    typedef queue_envelope_t<user_obj_type> envelope_type;
    typedef fifo_queue_internal_tmpl<envelope_type> base_type;

  public:
    fifo_queue_t() : base_type(){}
    ~fifo_queue_t()
      {
      try
        {
//         if( !this->finish_waiting())
//           this->finish_waiting( true );

        for(;;)
          {
          envelope_type * any_data = base_type::pull();
          if( any_data )
            delete any_data;
          else
            break;
          }
        
      } 
      catch(...)
        {}
      }
    void push( user_obj_type const & user_data ) {  base_type::push( new envelope_type( user_data ) ); }
    void push( user_obj_type && user_data ) { base_type::push( new envelope_type( std::move(user_data) ) ); }
    
    std::pair<user_obj_type,bool> pull()
      {
      std::unique_ptr<envelope_type> envelope{ base_type::pull() };
      if( envelope )  
        return { std::move(envelope->value), true };
      return {};
      }
      
     std::pair<user_obj_type,bool> pull_wait( size_t sleep_tm )
      {
      std::unique_ptr<envelope_type> envelope( base_type::pull_wait( sleep_tm ) );
      if( envelope )  
        return { std::move(envelope->value), true };
      return {};
      }
    };

  //----------------------------------------------------------------------------------------------------------------------
  //
  // common functional access methods
  //
  //----------------------------------------------------------------------------------------------------------------------
    
  template<typename queue_type, typename user_obj_type>
  void push( queue_type & queue, user_obj_type && user_data ) 
    {
    queue.push( std::forward<user_obj_type>(user_data) ) ;
    }
   
  template<typename queue_type>
  inline auto pull( queue_type & queue )
    {
    return queue.pull();
    }

}

